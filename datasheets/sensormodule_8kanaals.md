# 8-kanaals IR tracking sensormodule (pinout)

Geen officiële datasheet gevonden. Gegevens uit de bestelling en uit het elektrisch schema van dit project.

| eigenschap | waarde |
|------------|--------|
| type | 8-kanaals IR tracking sensor module (analoge uitgangen D1 tot D8) |
| reflectiesensoren | type nog na te kijken op de module (vaak TCRT5000, zie [tcrt5000.pdf](tcrt5000.pdf)) |
| voeding in dit project | 3,3 V van de ESP32 |
| bron | AliExpress, YX Electronic Components: "8 Kanaals IR Tracking Sensor Module 8 CH Infrarood Lijn Track Trace Detecteren Detectie Board" |

### Pinvolgorde (volgens het elektrisch schema)
| pin | functie | aangesloten op |
|-----|---------|----------------|
| VCC | voeding | 3,3 V ESP32 |
| IR | IR-leds aan/uit | 3,3 V ESP32 (samen met VCC) |
| D1 | sensor 1 | niet aangesloten |
| D2 | sensor 2 | GPIO 36 |
| D3 | sensor 3 | GPIO 39 |
| D4 | sensor 4 | GPIO 34 |
| D5 | sensor 5 | GPIO 35 |
| D6 | sensor 6 | GPIO 32 |
| D7 | sensor 7 | GPIO 33 |
| D8 | sensor 8 | niet aangesloten |
| GND | massa | GND ESP32 |
