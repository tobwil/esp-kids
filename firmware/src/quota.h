// Zeitkonto: Balance, Radio und Hörspiele (ARD Audiothek) gibt es nur mit Freigabe der Eltern – per PIN an der Uhr
// oder in der Weboberfläche – und höchstens so viele Minuten pro Tag, wie die Eltern eingestellt haben (Mo–Fr und
// Sa/So getrennt). Gezählt wird nur echte Nutzung: Spielen bzw. Hören. Neuer Tag um Mitternacht; der Stand, die
// Freigaben und die letzten sieben Tage überstehen Neustarts.
#pragma once
#include <Arduino.h>

namespace quota {

enum App : uint8_t { GAME, RADIO, STORIES, APPS };

struct Rule {
  uint16_t weekday, weekend;  // Minuten pro Tag, 0 = ohne Grenze
  bool free;                  // ohne Freigabe (nur das Tageslimit gilt)
};

enum Status : uint8_t {
  OPEN,     // darf jetzt
  LOCKED,   // braucht eine Freigabe
  USED_UP,  // Tageslimit erreicht
  NIGHT,    // Schlafenszeit
};

static const int UNTIL_BED = -1;  // Freigabe „bis zur Bettzeit“
static const int DAYS = 7;

void begin();
void poll();                              // aus der Hauptschleife (neuer Tag, sichern)
const char* name(App a);                  // „Balance“, „Radio“, „Hörspiele“
Rule rule(App a);
void setRule(App a, const Rule& r);

Status status(App a);
long leftS(App a);                        // Sekunden, die jetzt noch gehen (−1 = unbegrenzt, 0 = nichts)
uint32_t until(App a);                    // Freigabe bis (Unix-Zeit), 0 = keine
int usedMin(App a);                       // heute genutzt
int limitMin(App a);                      // heutiges Limit samt Extra-Minuten, 0 = ohne Grenze
int extraMin(App a);                      // heute zusätzlich gewährt
int historyMin(App a, int daysAgo);       // 1 = gestern … DAYS

// Freigeben für so viele Minuten (UNTIL_BED = bis zur Bettzeit). Mit raise reichen die Eltern bei Bedarf auch das
// Tageslimit so weit auf, dass die Minuten wirklich gehen (Freigabe an der Uhr oder „+“ in der Weboberfläche).
void unlock(App a, int minutes, bool raise);
void lock(App a);                         // Freigabe sofort beenden
void use(App a, uint32_t ms);             // tatsächlich genutzt (Spiel läuft, Musik spielt)
void testUsed(App a, int minutes);        // Test über USB: heute genutzte Minuten setzen

}  // namespace quota
