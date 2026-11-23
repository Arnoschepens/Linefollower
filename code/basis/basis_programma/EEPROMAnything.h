#pragma once

#include <EEPROM.h>
#include <Arduino.h>  // for type definitions

/*
  Zelfde bibliotheek als je origineel, maar geschikt voor de ESP32:
  de ESP32 schrijft pas echt naar het geheugen na EEPROM.commit().
  (EEPROM.begin(grootte) moet in setup() aangeroepen worden.)
*/

template <class T> int EEPROM_writeAnything(int ee, const T& value)
{
    const byte* p = (const byte*)(const void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
          EEPROM.write(ee++, *p++);
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32) || defined(ESP8266)
    EEPROM.commit();
#endif
    return i;
}

template <class T> int EEPROM_readAnything(int ee, T& value)
{
    byte* p = (byte*)(void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
          *p++ = EEPROM.read(ee++);
    return i;
}

template <class T> int EEPROM_resetAnything(int ee, const T& value)
{
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
          EEPROM.write(ee++, 0);
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32) || defined(ESP8266)
    EEPROM.commit();
#endif
    return i;
}
