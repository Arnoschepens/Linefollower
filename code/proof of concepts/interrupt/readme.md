# start/stop interrupt proof of concept
minimale hard- en software die de correcte werking van een start/stop drukknop aantoont, gebruik makend van een hardware interrupt

### Hardware
* ESP32
* drukknop tussen GPIO 23 en GND (interne pull-up: los = HIGH, ingedrukt = LOW)
* LED met voorschakelweerstand op GPIO 27 naar GND

### Schema
nog toe te voegen

### Code
start_stop_interrupt/start_stop_interrupt.ino

### Werking
* De knop hangt aan een hardware-interrupt op de dalende flank (attachInterrupt, FALLING).
* De interrupt service routine onthoudt enkel dat er gedrukt werd.
* De loop controleert na 20 ms of de knop nog ingedrukt is (debounce) en wisselt dan tussen START en STOP.
* Een nieuwe druk telt pas als de knop eerst 50 ms losgelaten is (dender bij het loslaten wordt genegeerd).

### Stappenplan
1. Upload start_stop_interrupt.ino naar de ESP32.
2. Open de seriële monitor: 115200 baud.
3. Druk op de knop: de LED gaat aan en de monitor toont START (druk 1).
4. Druk opnieuw: de LED gaat uit en de monitor toont STOP (druk 2).
5. Druk een aantal keer snel na elkaar: elke druk wordt precies één keer geteld.
