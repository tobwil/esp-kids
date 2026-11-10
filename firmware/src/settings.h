// Dauerhafte Einstellungen (NVS) und die Liste bekannter WLANs.
#pragma once
#include <Arduino.h>

struct Settings {
  uint8_t volume;        // %
  uint8_t brightness;    // %
  uint16_t screenOff;    // Sekunden, 0 = nie
  char name[17];         // Name der Uhr (Weboberfläche, Sicherung)
  // Standort für das Wetter (automatisch per IP, 0/0 = unbekannt)
  float lat, lon;
  char city[32];
};
extern Settings cfg;

static const int KNOWN_MAX = 6;
struct KnownNet { char ssid[33]; char pass[65]; };
extern KnownNet known[KNOWN_MAX];
extern int knownCount;

void settingsLoad();
void settingsSave();
int knownIndex(const char* ssid);
const char* knownPass(const char* ssid);
void knownAdd(const char* ssid, const char* pass);
void knownForget(const char* ssid);
