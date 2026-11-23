# basis programma

volgende zaken werken samen in dit template project  

* instelbare parameters mbv. SerialCommands 
  * cyclus tijd
  * debug on/off
  * start/stop
* parameters worden opgeslagen in eeprom geheugen
* start/stop kan ook geactiveerd worden mbv. een externe interrupt
* draadloze communicatie

Met behulp van de cyclus kan je bvb. een knipperende led programmeren.

### Code
basis_programma/basis_programma.ino (met SerialCommand.h, SerialCommand.cpp en EEPROMAnything.h in dezelfde map)

### Hardware
* ESP32
* drukknop tussen GPIO 23 en GND (interrupt op de dalende flank)
* LED met voorschakelweerstand op GPIO 27 naar GND

### Commando's (via seriële monitor 115200 baud "Both NL & CR", of via de app BLESerial nRF -> linefollowerArno)
| commando | werking |
|----------|---------|
| start | start: de LED knippert |
| stop | stop: de LED gaat uit |
| set cycle <us> | cyclustijd in microseconden (standaard 500000 = LED wisselt elke 0,5 s), wordt bewaard in EEPROM |
| set debug on / off | elke cyclus een regel met cyclusnummer, LED-toestand en rekentijd |
| debug | toont alle parameters en de maximale rekentijd |

De drukknop doet hetzelfde als start/stop. Na een herstart blijft de ingestelde cyclustijd behouden (EEPROM).

### Stappenplan
1. Upload basis_programma.ino naar de ESP32.
2. Typ **debug**: de cyclustijd is 500000 us.
3. Typ **start**: de LED knippert elke 0,5 s.
4. Typ **set cycle 100000**: de LED knippert sneller.
5. Typ **set debug on**: elke cyclus verschijnt een regel; **set debug off** stopt dit.
6. Druk op de knop: de LED stopt; nog eens drukken: de LED knippert weer.
7. Herstart de ESP32 en typ **debug**: de cyclustijd is nog steeds 100000 us (EEPROM).
8. Verbind met de telefoon (BLESerial nRF, linefollowerArno) en stuur dezelfde commando's.

### tussentijdse_finale_code.ino
* Compileert met de ESP32 Arduino-core **3.x**. Op core 2.x geeft BLE getValue() een std::string terug in plaats van een String, waardoor de regel String rxValue = pCharacteristic->getValue(); een type-fout geeft.
* Nodige bibliotheek in dezelfde map: EEPROMAnything.h (staat in code/basis). SerialCommand wordt in dit bestand niet gebruikt.
