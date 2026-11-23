/*
  START/STOP INTERRUPT: proof of concept
  Toont de correcte werking van een start/stop-drukknop met een hardware-interrupt.

  Hardware (zelfde pinnen als de linefollower):
    drukknop tussen GPIO 23 en GND (interne pull-up: los = HIGH, ingedrukt = LOW)
    LED (met weerstand) op GPIO 27 naar GND

  Werking:
    - Elke druk op de knop wisselt tussen START en STOP.
    - LED brandt = gestart, LED uit = gestopt.
    - Seriële monitor (115200 baud) toont elke wissel en het aantal drukken.

  De ISR (interrupt service routine) wordt opgeroepen op de dalende flank (knop ingedrukt)
  en onthoudt enkel dat er gedrukt werd. De loop controleert na 20 ms of de knop nog
  ingedrukt is (debounce) en wisselt dan de toestand. Dender bij het loslaten wordt genegeerd:
  de knop moet eerst 50 ms los zijn voor een nieuwe druk telt.
*/

const int START_PIN = 23;     // knop naar GND (INPUT_PULLUP)
const int LED_PIN = 27;

const unsigned long BUTTON_DEBOUNCE_MS = 20;   // zo lang moet de knop ingedrukt blijven na de flank
const unsigned long BUTTON_RELEASE_MS = 50;    // zo lang moet de knop los zijn voor een nieuwe druk

volatile bool buttonEvent = false;             // gezet door de ISR
volatile unsigned long buttonEventTime = 0;
bool buttonReleased = true;                    // knop is losgelaten sinds de vorige druk
unsigned long buttonHighSince = 0;

bool running = false;
unsigned long pressCount = 0;

// Interrupt service routine: zo kort mogelijk, enkel de flank onthouden
void IRAM_ATTR onButtonInterrupt()
{
  if (!buttonEvent)
  {
    buttonEvent = true;
    buttonEventTime = millis();
  }
}

void setup()
{
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(START_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(START_PIN), onButtonInterrupt, FALLING);   // knop naar GND = dalende flank

  Serial.println("ready: druk op de knop (GPIO 23) om te starten/stoppen");
}

void loop()
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
      running = !running;
      pressCount++;

      digitalWrite(LED_PIN, running ? HIGH : LOW);
      Serial.print(running ? "START" : "STOP");
      Serial.print("  (druk ");
      Serial.print(pressCount);
      Serial.println(")");
    }
  }
}
