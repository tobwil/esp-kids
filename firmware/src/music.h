// Hören: Radiosender (radio-browser.info, webradio.h) und die ARD Audiothek (ard.h), gespielt über den
// MP3-Spieler (player.h). Auf der Uhr gibt es nur, was die Eltern freigegeben haben (kids.h):
//   „radio“    freigegebene Sender
//   „stories“  freigegebene Sendungen und Folgen der Audiothek, dazu „Zuletzt gehört“
// Die Weboberfläche fragt mit query() alles ab (Suche, Rubriken), um daraus freizugeben.
#pragma once
#include <Arduino.h>
#include "player.h"

namespace music {

enum Kind : uint8_t { K_TRACK = 0, K_LIST = 1 };

struct Item {
  uint8_t kind;
  uint16_t dur;      // Sekunden (Folge)
  char id[232];      // Titel: „ard:a:<MP3>“ oder „radio:<Adresse>“; Liste: Pfad für browse()
  char title[72];
  char sub[56];
};

using State = player::State;
static const int ART = 220;  // Cover-Kantenlänge in Pixeln

void begin();
void poll();                 // aus der Hauptschleife

// Listen für die Uhr (mit Freigabe-Filter): „radio“, „stories“, „recent“, freigegebene „ard:set:…“
void browse(const char* path);
bool browseBusy();
uint32_t browseSeq();        // zählt hoch, sobald ein neues Ergebnis da ist
int browseCopy(char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen);

// Für die Weboberfläche: beliebige Liste abrufen („rlsearch:…“, „ardsearch:…“, „ard:home“ …) und auf das Ergebnis
// warten (blockiert bis zu 20 s, ohne Freigabe-Filter und ohne die Liste auf der Uhr zu ändern)
int query(const char* path, char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen);

// parent: von den Eltern gestartet (Weboberfläche) – auch nachts und ohne Freigabe
void playList(const Item* items, int n, int start, bool parent = false);
bool parentStarted();        // läuft gerade etwas, das die Eltern gestartet haben?
bool isRadio(const char* id);

// „Zuletzt gehört“: die letzten 10 Folgen, neueste zuerst; sie setzen an der gemerkten Stelle fort
struct Recent { Item item; uint32_t posSec; };
int recent(Recent* out, int max);
bool recentRemove(const char* id);   // entfernt den Eintrag und vergisst die Stelle
void recentClear();

void toggle();
void next();                 // Folge: +30 s, Radio: nächster Sender
void prev();                 // Folge: −15 s, Radio: voriger Sender
void stop();
State state();
const uint16_t* art(uint32_t* version);

// Sleep-Timer: nach so vielen Minuten ausblenden und anhalten (0 = aus)
void sleepSet(int minutes);
void fadeOut();              // jetzt in 20 s ausblenden und anhalten (Zeit um)
int sleepLeftS();            // 0 = aus

}  // namespace music
