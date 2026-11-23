# draadloze communicatie proof of concept
minimale hard- en software waarmee aangetoond wordt dat duplex kan gecommuniceerd worden tussen de microcontroller en een smartphone, gebruik makend van de app BLESerial nRF (Bluetooth Low Energy, Nordic UART Service)
<br />

### configuratie
* ESP32 met de sketch draadloze_communicatie/draadloze_communicatie.ino (met SerialCommand.h en SerialCommand.cpp in dezelfde map)
* LED met voorschakelweerstand op GPIO 27 naar GND
* iPhone met de app BLESerial nRF (App Store)
* BLE-naam van de ESP32: linefollowerArno
* regeleinde in de app: CR, LF of CR+LF (alle drie werken)

### opmerkingen
* Alles wat de ESP32 naar de telefoon stuurt, verschijnt ook in de seriële monitor (115200 baud).
* De ESP32 stuurt tekst in stukjes van 20 bytes (standaard BLE MTU).
* Na het verbreken van de verbinding is de ESP32 meteen weer vindbaar.

### gebruiksaanwijzing
1. Upload draadloze_communicatie.ino naar de ESP32.
2. Open BLESerial nRF, zoek naar linefollowerArno en verbind.
3. De ESP32 stuurt: linefollowerArno verbonden. Typ 'ping'.
4. Stuur **ping**: de ESP32 antwoordt **pong** (telefoon -> ESP32 -> telefoon).
5. Stuur **led on** / **led off**: de LED gaat aan of uit en de ESP32 bevestigt.
6. Stuur een ander woord: de ESP32 antwoordt ontvangen: gevolgd door dat woord.
7. Elke 5 s stuurt de ESP32 zelf een bericht met de uptime (ESP32 -> telefoon zonder vraag).
