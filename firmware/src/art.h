// Cover: JPEG laden, dekodieren, auf ART × ART verkleinern und rund freistellen (schwarz außerhalb).
// Läuft in einer eigenen Aufgabe; die Oberfläche fragt nur die Version ab.
#pragma once
#include <Arduino.h>

namespace art {

static const int SIZE = 220;

void begin();
void request(const char* url);              // leer = Cover entfernen
const uint16_t* image(uint32_t* version);   // RGB565, nullptr = keins

}  // namespace art
