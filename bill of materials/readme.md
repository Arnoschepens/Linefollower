## Bill of materials
<br />

| nr | naam | omschrijving | nieuw/recup | kostprijs/stuk (€) | aantal | subtotaal (€) |
|---:|------|--------------|-------------|-------------------:|-------:|--------------:|
| 1 | ESP32 | microcontroller | nieuw | 11,76 | 1 | 11,76 |
| 2 | prototype board | dual sided 90x70 mm board | nieuw | 10,22 | 1 | 10,22 |
| 3 | batterij | lithium-ion batterij | nieuw | 19,31 | 1 | 19,31 |
| 4 | H-brug | TB6612FNG dual motor driver 1A | nieuw | 5,43 | 1 | 5,43 |
| 5 | sensor | 8-kanaals IR tracking sensor module | recup | 7,96 | 1 | 7,96 |
| 6 | motor | N20 metal gear motor 6V 1000RPM | nieuw | 7,51 | 2 | 15,01 |
| 7 | wielen | N20 rubber wiel 34 mm | nieuw | 3,67 | 2 | 7,33 |
| 8 | motorhouders | N20 motor mount (set van 5) | nieuw | 5,80 | 1 | 5,80 |
| 9 | drukknop | start/stop-knop | recup | - | 1 | - |
| 10 | LED | indicatie-LED | recup | - | 1 | - |
| 11 | elektrische draad | draad 1,5 | recup | - | - | - |
| 12 | converter | 7,4 V naar 5 V, voeding van de ESP32 via VIN | - | - | 1 | - |
| 13 | schakelaar | aan/uit-schakelaar (toevoer naar de ESP32) | - | - | 1 | - |
| 14 | weerstand | 220 Ω, voorschakelweerstand van de LED | - | - | 1 | - |
| | **totaal** | | | | | **82,82** |

### Voeding (volgens het elektrisch schema)
* batterij 7,4 V -> VM van de TB6612FNG (motoren) en ingang van de converter
* converter 7,4 V -> 5 V -> aan/uit-schakelaar -> VIN van de ESP32
* 3,3 V van de ESP32 -> sensormodule (VCC en IR) en STBY van de TB6612FNG
* motoren max 6 V: in de code is 100% sturing = 81% duty (6 V bij 7,4 V)

### Nog aan te vullen
* exact type van het ESP32-bord en van de batterij (spanning, capaciteit)
* lader voor de Li-ion-batterij
* prijzen van de converter, de schakelaar en de weerstand
