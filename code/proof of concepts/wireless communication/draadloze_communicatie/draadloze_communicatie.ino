/*
  DRAADLOZE COMMUNICATIE: proof of concept (BLE-deel overgenomen uit linefollowerfinale/stap4)
  Toont duplex communicatie tussen de ESP32 en een smartphone via Bluetooth Low Energy (BLE),
  met de Nordic UART Service (NUS).

  Smartphone (iPhone): app 'BLESerial nRF' (App Store). Zoek in de app naar 'linefollowerArno', verbind
  en stuur een commando. Zet in de app het regeleinde op 'CR', 'LF' of 'CR+LF' (alle drie werken).
  Alles wat naar de telefoon gaat, komt ook in de seriële monitor (115200 baud, "Both NL & CR").

  Commando's (via telefoon of USB):
    ping            antwoordt 'pong'                       (telefoon -> ESP32 -> telefoon)
    led on|off      LED op GPIO 27 aan of uit + bevestiging
    <iets anders>   antwoordt 'ontvangen: <commando>'
  Daarnaast stuurt de ESP32 elke 5 s zelf een bericht (ESP32 -> telefoon, zonder vraag).
*/

#include "SerialCommand.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define Baudrate 115200

const int LED_PIN = 27;

// ---------- BLE (Nordic UART Service) + seriële poort als één "SerialPort" ----------
#define BLE_NAME "linefollowerArno"
#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // telefoon -> robot
#define NUS_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // robot -> telefoon

const uint16_t BLE_BUF = 512;
const uint8_t BLE_CHUNK = 20;                  // bytes per notificatie (standaard MTU)
const unsigned long BLE_NOTIFY_MS = 15;        // minimale tijd tussen twee notificaties

uint8_t bleRx[BLE_BUF];
volatile uint16_t bleRxHead = 0, bleRxTail = 0;
uint8_t bleTx[BLE_BUF];
uint16_t bleTxHead = 0, bleTxTail = 0;
volatile bool bleConnected = false;
volatile bool bleGreet = false;                // begroeting nog te sturen na het verbinden
volatile unsigned long bleConnectedAt = 0;
BLECharacteristic *bleTxChar = NULL;
unsigned long bleLastNotify = 0;

void blePush(uint8_t b)
{
  uint16_t next = (bleRxHead + 1) % BLE_BUF;
  if (next != bleRxTail) { bleRx[bleRxHead] = b; bleRxHead = next; }
}

class BleServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *server) { bleConnected = true; bleGreet = true; bleConnectedAt = millis(); }
  void onDisconnect(BLEServer *server)
  {
    bleConnected = false;
    bleTxHead = bleTxTail = 0;
    server->getAdvertising()->start();         // weer vindbaar voor de telefoon
  }
};

class BleRxCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristic)
  {
    auto value = characteristic->getValue();   // std::string (core 2.x) of String (core 3.x)
    size_t len = value.length();
    for (size_t i = 0; i < len; i++) blePush((uint8_t)value[i]);

    // Veel apps sturen geen regeleinde mee. Eindigt het bericht niet op CR of LF, voeg dan zelf een CR toe.
    if (len > 0 && value[len - 1] != '\r' && value[len - 1] != '\n') blePush('\r');
  }
};

// Eén "poort" voor SerialCommand: leest van USB-serieel en BLE, schrijft naar beide
class DualStream : public Stream
{
  public:
    void begin(unsigned long baud) { Serial.begin(baud); }

    int available() { return Serial.available() + ((bleRxHead + BLE_BUF - bleRxTail) % BLE_BUF); }

    int read()
    {
      if (Serial.available() > 0) return Serial.read();
      if (bleRxHead == bleRxTail) return -1;
      uint8_t b = bleRx[bleRxTail];
      bleRxTail = (bleRxTail + 1) % BLE_BUF;
      return b;
    }

    int peek()
    {
      if (Serial.available() > 0) return Serial.peek();
      if (bleRxHead == bleRxTail) return -1;
      return bleRx[bleRxTail];
    }

    void flush() {}

    size_t write(uint8_t b)
    {
      Serial.write(b);
      if (bleConnected)                         // alleen bufferen als er een telefoon verbonden is
      {
        uint16_t next = (bleTxHead + 1) % BLE_BUF;
        if (next != bleTxTail) { bleTx[bleTxHead] = b; bleTxHead = next; }   // vol: byte vallen
      }
      return 1;
    }

    using Print::write;
} SerialPort;

void bleBegin()
{
  BLEDevice::init(BLE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new BleServerCallbacks());

  BLEService *service = server->createService(NUS_SERVICE_UUID);

  bleTxChar = service->createCharacteristic(NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  bleTxChar->addDescriptor(new BLE2902());     // nodig zodat de telefoon meldingen kan aanzetten

  BLECharacteristic *rx = service->createCharacteristic(NUS_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new BleRxCallbacks());

  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
}

// Elke loop aanroepen: stuurt de wachtende tekst in stukjes van 20 bytes naar de telefoon
void bleFlush()
{
  if (bleGreet && bleConnected && millis() - bleConnectedAt > 1500)   // wacht tot de app genoteerd heeft voor meldingen
  {
    bleGreet = false;
    SerialPort.println("linefollowerArno verbonden. Typ 'ping'.");
  }

  if (!bleConnected || bleTxChar == NULL || bleTxHead == bleTxTail) return;
  if (millis() - bleLastNotify < BLE_NOTIFY_MS) return;
  bleLastNotify = millis();

  uint8_t chunk[BLE_CHUNK];
  uint8_t n = 0;
  while (n < BLE_CHUNK && bleTxTail != bleTxHead)
  {
    chunk[n++] = bleTx[bleTxTail];
    bleTxTail = (bleTxTail + 1) % BLE_BUF;
  }
  bleTxChar->setValue(chunk, n);
  bleTxChar->notify();
}

SerialCommand sCmd(SerialPort);

// ---------- periodiek bericht (ESP32 -> telefoon) ----------
const unsigned long HEARTBEAT_MS = 5000;
unsigned long lastHeartbeat = 0;

// ---------- setup ----------
void setup()
{
  SerialPort.begin(Baudrate);
  bleBegin();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  sCmd.addCommand("ping", onPing);
  sCmd.addCommand("led", onLed);
  sCmd.setDefaultHandler(onUnknownCommand);

  SerialPort.println("ready: verbind met 'linefollowerArno' en typ 'ping' of 'led on'");
}

// ---------- loop ----------
void loop()
{
  sCmd.readSerial();
  bleFlush();

  if (bleConnected && millis() - lastHeartbeat >= HEARTBEAT_MS)
  {
    lastHeartbeat = millis();
    SerialPort.print("ESP32 actief, uptime ");
    SerialPort.print(millis() / 1000);
    SerialPort.println(" s");
  }
}

// ---------- commando's ----------
void onPing()
{
  SerialPort.println("pong");
}

void onLed()
{
  char* arg = sCmd.next();
  if (arg != NULL && strcmp(arg, "on") == 0)       { digitalWrite(LED_PIN, HIGH); SerialPort.println("led aan"); }
  else if (arg != NULL && strcmp(arg, "off") == 0) { digitalWrite(LED_PIN, LOW);  SerialPort.println("led uit"); }
  else SerialPort.println("gebruik: led on of led off");
}

void onUnknownCommand(char *command)
{
  SerialPort.print("ontvangen: ");
  SerialPort.println(command);
}
