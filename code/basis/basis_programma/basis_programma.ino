/*
  BASIS PROGRAMMA (gebaseerd op linefollowerfinale/stap4)
  Alles uit de opdracht werkt hier samen, zonder sensoren of motoren:
    - instelbare parameters met SerialCommands
    - cyclustijd
    - debug on/off
    - start/stop (commando of drukknop via een externe interrupt)
    - parameters opgeslagen in het EEPROM-geheugen
    - draadloze communicatie (BLE)
  Voorbeeld met de cyclus: zolang er gestart is, wisselt de LED elke cyclus -> knipperende LED.

  Seriële monitor: 115200 baud, "Both NL & CR"
  Draadloos (iPhone): app 'BLESerial nRF'. Zoek naar 'linefollowerArno', verbind en stuur dezelfde commando's.
  Zet in de app het regeleinde op 'CR', 'LF' of 'CR+LF'.

  Commando's:
    start                start (LED knippert)
    stop                 stop (LED uit)
    set cycle <us>       cyclustijd in microseconden (bv. 500000 = LED wisselt elke 0,5 s), wordt bewaard in EEPROM
    set debug on|off     elke cyclus een regel met cyclusnummer en rekentijd
    debug                toon alle parameters

  Start/stop-knop: GPIO 23 naar GND (interrupt op dalende flank). LED: GPIO 27.
*/

#include "SerialCommand.h"
#include "EEPROMAnything.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define Baudrate 115200

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
    SerialPort.println("linefollowerArno verbonden. Typ 'debug'.");
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
unsigned long previous, calculationTime;

// ---------- hardware ----------
const int START_PIN = 23;     // knop naar GND (INPUT_PULLUP)
const int LED_PIN = 27;

// ---------- parameters (EEPROM) ----------
const unsigned long PARAM_MAGIC = 0xBA5E0001;

struct param_t
{
  unsigned long magic;
  unsigned long cycleTime;    // microseconden
} params;

// ---------- toestand ----------
bool running = false;
bool debug = false;
bool ledState = false;
unsigned long cycleCount = 0;

void startRun()
{
  running = true;
  cycleCount = 0;
  SerialPort.println("run on");
}

void stopRun()
{
  running = false;
  ledState = false;
  digitalWrite(LED_PIN, LOW);
  SerialPort.println("run off");
}

// ---------- knop (hardware-interrupt) ----------
const unsigned long BUTTON_DEBOUNCE_MS = 20;   // zo lang moet de knop ingedrukt blijven na de flank
const unsigned long BUTTON_RELEASE_MS = 50;    // zo lang moet de knop los zijn voor een nieuwe druk

volatile bool buttonEvent = false;             // gezet door de ISR
volatile unsigned long buttonEventTime = 0;
bool buttonReleased = true;                    // knop is losgelaten sinds de vorige druk
unsigned long buttonHighSince = 0;

// Interrupt service routine: zo kort mogelijk, enkel de flank onthouden
void IRAM_ATTR onButtonInterrupt()
{
  if (!buttonEvent)
  {
    buttonEvent = true;
    buttonEventTime = millis();
  }
}

void updateButton()
{
  bool pressed = (digitalRead(START_PIN) == LOW);

  // knop moet eerst even los zijn voor een volgende druk telt (dender bij loslaten negeren)
  if (pressed) buttonHighSince = millis();
  else if (!buttonReleased && millis() - buttonHighSince >= BUTTON_RELEASE_MS) buttonReleased = true;

  // flank gezien door de interrupt: na de debouncetijd controleren of de knop nog ingedrukt is
  if (buttonEvent && millis() - buttonEventTime >= BUTTON_DEBOUNCE_MS)
  {
    buttonEvent = false;
    if (pressed && buttonReleased)
    {
      buttonReleased = false;
      if (running) stopRun(); else startRun();
    }
  }
}

// ---------- setup ----------
void setup()
{
  SerialPort.begin(Baudrate);
  bleBegin();

  sCmd.addCommand("set", onSet);
  sCmd.addCommand("debug", onDebug);
  sCmd.addCommand("start", onStart);
  sCmd.addCommand("stop", onStop);
  sCmd.setDefaultHandler(onUnknownCommand);

  EEPROM.begin(sizeof(params));          // nodig op de ESP32
  EEPROM_readAnything(0, params);
  if (params.magic != PARAM_MAGIC)       // eerste keer: standaardwaarden
  {
    params.magic = PARAM_MAGIC;
    params.cycleTime = 500000;           // 0,5 s
    EEPROM_writeAnything(0, params);
  }

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(START_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(START_PIN), onButtonInterrupt, FALLING);   // knop naar GND = dalende flank

  SerialPort.println("ready");
}

// ---------- loop ----------
void loop()
{
  sCmd.readSerial();
  bleFlush();
  updateButton();

  unsigned long current = micros();
  if (current - previous >= params.cycleTime)
  {
    previous = current;

    /* het eigenlijke werk van één cyclus: hier een knipperende LED */
    if (running)
    {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      cycleCount++;
    }

    unsigned long difference = micros() - current;
    if (difference > calculationTime) calculationTime = difference;

    if (debug)
    {
      SerialPort.print("cyclus: ");    SerialPort.print(cycleCount);
      SerialPort.print("\tled: ");     SerialPort.print(ledState ? "aan" : "uit");
      SerialPort.print("\trekentijd (us): "); SerialPort.println(difference);
    }
  }
}

// ---------- commando's ----------
void onUnknownCommand(char *command)
{
  SerialPort.print("unknown command: \"");
  SerialPort.print(command);
  SerialPort.println("\"");
}

void onSet()
{
  char* param = sCmd.next();
  char* value = sCmd.next();
  if (param == NULL || value == NULL) { SerialPort.println("gebruik: set <cycle|debug> <waarde>"); return; }

  if (strcmp(param, "cycle") == 0)
  {
    long newCycleTime = atol(value);
    if (newCycleTime < 500) { SerialPort.println("minimum 500 us"); return; }
    params.cycleTime = newCycleTime;
  }
  else if (strcmp(param, "debug") == 0)
  {
    if (strcmp(value, "on") == 0) debug = true;
    else if (strcmp(value, "off") == 0) debug = false;
    else { SerialPort.println("gebruik: set debug on|off"); return; }
    SerialPort.println(debug ? "debug on" : "debug off");
    return;                    // geen EEPROM-schrijfactie nodig
  }
  else { SerialPort.println("onbekende parameter"); return; }

  EEPROM_writeAnything(0, params);
  SerialPort.println("ok");
}

void onDebug()
{
  SerialPort.print("cycle time (us): ");  SerialPort.println(params.cycleTime);
  SerialPort.print("running: ");          SerialPort.println(running ? "ja" : "nee");
  SerialPort.print("debug: ");            SerialPort.println(debug ? "on" : "off");
  SerialPort.print("cycli: ");            SerialPort.println(cycleCount);
  SerialPort.print("calculation time (us): ");
  SerialPort.println(calculationTime);
  calculationTime = 0;
}

void onStart()
{
  if (running) { SerialPort.println("loopt al"); return; }
  startRun();
}

void onStop()
{
  stopRun();
}
