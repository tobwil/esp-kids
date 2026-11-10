#include "kids.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <vector>

namespace kids {

static Cfg c;

const Cfg& cfg() { return c; }
bool hasPin() { return c.pin[0] != 0; }
bool pinOk(const char* pin) { return hasPin() && pin && !strcmp(pin, c.pin); }

// Minuten seit der Bettzeit, −1 = nicht Nacht
static int nightMinutes() {
  if (time(nullptr) < 1700000000) return -1;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int m = t.tm_hour * 60 + t.tm_min;
  bool isNight = c.bed > c.wake ? (m >= c.bed || m < c.wake) : (m >= c.bed && m < c.wake);
  if (!isNight) return -1;
  return (m - c.bed + 1440) % 1440;
}

bool night() { return nightMinutes() >= 0; }
bool musicLocked() { int n = nightMinutes(); return n >= 0 && n >= c.storyMin; }
int storyLeftS() {
  int n = nightMinutes();
  if (n < 0 || n >= c.storyMin) return 0;
  return (c.storyMin - n) * 60 - time(nullptr) % 60;
}
int limitVolume(int pct) { return min(pct, (int)c.maxVol); }

void set(const Cfg& n) {
  c = n;
  c.wake = min<uint16_t>(c.wake, 23 * 60);
  c.bed = constrain(c.bed, c.wake + 120, 24 * 60 - 1);
  c.maxVol = constrain(c.maxVol, 10, 100);
  c.storyMin = min<uint8_t>(c.storyMin, 120);
  Preferences p;
  p.begin("kids", false);
  p.putUShort("wake", c.wake);
  p.putUShort("bed", c.bed);
  p.putUChar("story", c.storyMin);
  p.putUChar("maxvol", c.maxVol);
  p.putString("pin", c.pin);
  p.end();
}

// Zur Bettzeit läuft noch die Einschlafzeit, danach sanft ausblenden
void poll() {
  static bool wasNight = false, wasLocked = false;
  bool n = night(), l = musicLocked();
  if (n && !wasNight && music::state().active) music::sleepSet(max(1, storyLeftS() / 60));
  if (l && !wasLocked && music::state().active) music::sleepSet(1);
  wasNight = n;
  wasLocked = l;
}

// ------------------------------------------------------------------ Freigaben (NVS „kids“/„all“, als JSON)

struct Entry { String path, title, sub; };
static std::vector<Entry> rel;
static SemaphoreHandle_t mtx = nullptr;

static void saveReleases() {
  JsonDocument d;
  for (auto& e : rel) {
    JsonObject o = d.add<JsonObject>();
    o["p"] = e.path;
    o["t"] = e.title;
    o["s"] = e.sub;
  }
  String out;
  serializeJson(d, out);
  Preferences p;
  p.begin("kids", false);
  p.putString("all", out);
  p.end();
}

bool released(const char* path) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  bool r = false;
  for (auto& e : rel) if (e.path == path) { r = true; break; }
  xSemaphoreGive(mtx);
  return r;
}

void release(const char* path, const char* title, const char* sub, bool on) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  int at = -1;
  for (size_t i = 0; i < rel.size(); i++) if (rel[i].path == path) { at = i; break; }
  if (!on && at >= 0) rel.erase(rel.begin() + at);
  if (on && at < 0 && rel.size() < RELEASE_MAX) rel.insert(rel.begin(), {path, title, sub});  // neueste zuerst
  bool changed = on != (at >= 0);
  if (changed) saveReleases();
  xSemaphoreGive(mtx);
}

int list(music::Item* out, int max, const char* prefix) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  int n = 0;
  for (auto& e : rel) {
    if (n >= max) break;
    if (prefix && !e.path.startsWith(prefix)) continue;
    music::Item& o = out[n++];
    // Sender und einzelne Folgen sind direkt spielbar, Sendungen sind Listen
    o.kind = e.path.startsWith("ard:set:") ? music::K_LIST : music::K_TRACK;
    o.dur = 0;
    snprintf(o.id, sizeof(o.id), "%s", e.path.c_str());
    snprintf(o.title, sizeof(o.title), "%s", e.title.c_str());
    snprintf(o.sub, sizeof(o.sub), "%s", e.sub.c_str());
  }
  xSemaphoreGive(mtx);
  return n;
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  Preferences p;
  p.begin("kids", true);
  c.wake = p.getUShort("wake", 6 * 60 + 30);
  c.bed = p.getUShort("bed", 19 * 60 + 30);
  c.storyMin = p.getUChar("story", 30);
  c.maxVol = p.getUChar("maxvol", 60);
  p.getString("pin", c.pin, sizeof(c.pin));
  String s = p.getString("all", "[]");
  p.end();
  JsonDocument d;
  if (deserializeJson(d, s)) return;
  for (JsonObject o : d.as<JsonArray>()) {
    String path = o["p"] | "";
    if (path.startsWith("radio:") || path.startsWith("ard:")) rel.push_back({path, o["t"] | "", o["s"] | ""});
  }
}

}  // namespace kids
