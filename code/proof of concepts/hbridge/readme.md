# H-Bridge proof of concept

minimale hard- & software + stappenplan dat aantoont dat 2 motoren onafhankelijk van elkaar kunnen draaien, en (traploos) regelbaar zijn in snelheid en draairichting.

### Hardware
* ESP32
* TB6612FNG motordriver (H-brug)
* 2 x N20 motor 6V
* batterij 7,4V op de H-brug (motoren max 6V: in de software is 100% = 81% duty = 6V)

### Aansluitingen
| ESP32 / voeding | TB6612FNG | motor |
|-----------------|-----------|-------|
| GPIO 17 | AIN1 | rechts (AOUT1/AOUT2) |
| GPIO 18 | AIN2 | rechts (AOUT1/AOUT2) |
| GPIO 22 | BIN1 | links (BOUT1/BOUT2) |
| GPIO 21 | BIN2 | links (BOUT1/BOUT2) |
| 3,3 V van de ESP32 | STBY | - |
| GND van de ESP32 | GND (logica) | - |
| batterij 7,4 V | VM | - |
| 0 V batterij | GND (motor) | - |

Elke motor krijgt 2 PWM-signalen (20 kHz): PWM op IN1 = vooruit, PWM op IN2 = achteruit.

### Schema
nog toe te voegen

### Code
H-brug_aansturing/H-brug_aansturing.ino (met SerialCommand.h en SerialCommand.cpp in dezelfde map)

### Stappenplan
1. Upload H-brug_aansturing.ino naar de ESP32.
2. Open de seriële monitor: 115200 baud, regeleinde "Both NL & CR".
3. Typ **links 50**: enkel de linkermotor draait vooruit, de rechtermotor staat stil.
4. Typ **rechts 50**: de rechtermotor draait ook vooruit (beide motoren onafhankelijk aangestuurd).
5. Typ **links 20**, **links 60**, **links 100**: de snelheid verandert traploos (elke waarde van 0 tot 100 kan).
6. Typ **links -50** en **rechts -50**: beide motoren draaien achteruit (draairichting).
7. Typ **stop**: beide motoren stoppen.
8. Optioneel: **test** laat beide motoren van 10% naar 100% oplopen in stappen van 10% (elk 1,5 s).

### Aandachtspunten
* 100% sturing = 6V op de motor (bij 7,4V batterij).
* Uitgang A van de driver zit aan het rechterwiel, uitgang B aan het linkerwiel.
