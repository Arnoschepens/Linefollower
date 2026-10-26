#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "EEPROMAnything.h"

// UUID's voor de Nordic UART Service (standaard voor nRF Connect op iOS)
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;

unsigned long previous, calculationTime;

// --------------------------- H-BRUG & KNOP CONFIGURATIE ---------------------------
const int AIN1 = 17; // Richting/PWM 1 linkermotor
const int AIN2 = 18; // Richting/PWM 2 linkermotor
const int BIN1 = 22; // Richting/PWM 1 rechtermotor
const int BIN2 = 21; // Richting/PWM 2 rechtermotor

const int BUTTON_PIN = 23; // Monostabiele start/stop knop (naar GND)
bool robotRunning = false;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

// --------------------------- SENSOR & IR CONFIGURATIE ---------------------------
const uint8_t SENSOR_COUNT = 6;
const uint8_t SENSOR_PINS[SENSOR_COUNT] = {36, 39, 34, 35, 32, 33}; // ADC1 pinnen (D2 t/m D7)
const uint8_t IR_PIN = 27; // Aangestuurde pin voor de IR-leds van de sensormodule

int sensorValues[SENSOR_COUNT];
bool sensorBlack[SENSOR_COUNT];
int currentPosition = -1;

// PID Variabelen voor de regelaar
int lastError = 0;
float integral = 0;

// --------------------------- PARAMETERS (Opgeslagen in EEPROM) ---------------------------
struct param_t
{
  unsigned long cycleTime; // Cyclustijd in microseconden (bijv. 10000 = 10ms)
  int threshold;           // Drempelwaarde voor zwart/wit detectie
  float kp;                // Proportionele versterking
  float ki;                // Integrale versterking
  float kd;                // Derivatieve versterking (rem-actie)
  int baseSpeed;           // Basissnelheid van de motoren (0 - 100)
} params;

// Functie om tekst naar de iPhone te sturen via BLE
void sendToPhone(String message) {
  if (deviceConnected) {
    pTxCharacteristic->setValue(message.c_str());
    pTxCharacteristic->notify();
    delay(10); 
  }
}

// Voorwaartse declaratie van commandoverwerking
void processCommand(String cmd);

// --------------------------- BLE CALLBACKS ---------------------------
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      pServer->getAdvertising()->start(); 
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue();
      if (rxValue.length() > 0) {
        processCommand(rxValue);
      }
    }
};

// --------------------------- COMMANDO VERWERKING (set & debug) ---------------------------
void processCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.print(">>> Ontvangen via BLE: [");
  Serial.print(cmd);
  Serial.println("]");

  if (cmd.startsWith("set ")) {
    int firstSpace = cmd.indexOf(' ', 4);
    if (firstSpace != -1) {
      String param = cmd.substring(4, firstSpace);
      String valueStr = cmd.substring(firstSpace + 1);
      
      if (param == "cycle") {
        params.cycleTime = valueStr.toInt();
      } else if (param == "threshold") {
        params.threshold = valueStr.toInt();
      } else if (param == "kp") {
        params.kp = valueStr.toFloat();
      } else if (param == "ki") {
        params.ki = valueStr.toFloat();
      } else if (param == "kd") {
        params.kd = valueStr.toFloat();
      } else if (param == "speed") {
        params.baseSpeed = valueStr.toInt();
      }
      
      EEPROM_writeAnything(0, params);
      sendToPhone("Parameter opgeslagen!\n");
      
      Serial.println("--- Parameter Gewijzigd via BLE ---");
      Serial.print("Kp: "); Serial.println(params.kp);
      Serial.print("Ki: "); Serial.println(params.ki);
      Serial.print("Kd: "); Serial.println(params.kd);
      Serial.print("Base Speed: "); Serial.println(params.baseSpeed);
      Serial.print("Threshold: "); Serial.println(params.threshold);
      Serial.print("Cycle Time: "); Serial.println(params.cycleTime);
      Serial.println("-----------------------------------");
    }
  } 
  else if (cmd == "debug") {
    sendToPhone("--- DEBUG INFO ---\n");
    sendToPhone("Status: " + String(robotRunning ? "RUNNING" : "STOPPED") + "\n");
    sendToPhone("Cycle time: " + String(params.cycleTime) + "\n");
    sendToPhone("Threshold: " + String(params.threshold) + "\n");
    sendToPhone("Kp: " + String(params.kp) + "\n");
    sendToPhone("Ki: " + String(params.ki) + "\n");
    sendToPhone("Kd: " + String(params.kd) + "\n");
    sendToPhone("Base Speed: " + String(params.baseSpeed) + "\n");
    
    String sensors = "Sensors: ";
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
      sensors += String(sensorValues[i]) + "\t";
    }
    sendToPhone(sensors + "\n");
    
    String pattern = "Pattern: ";
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
      pattern += (sensorBlack[i] ? "#" : ".");
    }
    sendToPhone(pattern + "\n");
    sendToPhone("Position: " + String(currentPosition) + "\n");
    sendToPhone("Calc time (us): " + String(calculationTime) + "\n");
    calculationTime = 0;
  } else {
    sendToPhone("Onbekend commando: " + cmd + "\n");
  }
}

// --------------------------- SENSOR FUNCTIE (ZONDER INVERSIE, ZWART = HOOG) ---------------------------
int readSensors() {
  long weightedSub = 0;
  int blackCount = 0;

  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    sensorValues[i] = analogRead(SENSOR_PINS[i]);
    
    // Zwart is actief als de waarde HOGER is dan de drempelwaarde
    bool black = sensorValues[i] > params.threshold;

    sensorBlack[i] = black;
    if (black) {
      weightedSub += (long)i * 1000; 
      blackCount++;
    }
  }

  if (blackCount == 0) return -1; 
  return weightedSub / blackCount;
}

// --------------------------- MOTOR FUNCTIE (ALLEEN VOORWAARTS / AFREMMEN) ---------------------------
void setMotorSpeed(int leftSpeed, int rightSpeed) {
  // Beperk de snelheid tussen 0 en 100 (geen negatieve waardes / achteruit)
  leftSpeed = constrain(leftSpeed, 0, 100);
  rightSpeed = constrain(rightSpeed, 0, 100);

  // Schaal 0-100% naar PWM 0-230 (~90% max vermogen om 6V motoren te beschermen op 7.4V)
  int pwmLeft = map(leftSpeed, 0, 100, 0, 230);
  int pwmRight = map(rightSpeed, 0, 100, 0, 230);

  // Linkermotor aansturen (altijd voorwaarts, PWM bepaalt de snelheid of remt af naar 0)
  analogWrite(AIN1, pwmLeft);
  analogWrite(AIN2, 0);

  // Rechtermotor aansturen (altijd voorwaarts, PWM bepaalt de snelheid of remt af naar 0)
  analogWrite(BIN1, pwmRight);
  analogWrite(BIN2, 0);
}

// --------------------------- SETUP ---------------------------
void setup()
{
  Serial.begin(115200);
  delay(1000); 

  EEPROM_readAnything(0, params);

  // Veilige defaultwaarden inclusief drempelwaarde ingesteld op 3000 voor zwarte lijn
  if (params.cycleTime == 0 || params.cycleTime > 100000) params.cycleTime = 10000; 
  if (params.threshold <= 0 || params.threshold > 4095) params.threshold = 3000;  
  if (params.kp <= 0) params.kp = 0.05;         
  if (params.ki < 0)  params.ki = 0.0;         
  if (params.kd < 0)  params.kd = 0.0;         
  if (params.baseSpeed <= 0) params.baseSpeed = 75;    

  Serial.println("=========================================");
  Serial.println("--- Opgeslagen EEPROM Waarden ---");
  Serial.print("Cycle Time (us): "); Serial.println(params.cycleTime);
  Serial.print("Threshold:        "); Serial.println(params.threshold);
  Serial.print("Kp:               "); Serial.println(params.kp);
  Serial.print("Ki:               "); Serial.println(params.ki);
  Serial.print("Kd:               "); Serial.println(params.kd);
  Serial.print("Base Speed (%):   "); Serial.println(params.baseSpeed);
  Serial.println("=========================================");

  analogReadResolution(12); 
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    pinMode(SENSOR_PINS[i], INPUT);
    analogSetPinAttenuation(SENSOR_PINS[i], ADC_11db);
  }

  // IR-pin (GPIO 27) instellen als uitgang en hoog zetten om de IR-leds in te schakelen
  pinMode(IR_PIN, OUTPUT);
  digitalWrite(IR_PIN, HIGH);

  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP); // Start/stop knop met interne pull-up

  // Initialisatie Bluetooth
  BLEDevice::init("LinefollowerArno"); 
  
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_RX,
                      BLECharacteristic::PROPERTY_WRITE
                    );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  pServer->getAdvertising()->start();
  
  Serial.println("Bluetooth is actief! Wachten op verbinding...");
}

// --------------------------- LOOP (Cyclische aansturing met Start/Stop knop) ---------------------------
void loop()
{
  // 1. Monostabiele knop uitlezen met debouncing voor toggle start/stop
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    static bool buttonWasPressed = false;
    if (reading == LOW && !buttonWasPressed) {
      buttonWasPressed = true;
      robotRunning = !robotRunning; 
      if (robotRunning) {
        Serial.println(">>> ROBOT GESTART via knop <<<");
      } else {
        Serial.println(">>> ROBOT GESTOPT via knop <<<");
        setMotorSpeed(0, 0); 
      }
    } else if (reading == HIGH) {
      buttonWasPressed = false;
    }
  }
  lastButtonState = reading;

  // 2. Als de robot gestopt is, doen we niks met de PID regeling
  if (!robotRunning) {
    setMotorSpeed(0, 0);
    integral = 0;
    lastError = 0;
    return;
  }

  // 3. Normale PID cyclus als de robot aan staat
  unsigned long current = micros();
  if (current - previous >= params.cycleTime)
  {
    previous = current;

    currentPosition = readSensors();

    if (currentPosition == -1) {
      integral = 0;
      lastError = 0;
      setMotorSpeed(0, 0);
    } 
    else 
    {
      int targetPosition = 2500;
      int error = currentPosition - targetPosition;

      float pTerm = error * params.kp;

      integral += error;
      integral = constrain(integral, -1000, 1000); 
      float iTerm = integral * params.ki;

      float derivative = error - lastError;
      float dTerm = derivative * params.kd;

      lastError = error;

      int correction = pTerm + iTerm + dTerm;

      // CORRECTE CORRECTIE: Als de linkersensor aangaat, corrigeert het rechterwiel (en vice versa)
      int leftSpeed  = params.baseSpeed + correction;
      int rightSpeed = params.baseSpeed - correction;

      setMotorSpeed(leftSpeed, rightSpeed);
    }
  }

  unsigned long difference = micros() - current;
  if (difference > calculationTime) calculationTime = difference;
}