# ESP32-bord (gebruikte pinnen)

Datasheet van de chip: [esp32_datasheet_en.pdf](esp32_datasheet_en.pdf) (Espressif).

| eigenschap | waarde |
|------------|--------|
| bord | ESP32 development board, 30 pinnen (exacte variant nog na te kijken op het bord) |
| bron | AliExpress, Shop1104661756 Store: "ESP32 Development Board Draadloze WiFi Bluetooth-module ... NodeMCU-32S 32 32D 32U 30Pin Board" |
| voeding in dit project | 5 V op VIN, van de converter (7,4 V naar 5 V) via de aan/uit-schakelaar |
| logica | 3,3 V |

### Gebruikte pinnen (volgens de code en het elektrisch schema)
| pin | richting | functie |
|-----|----------|---------|
| VIN | ingang | 5 V van de converter |
| GND | - | massa |
| 3,3V | uitgang | voeding sensormodule (VCC en IR) |
| GPIO 17 | uitgang (PWM) | DRV8833 AIN1 |
| GPIO 18 | uitgang (PWM) | DRV8833 AIN2 |
| GPIO 22 | uitgang (PWM) | DRV8833 BIN1 |
| GPIO 21 | uitgang (PWM) | DRV8833 BIN2 |
| GPIO 23 | ingang (pull-up, interrupt) | start/stop-knop naar GND |
| GPIO 27 | uitgang | LED via 220 Ω naar GND |
| GPIO 36 | ingang (ADC1) | sensormodule D2 |
| GPIO 39 | ingang (ADC1) | sensormodule D3 |
| GPIO 34 | ingang (ADC1) | sensormodule D4 |
| GPIO 35 | ingang (ADC1) | sensormodule D5 |
| GPIO 32 | ingang (ADC1) | sensormodule D6 |
| GPIO 33 | ingang (ADC1) | sensormodule D7 |

De sensoren zitten allemaal op ADC1. GPIO 34, 35, 36 en 39 zijn enkel ingangen.
