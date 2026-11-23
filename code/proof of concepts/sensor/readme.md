# Sensoren proof of concept

minimale hard- en software die aantoont dat minimaal 6 sensoren onafhankelijk van elkaar kunnen uitgelezen worden (geen calibratie, normalisatie of interpolatie). Hierbij moet een zo groot mogelijk bereik van de AD converter benut worden (indien van toepassing)

### Hardware
* ESP32
* 8-kanaals IR tracking sensor module (6 kanalen gebruikt)

### Aansluitingen (links -> rechts)
| sensor | ESP32 |
|--------|-------|
| S1 | GPIO 36 |
| S2 | GPIO 39 |
| S3 | GPIO 34 |
| S4 | GPIO 35 |
| S5 | GPIO 32 |
| S6 | GPIO 33 |

Alle 6 pinnen zitten op ADC1, zodat ze ook werken terwijl Bluetooth/WiFi actief is.

### Schema
nog toe te voegen

### Code
ESP32_sensoren_kalibratie.ino

### Bereik van de AD converter
* resolutie 12 bit: analogReadResolution(12) geeft 0 - 4095
* demping 11 dB per pin: analogSetPinAttenuation(pin, ADC_11db) zodat het volledige ingangsbereik gebruikt wordt
* elke waarde is het gemiddelde van 8 metingen (minder ruis)

### Stappenplan
1. Upload ESP32_sensoren_kalibratie.ino naar de ESP32.
2. Open de seriële monitor: 115200 baud.
3. Zonder kalibratie toont de monitor elke 300 ms de ruwe waarde van elke sensor (S1 tot S6).
4. Beweeg een zwarte lijn onder de sensoren: enkel de waarde van de sensor boven de lijn verandert, de andere blijven gelijk (onafhankelijk uitgelezen).

### Opmerking
De sketch heeft ook een optionele kalibratie (w = wit, b = zwart, r = reset, s = toon). Voor deze proof of concept zijn enkel de ruwe waarden nodig: stuur r om terug ruwe waarden te tonen.
