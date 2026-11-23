/*
  H-BRUG AANSTURING: motortest (overgenomen uit finalepoging3/stap4)
  Seriële monitor: 115200 baud, "Both NL & CR"

  Commando's:
    links <-100..100>             linkermotor apart aansturen (negatief = achteruit, 0 = stop)
    rechts <-100..100>            rechtermotor apart aansturen (negatief = achteruit, 0 = stop)
    test                          motortest 10% -> 100% (stappen van 10%, elk 1,5 s); nogmaals 'test' = stop
    motor left|right <-100..100>  één motor apart aansturen (negatief = achteruit)
    motor stop                    beide motoren uit
    stop                          alles stoppen

  Beperking motorspanning: H-brug op 7,4 V, motoren max 6 V -> 100% sturing = 81% duty.
*/

#include "SerialCommand.h"

#define Baudrate 115200

SerialCommand sCmd(Serial);

// ---------- hardware ----------
const int AIN1 = 17, AIN2 = 18, BIN1 = 22, BIN2 = 21;        // H-brug-uitgangen A en B
// Motor-uitgang A zit op jouw robot aan het RECHTERwiel en B aan het LINKERwiel (getest met 'motor left').
// Daarom: 'links' (index 0) gebruikt de B-pinnen en 'rechts' (index 1) de A-pinnen.
const int MOTOR_PINS[4] = {BIN1, BIN2, AIN1, AIN2};   // links, rechts
const bool MOTOR_A_INVERT = false;
const bool MOTOR_B_INVERT = false;

// Spanningsbegrenzing: 100% sturing = 6 V = 6/7,4 = 81% duty
const float BATTERY_VOLTAGE = 7.4;
const float MOTOR_MAX_VOLTAGE = 6.0;
const int MAX_PWM = (int)(255.0 * MOTOR_MAX_VOLTAGE / BATTERY_VOLTAGE);

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
}

// ---------- motortest (10% -> 100%) ----------
const unsigned long TEST_STEP_MS = 1500;
int testStep = 0;
unsigned long testStepStart = 0;

void startTest()
{
  motorsOff();
  testStep = 1;
  testStepStart = millis();
  setMotor(0, 10);
  setMotor(1, 10);
  Serial.println("motortest start: 10%");
}

void stopTest()
{
  testStep = 0;
  motorsOff();
  Serial.println("motortest stop");
}

void updateTest()
{
  if (testStep > 0 && millis() - testStepStart >= TEST_STEP_MS)
  {
    if (testStep >= 10) { stopTest(); return; }
    testStep++;
    testStepStart = millis();
    setMotor(0, testStep * 10);
    setMotor(1, testStep * 10);
    Serial.print("motortest: ");
    Serial.print(testStep * 10);
    Serial.println("%");
  }
}

// ---------- setup ----------
void setup()
{
  Serial.begin(Baudrate);

  sCmd.addCommand("links", onLinks);
  sCmd.addCommand("rechts", onRechts);
  sCmd.addCommand("test", onTest);
  sCmd.addCommand("motor", onMotor);
  sCmd.addCommand("stop", onStop);
  sCmd.setDefaultHandler(onUnknownCommand);

  for (int i = 0; i < 4; i++) { pinMode(MOTOR_PINS[i], OUTPUT); PWM_INIT(i, MOTOR_PINS[i]); }
  motorsOff();

  Serial.println("ready: typ 'links 50', 'rechts -50', 'test' of 'stop'");
}

// ---------- loop ----------
void loop()
{
  sCmd.readSerial();
  updateTest();
}

// ---------- commando's ----------
void onUnknownCommand(char *command)
{
  Serial.print("unknown command: \"");
  Serial.print(command);
  Serial.println("\"");
}

// links <-100..100> / rechts <-100..100>: één motor apart, de andere blijft zoals hij was
void driveOne(int motor, const char *name)
{
  char* value = sCmd.next();
  if (value == NULL)
  {
    Serial.print("geef ook een percentage, bv: ");
    Serial.print(name);
    Serial.println(" 50  (of -50 voor achteruit)");
    return;
  }

  if (testStep > 0) { testStep = 0; motorsOff(); }   // test onderbreken
  int percent = constrain(atoi(value), -100, 100);
  setMotor(motor, percent);

  Serial.print(name);
  Serial.print(": ");
  Serial.print(percent);
  Serial.println(percent < 0 ? "% (achteruit)" : (percent == 0 ? "% (stop)" : "% (vooruit)"));
}

void onLinks()  { driveOne(0, "links"); }
void onRechts() { driveOne(1, "rechts"); }

void onTest()
{
  if (testStep > 0) stopTest(); else startTest();
}

void onStop()
{
  if (testStep > 0) stopTest();
  else { motorsOff(); Serial.println("motoren stop"); }
}

void onMotor()
{
  char* which = sCmd.next();
  char* value = sCmd.next();
  if (which == NULL) { Serial.println("gebruik: motor left|right <-100..100> of motor stop"); return; }

  testStep = 0;
  if (strcmp(which, "stop") == 0) { motorsOff(); Serial.println("motoren stop"); return; }
  if (value == NULL) { Serial.println("geef ook een percentage, bv: motor left 50"); return; }

  if (strcmp(which, "left") == 0) setMotor(0, atoi(value));
  else if (strcmp(which, "right") == 0) setMotor(1, atoi(value));
  else Serial.println("gebruik: motor left|right <-100..100>");
}
