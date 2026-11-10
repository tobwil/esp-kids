// MP3-Spieler für Radiosender (live, Titel aus dem Stream, verbindet bei Abbruch neu) und Audiothek-Folgen
// (gemerkte Position je Folge, Springen per Range-Anfrage). Dekodiert mit libhelix, läuft in einer eigenen Aufgabe.
#pragma once
#include <Arduino.h>

namespace player {

struct State {
  bool active;       // es läuft etwas (oder ist pausiert)
  bool playing;      // nicht pausiert
  bool live;         // Radiosender
  char title[72];
  char artist[56];
  char id[16];       // wechselt mit jedem neuen Stück
  uint16_t dur;      // Sekunden (0 = unbekannt/live)
  float pos;         // Sekunden
  char msg[64];      // Fehler / Hinweis
};

void begin();
// url: MP3-Adresse; live = Radiosender (ohne Position, ohne Springen)
void playUrl(const char* url, const char* title, const char* show, const char* image, uint16_t dur, bool live);
uint32_t resumeSec(const char* url);  // gemerkte Stelle einer Folge in Sekunden (0 = keine oder zu Ende gehört)
void forgetPos(const char* url);
void toggle();
void skip(int seconds);
void stop();
State state();

}  // namespace player
