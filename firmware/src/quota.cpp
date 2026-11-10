#include "quota.h"
#include <Preferences.h>
#include <time.h>
#include "kids.h"
#include "net.h"

namespace quota {

// NVS „quota“: r0…r2 Regeln, st Stand (Tag, Nutzung, Extra, Freigaben, Verlauf)
struct State {
  int32_t day;                 // Tage seit 1970 (lokal), −1 = unbekannt
  uint32_t usedMs[APPS];
  uint16_t extraMin[APPS];
  uint32_t until[APPS];        // Unix-Zeit
  uint16_t hist[DAYS][APPS];   // Minuten, [0] = gestern
};

static Rule rules[APPS];
static State st;
static uint32_t savedAt = 0;
static bool dirty = false;

static const char* const NAMES[APPS] = {"Balance", "Radio", "Hörspiele"};
const char* name(App a) { return a < APPS ? NAMES[a] : ""; }

static void saveState() {
  Preferences p;
  p.begin("quota", false);
  p.putBytes("st", &st, sizeof(st));
  p.end();
  dirty = false;
  savedAt = millis();
}

static int today() {
  if (!timeValid()) return -1;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  t.tm_hour = 12;
  t.tm_min = t.tm_sec = 0;
  return (int)(mktime(&t) / 86400);
}

static bool weekend() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return t.tm_wday == 0 || t.tm_wday == 6;
}

// neuer Tag: heutige Nutzung in den Verlauf, Extra-Minuten verfallen (Freigaben laufen nach Uhrzeit ab)
static void roll() {
  int d = today();
  if (d < 0 || d == st.day) return;
  if (st.day >= 0 && d > st.day) {
    int gap = d - st.day, shift = min(gap, DAYS);
    for (int k = DAYS - 1; k >= shift; k--) memcpy(st.hist[k], st.hist[k - shift], sizeof(st.hist[k]));
    for (int k = 0; k < shift; k++)
      for (int a = 0; a < APPS; a++) st.hist[k][a] = k == gap - 1 ? (st.usedMs[a] + 30000) / 60000 : 0;
  }
  st.day = d;
  memset(st.usedMs, 0, sizeof(st.usedMs));
  memset(st.extraMin, 0, sizeof(st.extraMin));
  saveState();
}

Rule rule(App a) { return rules[a]; }

void setRule(App a, const Rule& r) {
  if (a >= APPS) return;
  rules[a] = {(uint16_t)min<int>(r.weekday, 600), (uint16_t)min<int>(r.weekend, 600), r.free};
  char key[4];
  snprintf(key, sizeof(key), "r%d", a);
  Preferences p;
  p.begin("quota", false);
  p.putBytes(key, &rules[a], sizeof(Rule));
  p.end();
}

int limitMin(App a) {
  roll();
  int base = weekend() ? rules[a].weekend : rules[a].weekday;
  return base ? base + st.extraMin[a] : 0;
}

int extraMin(App a) { roll(); return st.extraMin[a]; }
int usedMin(App a) { roll(); return (st.usedMs[a] + 30000) / 60000; }
int historyMin(App a, int daysAgo) { roll(); return daysAgo >= 1 && daysAgo <= DAYS ? st.hist[daysAgo - 1][a] : 0; }

uint32_t until(App a) {
  uint32_t now = time(nullptr);
  return st.until[a] > now ? st.until[a] : 0;
}

static bool nightFor(App a) { return a == GAME ? kids::night() : kids::musicLocked(); }

long leftS(App a) {
  if (a >= APPS || !timeValid() || nightFor(a)) return 0;
  long left = -1;
  int lim = limitMin(a);
  if (lim) left = max(0L, (long)lim * 60 - (long)(st.usedMs[a] / 1000));
  if (!rules[a].free) {
    uint32_t u = until(a);
    long byUnlock = u ? (long)(u - time(nullptr)) : 0;
    left = left < 0 ? byUnlock : min(left, byUnlock);
  }
  return left;
}

Status status(App a) {
  if (!timeValid() || nightFor(a)) return NIGHT;
  int lim = limitMin(a);
  if (lim && st.usedMs[a] >= (uint32_t)lim * 60000) return USED_UP;
  if (!rules[a].free && !until(a)) return LOCKED;
  return OPEN;
}

static uint32_t bedtime() {  // nächste Bettzeit als Unix-Zeit
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int bed = kids::cfg().bed;
  t.tm_hour = bed / 60;
  t.tm_min = bed % 60;
  t.tm_sec = 0;
  time_t b = mktime(&t);
  if (b <= now) b += 86400;
  return b;
}

void unlock(App a, int minutes, bool raise) {
  if (a >= APPS || !timeValid()) return;
  roll();
  uint32_t now = time(nullptr);
  st.until[a] = minutes == UNTIL_BED ? bedtime() : now + (uint32_t)max(1, minutes) * 60;
  if (raise && minutes != UNTIL_BED) {  // Tageslimit so weit aufstocken, dass die Minuten wirklich gehen
    int lim = limitMin(a);
    int need = (int)((st.usedMs[a] + 59999) / 60000) + minutes;
    if (lim && need > lim) st.extraMin[a] += need - lim;
  }
  saveState();
}

void lock(App a) {
  if (a >= APPS) return;
  st.until[a] = 0;
  saveState();
}

void use(App a, uint32_t ms) {
  if (a >= APPS || !timeValid()) return;
  roll();
  st.usedMs[a] += ms;
  dirty = true;
}

void testUsed(App a, int minutes) {
  if (a >= APPS) return;
  roll();
  st.usedMs[a] = (uint32_t)max(0, minutes) * 60000u;
  saveState();
}

void poll() {
  roll();
  if (dirty && millis() - savedAt > 60000) saveState();  // den Flash schonen: höchstens einmal pro Minute
}

void begin() {
  static const Rule DEF[APPS] = {{15, 30, false}, {45, 60, false}, {45, 60, false}};
  Preferences p;
  p.begin("quota", true);
  for (int a = 0; a < APPS; a++) {
    char key[4];
    snprintf(key, sizeof(key), "r%d", a);
    rules[a] = DEF[a];
    if (p.getBytesLength(key) == sizeof(Rule)) p.getBytes(key, &rules[a], sizeof(Rule));
  }
  memset(&st, 0, sizeof(st));
  st.day = -1;
  if (p.getBytesLength("st") == sizeof(st)) p.getBytes("st", &st, sizeof(st));
  p.end();
  esp_register_shutdown_handler([]() { if (dirty) saveState(); });
}

}  // namespace quota
