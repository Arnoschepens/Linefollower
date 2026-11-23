# Finale code

stap4/stap4.ino: volledige PID-lijnvolger met draadloze bediening via BLE (met SerialCommand.h, SerialCommand.cpp en EEPROMAnything.h in dezelfde map). Compileert met de ESP32 Arduino-core 3.x.

### Hardware
| onderdeel | ESP32 |
|-----------|-------|
| sensoren S1 tot S6 (links -> rechts) | GPIO 36, 39, 34, 35, 32, 33 |
| TB6612FNG AIN1 / AIN2 (rechtermotor) | GPIO 17 / 18 |
| TB6612FNG BIN1 / BIN2 (linkermotor) | GPIO 22 / 21 |
| start/stop-knop naar GND (interrupt, FALLING) | GPIO 23 |
| LED (knippert = stil, brandt = rijden) | GPIO 27 |

### Werking
* elke cyclus: sensoren lezen, normaliseren met de kalibratie (zwart = 0, wit = 1000), positie van de lijn bepalen met kwadratische interpolatie (in mm)
* PID-regelaar: error = -positie, P + I + D (Ki en Kd worden herrekend met de cyclustijd), output begrensd op +-510
* output verdeeld over beide motoren met de parameter diff; een motor draait nooit achteruit
* motorspanning begrensd: 100% sturing = 6 V bij een batterij van 7,4 V
* lijn kwijt: de robot stuurt nog losttime ms verder en stopt dan
* alle parameters worden bewaard in het EEPROM

### Commando's (seriële monitor 115200 baud "Both NL & CR", of de app BLESerial nRF -> linefollowerArno)
| commando | werking |
|----------|---------|
| start / stop | rijden starten of stoppen (zelfde als de knop) |
| calibrate black / calibrate white | kalibratie zwart / wit |
| set power <0-100> | basissnelheid in procent |
| set kp / set ki / set kd <getal> | regelaar |
| set diff <0-1> | verdeling van de output over de wielen |
| set cycle <us> | cyclustijd |
| set lost <100-1000> | grens voor geen lijn (standaard 900) |
| set losttime <ms> | zo lang verder sturen zonder lijn (standaard 100) |
| set debug on / off | elke 100 ms positie, output, P, I, D en motoren |
| live on / off | sensorwaarden en positie (alleen bij stilstand) |
| debug | alle parameters en waarden |
| motor left / right <-100..100>, motor stop | motoren apart aansturen |
