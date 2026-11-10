// Regeln der Eltern: Eltern-PIN, Aufstehen und Bettzeit, Einschlafhören, Lautstärke-Grenze und die Freigabeliste
// (welche Radiosender und Audiothek-Sendungen bzw. -Folgen es auf der Uhr überhaupt gibt).
#pragma once
#include <Arduino.h>
#include "music.h"

namespace kids {

struct Cfg {
  uint16_t wake, bed;    // Minuten nach Mitternacht (Aufstehen, Bettzeit)
  uint8_t storyMin;      // so lange darf nach der Bettzeit noch etwas zum Einschlafen laufen
  uint8_t maxVol;        // %
  char pin[9];           // Eltern-PIN (4–8 Ziffern, "" = noch keine)
};

void begin();
const Cfg& cfg();
void set(const Cfg& c);
bool hasPin();
bool pinOk(const char* pin);

// Schlafenszeit: von der Bettzeit bis zum Aufstehen. Die ersten storyMin Minuten darf noch etwas zum
// Einschlafen laufen, danach ist Hören gesperrt.
bool night();
bool musicLocked();      // nachts nach der Einschlafzeit
int storyLeftS();        // Sekunden Einschlafzeit, die noch übrig sind (0 = keine)
int limitVolume(int pct);
void poll();             // aus der Hauptschleife: zur Einschlafzeit ausblenden

// Freigaben: "radio:<Adresse>" (Sender), "ard:set:<id>" (ganze Sendung), "ard:a:<MP3>" (einzelne Folge)
static const int RELEASE_MAX = 40;
bool released(const char* path);
void release(const char* path, const char* title, const char* sub, bool on);
int list(music::Item* out, int max, const char* prefix = nullptr);  // prefix: nur diese Art, nullptr = alle

}  // namespace kids
