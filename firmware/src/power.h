// Akku: Verlauf, Restlaufzeit, Warnungen, Sparmodus und Nachtruhe.
// Sparmodus: Display höchstens 40 % hell und nach 10 s aus, Tiefschlaf schon nach 3 Minuten.
// Nachtruhe: von Bettzeit + Einschlafzeit bis zum Aufstehen im Tiefschlaf.
#pragma once
#include <Arduino.h>

namespace pwr {

enum Mode : uint8_t { M_OFF = 0, M_AUTO = 1, M_ON = 2 };  // Auto: ab 20 % bis zum Laden

void begin();
void poll();                 // aus der Hauptschleife
void save();                 // Akku-Verlauf sichern (vor Tiefschlaf, Ausschalten, Neustart)
bool dozing();               // Display dunkel, nichts läuft: Prozessor im Leichtschlaf – Tasks dürfen seltener aufwachen
void lightSleepOnUsb(bool on);  // zum Testen: Leichtschlaf auch am Kabel (USB-Konsole fällt dann zeitweise aus)
Mode mode();
void setMode(Mode m);
bool saving();               // Sparmodus gerade aktiv
uint16_t screenOffS();       // Display-aus-Zeit unter Berücksichtigung des Sparmodus (0 = nie)
uint16_t deepSleepMin();     // wirksam (Sparmodus kürzt auf 3), 0 = nie
uint8_t deepSleepSetting();  // eingestellt: Minuten, 0 = nie (immer erreichbar)
void setDeepSleep(uint8_t min);
bool nightRestOn();
void setNightRest(bool on);
bool nightRest(uint32_t* leftS);  // jetzt ist Nachtruhe; leftS = Sekunden bis zum Ende
int remainingMin();          // geschätzte Restlaufzeit, −1 = unbekannt
float drainPerHour();        // Verbrauch in %/h (aus dem Verlauf), 0 = unbekannt
String historyJson();        // [[Minuten vor jetzt, %, mV, lädt], …]
void sleepInfo(const char* why, long leftS);  // aus main: was den Tiefschlaf aufhält bzw. wann er kommt
String sleepText();          // „Tiefschlaf in 7 min“ / „wach: Musik läuft“
String remainingText();      // „noch ca. 5 h 20 min“, „Restzeit wird gemessen“ bzw. "" am Kabel

}  // namespace pwr
