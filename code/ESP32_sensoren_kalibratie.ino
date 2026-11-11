/*
  ESP32 - 6 sensoren uitlezen met kalibratie (wit / zwart)

  Pinnen (links -> rechts): 36, 39, 34, 35, 32, 33
  Seriële monitor: 115200 baud, stuur 1 letter:

    w = meet WIT    (houd alle sensoren boven wit, stuur 'w')
    b = meet ZWART  (houd alle sensoren boven zwart, stuur 'b')
    r = RESET       (wist de kalibratie, terug naar ruwe waarden)
    s = toon kalibratiewaarden

  Na w + b wordt elke sensor omgerekend naar een percentage:
    0 %   = wit
    100 % = zwart
  Dit werkt ook als jouw sensor omgekeerd werkt (zwart = lagere waarde).
  De kalibratie blijft bewaard na een herstart.
*/

#include <Preferences.h>

const uint8_t SENSOR_COUNT = 6;
const uint8_t SENSOR_PINS[SENSOR_COUNT] = {36, 39, 34, 35, 32, 33};

const unsigned long PRINT_INTERVAL_MS = 300;
const unsigned long CALIBRATION_TIME_MS = 2000;  // meetduur per kalibratiestap
const int MIN_DIFFERENCE = 300;                  // minimaal verschil wit/zwart

int whiteValue[SENSOR_COUNT];
int blackValue[SENSOR_COUNT];
bool hasWhite = false;
bool hasBlack = false;

Preferences prefs;
unsigned long lastPrint = 0;

// --------------------------- HULPFUNCTIES ---------------------------

// Gemiddelde van enkele metingen (minder ruis)
int readAveraged(uint8_t pin) {
  long sum = 0;
  for (uint8_t s = 0; s < 8; s++) sum += analogRead(pin);
  return sum / 8;
}

void printArray(const char *label, const int values[]) {
  Serial.print(label);
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    Serial.print(values[i]);
    Serial.print("\t");
  }
  Serial.println();
}

// Meet gedurende CALIBRATION_TIME_MS het gemiddelde van elke sensor
void measureAverage(int result[]) {
  long sum[SENSOR_COUNT] = {0};
  unsigned long samples = 0;
  unsigned long start = millis();

  while (millis() - start < CALIBRATION_TIME_MS) {
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
      sum[i] += analogRead(SENSOR_PINS[i]);
    }
    samples++;
    delay(2);
  }

  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    result[i] = (samples > 0) ? (int)(sum[i] / samples) : 0;
  }
}

// --------------------------- OPSLAG ---------------------------

void saveCalibration() {
  prefs.begin("sensorcal", false);
  prefs.putBool("valid", true);
  prefs.putBytes("white", whiteValue, sizeof(whiteValue));
  prefs.putBytes("black", blackValue, sizeof(blackValue));
  prefs.end();
}

void clearCalibration() {
  prefs.begin("sensorcal", false);
  prefs.clear();
  prefs.end();
}

bool loadCalibration() {
  prefs.begin("sensorcal", true);
  bool valid = prefs.getBool("valid", false);
  if (valid) {
    size_t w = prefs.getBytes("white", whiteValue, sizeof(whiteValue));
    size_t b = prefs.getBytes("black", blackValue, sizeof(blackValue));
    if (w != sizeof(whiteValue) || b != sizeof(blackValue)) valid = false;
  }
  prefs.end();
  return valid;
}

// --------------------------- KALIBRATIE ---------------------------

void checkCalibration() {
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    int diff = abs(blackValue[i] - whiteValue[i]);
    if (diff < MIN_DIFFERENCE) {
      Serial.print("WAARSCHUWING: sensor ");
      Serial.print(i + 1);
      Serial.print(" (GPIO");
      Serial.print(SENSOR_PINS[i]);
      Serial.print(") heeft te weinig verschil tussen wit en zwart (");
      Serial.print(diff);
      Serial.println("). Controleer afstand en aansluiting, of stuur 'r' en probeer opnieuw.");
    }
  }
}

void showCalibration() {
  Serial.println("---- Kalibratie ----");
  if (hasWhite) printArray("Wit:   ", whiteValue);
  else Serial.println("Wit:   (nog niet gemeten)");
  if (hasBlack) printArray("Zwart: ", blackValue);
  else Serial.println("Zwart: (nog niet gemeten)");
  Serial.println("--------------------");
}

void calibrate(bool white) {
  Serial.print("Meting ");
  Serial.print(white ? "WIT" : "ZWART");
  Serial.println(" gestart... houd de sensoren stil boven het oppervlak.");

  if (white) {
    measureAverage(whiteValue);
    hasWhite = true;
    printArray("Wit gemeten:   ", whiteValue);
  } else {
    measureAverage(blackValue);
    hasBlack = true;
    printArray("Zwart gemeten: ", blackValue);
  }

  if (hasWhite && hasBlack) {
    saveCalibration();
    Serial.println("Kalibratie voltooid en opgeslagen.");
    checkCalibration();
  } else {
    Serial.println(white ? "Stuur nu 'b' om zwart te meten."
                         : "Stuur nu 'w' om wit te meten.");
  }
}

void resetCalibration() {
  hasWhite = false;
  hasBlack = false;
  clearCalibration();
  Serial.println("RESET: kalibratie gewist. Er worden weer ruwe waarden getoond.");
  Serial.println("Stuur 'w' (wit) en 'b' (zwart) om opnieuw te kalibreren.");
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    switch (c) {
      case 'w': case 'W': calibrate(true);  break;
      case 'b': case 'B': calibrate(false); break;
      case 'r': case 'R': resetCalibration(); break;
      case 's': case 'S': showCalibration(); break;
      default: break;  // newline e.d. negeren
    }
  }
}

// --------------------------- SETUP ---------------------------

void setup() {
  Serial.begin(115200);
  delay(500);

  analogReadResolution(12);  // 0 - 4095
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    analogSetPinAttenuation(SENSOR_PINS[i], ADC_11db);
  }

  if (loadCalibration()) {
    hasWhite = true;
    hasBlack = true;
    Serial.println("Opgeslagen kalibratie geladen.");
    showCalibration();
  } else {
    Serial.println("Geen kalibratie gevonden: ruwe waarden worden getoond.");
  }

  Serial.println("Commando's: w = wit, b = zwart, r = reset, s = toon kalibratie");
}

// --------------------------- LOOP ---------------------------

void loop() {
  handleSerialCommands();

  if (millis() - lastPrint < PRINT_INTERVAL_MS) return;
  lastPrint = millis();

  bool calibrated = hasWhite && hasBlack;

  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    int raw = readAveraged(SENSOR_PINS[i]);

    Serial.print("S");
    Serial.print(i + 1);
    Serial.print("(");
    Serial.print(SENSOR_PINS[i]);
    Serial.print("): ");
    Serial.print(raw);

    if (calibrated) {
      int range = blackValue[i] - whiteValue[i];
      int percent = 0;
      if (range != 0) {
        percent = (int)((long)(raw - whiteValue[i]) * 100 / range);
      }
      percent = constrain(percent, 0, 100);

      Serial.print(" = ");
      Serial.print(percent);
      Serial.print("% ");
      Serial.print(percent >= 50 ? "#" : ".");
    }

    if (i < SENSOR_COUNT - 1) Serial.print("   |   ");
  }
  Serial.println();
}
