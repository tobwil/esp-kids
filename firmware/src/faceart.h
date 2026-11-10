// Zifferblatt als 8-Bit-Bild: große Uhrzeit und Wochentag über einer Wetterszene – Sonne, Mond und Sterne, Wolken,
// Regen, Gewitter, Schnee oder Nebel, je nach aktuellem Wetter und Tageszeit. Auf der Wiese die Temperatur.
// Reines Zeichnen in ein Raster (ohne LVGL und Arduino), damit sich alle Wetter auch am Rechner als Vorschau
// rendern lassen (tests/face_preview.cpp).
#pragma once
#include <stdint.h>

namespace faceart {

constexpr int S = 116;  // Raster in Pixeln (auf der Uhr ×4)
constexpr uint16_t rgb(uint32_t c) { return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F); }

enum Sky : uint8_t { CLEAR, PARTLY, CLOUDY, FOG, DRIZZLE, RAIN, SNOW, STORM, SKIES };
Sky skyOf(int wmo);              // WMO-Wettercode (Open-Meteo) → Szene
bool heavyOf(int wmo);           // starker Regen bzw. Schnee

struct Scene {
  bool weather;       // Wetter bekannt (sonst: heiter, ohne Temperatur)
  Sky sky;
  bool heavy;
  bool day;           // Sonne ist auf
  int temp;           // °C, gerundet
  bool timeValid;
  int hour, minute;
  int wday;           // 0 = Sonntag
  int mday, month;    // 1 … 31, 1 … 12
  int battery;        // %, −1 = kein Akku
  bool charging;
  bool playing;       // Musik läuft
  int frame;          // Animationsschritt
};

void draw(uint16_t* px, const Scene& s);  // füllt das ganze Raster S × S (außerhalb des Kreises schwarz)

}  // namespace faceart
