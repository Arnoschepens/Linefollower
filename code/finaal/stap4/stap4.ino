/*
  STAP 4: volledige PID-lijnvolger + draadloos via BLE (logica uit de 3 cursuspagina's)
  Seriële monitor: 115200 baud, "Both NL & CR"

  Commando's:
    start                start het rijden (zelfde als de knop)
    stop                 stopt het rijden en de motoren meteen
    calibrate black | calibrate white
    set power <0-100>    basissnelheid in procent
    set kp <getal>       proportioneel
    set ki <getal>       integrerend   (in seconden, wordt herrekend naar de cyclustijd)
    set kd <getal>       differentiërend (in seconden, idem)
    set diff <0-1>       verdeling van de output over de wielen (begin 0.5)
    set cycle <us>       cyclustijd (Ki en Kd worden mee herrekend)
    set lost <100-1000>  grens voor 'geen lijn': zwartste sensor boven dit niveau = lijn kwijt (standaard 900)
    set losttime <ms>    zo lang zonder lijn verder sturen voor hij stopt (0-2000, standaard 100)
    set debug on|off     elke 100 ms een regel met positie, output, P, I, D en motoren (ook tijdens het rijden)
    live on | live off   genormaliseerde waarden, positie en error (alleen als de robot stilstaat)
    debug                alle parameters en waarden
    motor left|right <-100..100> | motor stop
  Start/stop-knop (GPIO 23) en controle-LED (GPIO 27: knippert = stil, brandt = rijden)
  De knop werkt met een hardware-interrupt (FALLING-flank); de ISR zet enkel een vlag,
  de loop controleert de knop (debounce) en start of stopt het rijden.
  Lijn kwijt: de robot rijdt nog 'losttime' ms verder met de laatste sturing
  (zo komt hij in een scherpe bocht terug op de lijn) en stopt pas daarna.

  Draadloos (iPhone): app 'BLESerial nRF' (App Store). Zoek in de app naar 'linefollowerArno', verbind,
  en stuur dezelfde commando's. Alles wat naar de seriële monitor gaat, komt ook in de app.
  Zet in de app het regeleinde op 'CR', 'LF' of 'CR+LF' (alle drie werken).

  Beperking motorspanning: H-brug op 7,4 V, motoren max 6 V -> 100% sturing = 81% duty.
*/

#include "SerialCommand.h"
#include "EEPROMAnything.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>          // zoals in je werkende BLE-programma

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
const int SENSOR_COUNT = 6;
const int sensor[SENSOR_COUNT] = {36, 39, 34, 35, 32, 33};   // links -> rechts

const int AIN1 = 17, AIN2 = 18, BIN1 = 22, BIN2 = 21;        // H-brug-uitgangen A en B
// Motor-uitgang A zit op jouw robot aan het RECHTERwiel en B aan het LINKERwiel (getest met 'motor left').
// Daarom: 'links' (index 0) gebruikt de B-pinnen en 'rechts' (index 1) de A-pinnen.
const int MOTOR_PINS[4] = {BIN1, BIN2, AIN1, AIN2};   // links, rechts
const bool MOTOR_A_INVERT = false;
const bool MOTOR_B_INVERT = false;

// Stuurrichting: de cursus zegt 'rijdt de robot van de lijn weg, wissel dan powerLinks en powerRechts'.
// true  = lijn rechts van het midden -> linkermotor sneller (robot stuurt naar rechts)
// false = oorspronkelijke verdeling uit de cursus
// linefollowerfinale3: op false gezet. Met true moest Kp negatief zijn om juist te sturen; dan werkt een
// positieve Kd net omgekeerd (versterkt de beweging in plaats van ze te dempen). Nu zijn Kp, Ki en Kd allemaal positief.
const bool SWAP_STEERING = false;

const int START_PIN = 23;     // knop naar GND (INPUT_PULLUP)
const int LED_PIN = 27;

// Spanningsbegrenzing: 100% sturing = 6 V = 6/7,4 = 81% duty
const float BATTERY_VOLTAGE = 7.4;
const float MOTOR_MAX_VOLTAGE = 6.0;
const int MAX_PWM = (int)(255.0 * MOTOR_MAX_VOLTAGE / BATTERY_VOLTAGE);

// ---------- parameters (EEPROM) ----------
const unsigned long PARAM_MAGIC = 0x5D1F0003;

struct param_t
{
  unsigned long magic;
  unsigned long cycleTime;
  int black[SENSOR_COUNT];
  int white[SENSOR_COUNT];
  float power;       // procent 0..100
  float diff;        // 0..1
  float kp;
  float ki;          // intern: Ki* = Ki * cycleTime (seconden)
  float kd;          // intern: Kd* = Kd / cycleTime (seconden)
  int lostLevel;     // geen lijn als de zwartste sensor boven dit niveau ligt
  int lostTime;      // ms zonder lijn verder sturen voor de robot stopt (set losttime)
} params;

// ---------- meetwaarden ----------
const float SENSOR_DISTANCE = 8;                        // mm tussen de sensoren (gemeten)
const int WEIGHTS[SENSOR_COUNT] = {-50, -30, -10, 10, 30, 50};
const int DEADBAND = 125;
// Automatische lijndetectie (zonder vaste grens op de vloer):
//  - lijn = de zwartste sensor ligt duidelijk onder het gemiddelde van alle sensoren (contrast)
//  - of de zwartste sensor is zeker zwart (bv. dwarslijn of brede lijn onder alle sensoren)
const int LINE_CONTRAST = 200;     // minimaal verschil tussen gemiddelde en zwartste sensor
const int LINE_SURE = 400;         // zwartste sensor onder dit niveau = altijd lijn
const int DEFAULT_LOST_LEVEL = 900;   // bovengrens: boven dit niveau nooit lijn   // zwartste sensor boven dit niveau (0 = zwart, 1000 = wit): geen lijn

int rawValues[SENSOR_COUNT];
int normValues[SENSOR_COUNT];
float position = 0;            // kwadratische interpolatie in mm, 0 = midden
float weightedPos = 0;         // gewogen gemiddelde -50..+50 (ter vergelijking)
bool lineFound = false;
int contrast = 0;             // gemiddelde van alle sensoren min de zwartste
int darkest = 1000;           // laagste genormaliseerde waarde (de zwartste sensor)

// ---------- regelaar ----------
bool running = false;
bool lineLost = false;

// Lijn even kwijt (scherpe bocht, ruis): eerst nog verder rijden met de laatste sturing, pas daarna stoppen
const int DEFAULT_LOST_TIME = 100;   // standaard voor params.lostTime (ms)
bool lineMissing = false;            // lijn op dit moment niet gezien
unsigned long lineMissingSince = 0;
float error = 0, lastErr = 0, iTerm = 0, output = 0;
int powerLeft = 0, powerRight = 0;

bool debug = false;
unsigned long lastDebug = 0;
float pTerm = 0, dTerm = 0;
bool liveMode = false;
unsigned long lastLive = 0;

// ---------- PWM (ESP32 core 2.x en 3.x) ----------
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  #define PWM_INIT(ch, pin) ledcAttach(pin, 20000, 8)
  #define PWM_OUT(ch, pin, duty) ledcWrite(pin, duty)
#else
  #define PWM_INIT(ch, pin) do { ledcSetup(ch, 20000, 8); ledcAttachPin(pin, ch); } while (0)
  #define PWM_OUT(ch, pin, duty) ledcWrite(ch, duty)
#endif

// motor 0 = links, 1 = rechts, percent -100..100 (100% = 6 V)
void setMotor(int motor, int percent)
{
  percent = constrain(percent, -100, 100);
  bool forward = (percent >= 0);
  if (motor == 0 ? MOTOR_A_INVERT : MOTOR_B_INVERT) forward = !forward;
  int duty = abs(percent) * MAX_PWM / 100;
  PWM_OUT(motor * 2,     MOTOR_PINS[motor * 2],     forward ? duty : 0);
  PWM_OUT(motor * 2 + 1, MOTOR_PINS[motor * 2 + 1], forward ? 0 : duty);
}

void motorsOff()
{
  setMotor(0, 0);
  setMotor(1, 0);
  powerLeft = 0;
  powerRight = 0;
}

// ---------- start / stop ----------
void startRun()
{
  pTerm = 0; dTerm = 0;
  iTerm = 0;                       // iTerm altijd op 0 bij een nieuwe start
  lastErr = -position;             // geen sprong in de D-term
  lineLost = false;
  lineMissing = false;
  running = true;
  SerialPort.println("run on");
}

void stopRun()
{
  running = false;
  motorsOff();
}

// ---------- knop (hardware-interrupt) en LED ----------
const unsigned long BUTTON_DEBOUNCE_MS = 20;   // zo lang moet de knop ingedrukt blijven na de flank
const unsigned long BUTTON_RELEASE_MS = 50;    // zo lang moet de knop los zijn voor een nieuwe druk

volatile bool buttonEvent = false;             // gezet door de ISR
volatile unsigned long buttonEventTime = 0;
bool buttonReleased = true;                    // knop is losgelaten sinds de vorige druk
unsigned long buttonHighSince = 0;
unsigned long lastBlink = 0;
bool ledState = false;

// Interrupt service routine: zo kort mogelijk, enkel de flank onthouden
void IRAM_ATTR onButtonInterrupt()
{
  if (!buttonEvent)
  {
    buttonEvent = true;
    buttonEventTime = millis();
  }
}

void updateButtonAndLed()
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
      if (running) { stopRun(); SerialPort.println("run off"); }
      else startRun();
    }
  }

  if (running) digitalWrite(LED_PIN, HIGH);
  else if (millis() - lastBlink >= 250)
  {
    lastBlink = millis();
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }
}

// ---------- setup ----------
void setup()
{
  SerialPort.begin(Baudrate);
  bleBegin();

  sCmd.addCommand("set", onSet);
  sCmd.addCommand("debug", onDebug);
  sCmd.addCommand("calibrate", onCalibrate);
  sCmd.addCommand("live", onLive);
  sCmd.addCommand("start", onStart);
  sCmd.addCommand("stop", onStop);
  sCmd.addCommand("motor", onMotor);
  sCmd.setDefaultHandler(onUnknownCommand);

  EEPROM.begin(sizeof(params));          // nodig op de ESP32
  EEPROM_readAnything(0, params);
  if (params.magic != PARAM_MAGIC)       // eerste keer: standaardwaarden
  {
    params.magic = PARAM_MAGIC;
    params.cycleTime = 10000;
    for (int i = 0; i < SENSOR_COUNT; i++) { params.black[i] = 3500; params.white[i] = 500; }
    params.power = 40;
    params.diff = 0.5;
    params.kp = 0;
    params.ki = 0;
    params.kd = 0;
    EEPROM_writeAnything(0, params);
  }

  if (params.lostLevel < 100 || params.lostLevel > 1000)   // nieuw veld: standaardwaarde
  {
    params.lostLevel = DEFAULT_LOST_LEVEL;
    EEPROM_writeAnything(0, params);
  }

  if (params.lostTime < 0 || params.lostTime > 2000)     // nieuw veld: standaardwaarde
  {
    params.lostTime = DEFAULT_LOST_TIME;
    EEPROM_writeAnything(0, params);
  }

  analogReadResolution(12);
  for (int i = 0; i < SENSOR_COUNT; i++)
  {
    pinMode(sensor[i], INPUT);
    analogSetPinAttenuation(sensor[i], ADC_11db);
  }

  pinMode(START_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(START_PIN), onButtonInterrupt, FALLING);   // knop naar GND = dalende flank
  pinMode(LED_PIN, OUTPUT);
  for (int i = 0; i < 4; i++) { pinMode(MOTOR_PINS[i], OUTPUT); PWM_INIT(i, MOTOR_PINS[i]); }
  motorsOff();

  SerialPort.println("ready");
}

// ---------- loop ----------
void loop()
{
  sCmd.readSerial();
  bleFlush();
  updateButtonAndLed();

  unsigned long current = micros();
  if (current - previous >= params.cycleTime)
  {
    previous = current;

    /* sensoren lezen en normaliseren: zwart -> 0, wit -> 1000 */
    for (int i = 0; i < SENSOR_COUNT; i++)
    {
      rawValues[i] = analogRead(sensor[i]);
      normValues[i] = map(rawValues[i], params.black[i], params.white[i], 0, 1000);
      normValues[i] = constrain(normValues[i], 0, 1000);
    }

    /* interpolatie 1: gewogen gemiddelde (ter vergelijking) */
    long sumDark = 0, sumWeighted = 0;
    for (int i = 0; i < SENSOR_COUNT; i++)
    {
      int dark = (1000 - normValues[i]) - DEADBAND;
      if (dark < 0) dark = 0;
      sumDark += dark;
      sumWeighted += (long)dark * WEIGHTS[i];
    }
    darkest = 1000;
    for (int i = 0; i < SENSOR_COUNT; i++) if (normValues[i] < darkest) darkest = normValues[i];
    long sumNorm = 0;
    for (int i = 0; i < SENSOR_COUNT; i++) sumNorm += normValues[i];
    contrast = (int)(sumNorm / SENSOR_COUNT) - darkest;
    lineFound = (sumDark > 0) && (darkest <= params.lostLevel) &&
                (darkest <= LINE_SURE || contrast >= LINE_CONTRAST);
    if (lineFound) weightedPos = (float)sumWeighted / sumDark;

    /* interpolatie 2: kwadratisch (dit is de positie voor de regelaar, in mm) */
    int index = 0;
    for (int i = 1; i < SENSOR_COUNT; i++) if (normValues[i] < normValues[index]) index = i;
    if (index == 0) index = 1;
    else if (index == SENSOR_COUNT - 1) index = SENSOR_COUNT - 2;

    long sZero = normValues[index];
    long sMinusOne = normValues[index - 1];
    long sPlusOne = normValues[index + 1];

    float b = ((float)(sPlusOne - sMinusOne)) / 2;
    float a = sPlusOne - b - sZero;
    if (a > 0)
    {
      float p = -b / (2 * a);
      p = constrain(p, -1.0f, 1.0f);
      p += index - (SENSOR_COUNT - 1) / 2.0f;
      position = p * SENSOR_DISTANCE;
    }

    /* PID: enkel als de robot rijdt */
    if (running)
    {
      if (!lineFound)
      {
        /* lijn kwijt: motoren houden hun laatste waarde (robot blijft in dezelfde richting sturen) */
        if (!lineMissing)
        {
          lineMissing = true;
          lineMissingSince = millis();
        }
        else if (millis() - lineMissingSince >= (unsigned long)params.lostTime)
        {
          stopRun();              // te lang geen lijn: stoppen
          lineLost = true;
        }
      }
      else
      {
        if (lineMissing)
        {
          lineMissing = false;
          lastErr = -position;    // lijn terug: geen sprong in de D-term
        }

        /* bereken error = setpoint - positie */
        error = -position;

        /* proportioneel regelen */
        pTerm = error * params.kp;
        output = pTerm;

        /* integrerend regelen */
        iTerm += params.ki * error;
        iTerm = constrain(iTerm, -510, 510);
        output += iTerm;

        /* differentiërend regelen */
        dTerm = params.kd * (error - lastErr);
        output += dTerm;
        lastErr = error;

        /* output begrenzen tot wat fysiek mogelijk is */
        output = constrain(output, -510, 510);

        /* verdelen over de 2 motoren (power in 0..255-schaal, zoals in de cursus) */
        float power = params.power * 255.0f / 100.0f;
        float left, right;
        if (output >= 0)
        {
          left = constrain(power + params.diff * output, -255.0f, 255.0f);
          right = constrain(left - output, -255.0f, 255.0f);
          left = right + output;
        }
        else
        {
          right = constrain(power - params.diff * output, -255.0f, 255.0f);
          left = constrain(right + output, -255.0f, 255.0f);
          right = left - output;
        }

        /* naar procent (255 = 100% = 6 V); setMotor begrenst op +-100% */
        /* geen enkele motor mag achteruit draaien: negatieve waarden worden 0 (alleen vertragen) */
        left = max(left, 0.0f);
        right = max(right, 0.0f);

        if (SWAP_STEERING) { float t = left; left = right; right = t; }

        powerLeft = (int)(left * 100.0f / 255.0f);
        powerRight = (int)(right * 100.0f / 255.0f);
        setMotor(0, powerLeft);
        setMotor(1, powerRight);
      }
    }
  }

  unsigned long difference = micros() - current;
  if (difference > calculationTime) calculationTime = difference;

  if (lineLost) { lineLost = false; SerialPort.println("lijn kwijt: gestopt"); }

  /* debug-vlag (set debug on): regelaar volgen, formaat werkt ook in de Serial Plotter */
  if (debug && millis() - lastDebug >= 100)
  {
    lastDebug = millis();
    SerialPort.print("pos:");   SerialPort.print(position, 1);
    SerialPort.print(",out:");  SerialPort.print(output, 1);
    SerialPort.print(",P:");    SerialPort.print(pTerm, 1);
    SerialPort.print(",I:");    SerialPort.print(iTerm, 1);
    SerialPort.print(",D:");    SerialPort.print(dTerm, 1);
    SerialPort.print(",L:");    SerialPort.print(powerLeft);
    SerialPort.print(",R:");    SerialPort.println(powerRight);
  }

  /* live-uitvoer enkel tijdens stilstand (seriële uitvoer vertraagt de cyclus) */
  if (liveMode && !running && millis() - lastLive >= 200)
  {
    lastLive = millis();
    for (int i = 0; i < SENSOR_COUNT; i++) { SerialPort.print(normValues[i]); SerialPort.print("\t"); }
    if (lineFound)
    {
      SerialPort.print("zwartste: "); SerialPort.print(darkest);
      SerialPort.print("\tcontrast: "); SerialPort.print(contrast);
      SerialPort.print("\tgewogen: "); SerialPort.print(weightedPos, 1);
      SerialPort.print("\tpositie mm: "); SerialPort.println(position, 1);
    }
    else { SerialPort.print("geen lijn (zwartste: "); SerialPort.print(darkest); SerialPort.print(", contrast: "); SerialPort.print(contrast); SerialPort.println(")"); }
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
  if (param == NULL || value == NULL) { SerialPort.println("gebruik: set <power|kp|ki|kd|diff|cycle|lost|losttime|debug> <waarde>"); return; }

  if (strcmp(param, "cycle") == 0)
  {
    long newCycleTime = atol(value);
    if (newCycleTime < 500) { SerialPort.println("minimum 500 us"); return; }
    float ratio = ((float) newCycleTime) / ((float) params.cycleTime);

    params.ki *= ratio;
    params.kd /= ratio;

    params.cycleTime = newCycleTime;
  }
  else if (strcmp(param, "ki") == 0)
  {
    float cycleTimeInSec = ((float) params.cycleTime) / 1000000;
    params.ki = atof(value) * cycleTimeInSec;
  }
  else if (strcmp(param, "kd") == 0)
  {
    float cycleTimeInSec = ((float) params.cycleTime) / 1000000;
    params.kd = atof(value) / cycleTimeInSec;
  }
  else if (strcmp(param, "debug") == 0)
  {
    if (strcmp(value, "on") == 0) debug = true;
    else if (strcmp(value, "off") == 0) debug = false;
    else { SerialPort.println("gebruik: set debug on|off"); return; }
    SerialPort.println(debug ? "debug on" : "debug off");
    return;                    // geen EEPROM-schrijfactie nodig
  }
  else if (strcmp(param, "lost") == 0) params.lostLevel = constrain(atoi(value), 100, 1000);
  else if (strcmp(param, "losttime") == 0) params.lostTime = constrain(atoi(value), 0, 2000);
  else if (strcmp(param, "kp") == 0) params.kp = atof(value);
  else if (strcmp(param, "power") == 0) params.power = constrain(atof(value), 0.0f, 100.0f);
  else if (strcmp(param, "diff") == 0) params.diff = constrain(atof(value), 0.0f, 1.0f);
  else { SerialPort.println("onbekende parameter"); return; }

  EEPROM_writeAnything(0, params);
  SerialPort.println("ok");
}

void onCalibrate()
{
  char* param = sCmd.next();
  if (param == NULL) { SerialPort.println("gebruik: calibrate black of calibrate white"); return; }

  stopRun();
  if (strcmp(param, "black") == 0)
  {
    SerialPort.print("start calibrating black... ");
    for (int i = 0; i < 6; i++) params.black[i] = analogRead(sensor[i]);
    SerialPort.println("done");
  }
  else if (strcmp(param, "white") == 0)
  {
    SerialPort.print("start calibrating white... ");
    for (int i = 0; i < 6; i++) params.white[i] = analogRead(sensor[i]);
    SerialPort.println("done");
  }
  else { SerialPort.println("gebruik: calibrate black of calibrate white"); return; }

  EEPROM_writeAnything(0, params);
}

void onLive()
{
  char* arg = sCmd.next();
  liveMode = (arg != NULL && strcmp(arg, "on") == 0);
  SerialPort.println(liveMode ? "live on" : "live off");
}

void onStart()
{
  if (running) { SerialPort.println("loopt al"); return; }
  startRun();                              // meldt zelf 'run on'
}

void onStop()
{
  stopRun();
  SerialPort.println("run off");
}

void onMotor()
{
  char* which = sCmd.next();
  char* value = sCmd.next();
  if (which == NULL) { SerialPort.println("gebruik: motor left|right <-100..100> of motor stop"); return; }

  stopRun();
  if (strcmp(which, "stop") == 0) { motorsOff(); SerialPort.println("motoren stop"); return; }
  if (value == NULL) { SerialPort.println("geef ook een percentage, bv: motor left 50"); return; }

  if (strcmp(which, "left") == 0) setMotor(0, atoi(value));
  else if (strcmp(which, "right") == 0) setMotor(1, atoi(value));
  else SerialPort.println("gebruik: motor left|right <-100..100>");
}

void printArray(const char *label, int *values)
{
  SerialPort.print(label);
  for (int i = 0; i < SENSOR_COUNT; i++) { SerialPort.print(values[i]); SerialPort.print("\t"); }
  SerialPort.println();
}

void onDebug()
{
  SerialPort.print("cycle time: ");
  SerialPort.println(params.cycleTime);

  float cycleTimeInSec = ((float) params.cycleTime) / 1000000;
  SerialPort.print("Power (%): ");  SerialPort.println(params.power);
  SerialPort.print("Diff: ");       SerialPort.println(params.diff);
  SerialPort.print("Kp: ");         SerialPort.println(params.kp);
  SerialPort.print("Ki: ");         SerialPort.println(params.ki / cycleTimeInSec);
  SerialPort.print("Lost level: "); SerialPort.println(params.lostLevel);
  SerialPort.print("Lost time (ms): "); SerialPort.println(params.lostTime);
  SerialPort.print("Kd: ");         SerialPort.println(params.kd * cycleTimeInSec);

  printArray("black:  ", params.black);
  printArray("white:  ", params.white);
  printArray("raw:    ", rawValues);
  printArray("normal: ", normValues);

  SerialPort.print("positie (mm): ");   SerialPort.println(position, 1);
  SerialPort.print("gewogen: ");        SerialPort.println(weightedPos, 1);
  SerialPort.print("output: ");         SerialPort.println(output, 1);
  SerialPort.print("motor links/rechts (%): ");
  SerialPort.print(powerLeft); SerialPort.print(" / "); SerialPort.println(powerRight);
  SerialPort.print("max PWM (6 V): ");  SerialPort.println(MAX_PWM);

  SerialPort.print("calculation time: ");
  SerialPort.println(calculationTime);
  calculationTime = 0;
}
