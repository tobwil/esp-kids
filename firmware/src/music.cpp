#include "music.h"
#include <Preferences.h>
#include <vector>
#include "ard.h"
#include "art.h"
#include "audio.h"
#include "kids.h"
#include "power.h"
#include "settings.h"
#include "webradio.h"

namespace music {

static const int BROWSE_MAX = 80;
static SemaphoreHandle_t mtx;
static Item* items;  // PSRAM
static int itemCount = 0;
static char listTitle[72] = "", listErr[64] = "";
static volatile uint32_t seq = 0;
static char want[240] = "";
static volatile bool wanted = false, busy = false;
static bool playing = false, radio = false, byParent = false;
static uint32_t sleepEnd = 0, fadeMs = 30000;
// Abfragen der Weboberfläche (eigener Puffer, damit die Liste auf der Uhr unberührt bleibt)
static char qPath[240];
static Item* qItems;
static int qCount;
static char qTitle[72], qErr[64];
static volatile bool qWanted = false, qDone = false;

static void ucopy(char* dst, size_t n, const char* src) {
  size_t len = strlen(src);
  if (len < n) { memcpy(dst, src, len + 1); return; }
  size_t cut = n - 1;
  while (cut > 0 && ((uint8_t)src[cut] & 0xC0) == 0x80) cut--;
  memcpy(dst, src, cut);
  dst[cut] = 0;
}

bool isRadio(const char* id) { return !strncmp(id, "radio:", 6); }

static void addList(Item* it, int* n, const char* id, const char* title, const char* sub) {
  Item& o = it[(*n)++];
  o.kind = K_LIST;
  o.dur = 0;
  ucopy(o.id, sizeof(o.id), id);
  ucopy(o.title, sizeof(o.title), title);
  ucopy(o.sub, sizeof(o.sub), sub);
}

// ------------------------------------------------------------------ Zuletzt gehört (NVS „music“/„hist“)

static const int RECENT_MAX = 10;
static Item* rec = nullptr;
static int recN = 0;

static void recentSave() {
  Preferences p;
  p.begin("music", false);
  if (recN) p.putBytes("hist", rec, recN * sizeof(Item));
  else p.remove("hist");
  p.end();
}

static void remember(const Item& it) {  // Folgen nach vorn; doppelte fallen weg
  if (!rec || it.kind != K_TRACK || isRadio(it.id)) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  int at = recN < RECENT_MAX ? recN : RECENT_MAX - 1;
  for (int i = 0; i < recN; i++)
    if (!strcmp(rec[i].id, it.id)) { at = i; break; }
  bool same = at == 0 && recN;
  if (!same) {
    memmove(rec + 1, rec, at * sizeof(Item));
    rec[0] = it;
    if (at >= recN) recN = std::min(recN + 1, RECENT_MAX);
  }
  xSemaphoreGive(mtx);
  if (!same) recentSave();
}

static uint32_t resumeOf(const char* id) { return !strncmp(id, "ard:a:", 6) ? player::resumeSec(id + 6) : 0; }

int recent(Recent* out, int max) {
  int n = 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < recN && n < max; i++) out[n++] = {rec[i], 0};
  xSemaphoreGive(mtx);
  for (int i = 0; i < n; i++) out[i].posSec = resumeOf(out[i].item.id);
  return n;
}

bool recentRemove(const char* id) {
  bool found = false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < recN; i++)
    if (!strcmp(rec[i].id, id)) {
      memmove(rec + i, rec + i + 1, (recN - i - 1) * sizeof(Item));
      recN--;
      found = true;
      break;
    }
  xSemaphoreGive(mtx);
  if (!found) return false;
  if (!strncmp(id, "ard:a:", 6)) player::forgetPos(id + 6);
  recentSave();
  return true;
}

void recentClear() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  int n = recN;
  recN = 0;
  xSemaphoreGive(mtx);
  for (int i = 0; i < n; i++)
    if (!strncmp(rec[i].id, "ard:a:", 6)) player::forgetPos(rec[i].id + 6);
  recentSave();
}

// angefangene Folgen: „· bei 12:34“ an den Untertitel
static void markResume(Item* it, int n) {
  for (int i = 0; i < n; i++) {
    if (it[i].kind != K_TRACK) continue;
    uint32_t sec = resumeOf(it[i].id);
    if (sec < 30) continue;
    char sub[56];
    snprintf(sub, sizeof(sub), "%s%sbei %u:%02u", it[i].sub, it[i].sub[0] ? " · " : "", (unsigned)(sec / 60), (unsigned)(sec % 60));
    ucopy(it[i].sub, sizeof(it[i].sub), sub);
  }
}

// ------------------------------------------------------------------ Listen

static int fetch(const char* path, Item* tmp, char* title, size_t titleLen, char* err, size_t errLen) {
  int n = 0;
  if (!strncmp(path, "rl", 2)) wradio::browse(path, title, titleLen, tmp, BROWSE_MAX, &n, err, errLen);
  else if (!strncmp(path, "ard", 3)) ard::browse(path, title, titleLen, tmp, BROWSE_MAX, &n, err, errLen);
  else ucopy(err, errLen, "Unbekannte Liste");
  return n;
}

// Listen der Uhr: nur Freigegebenes
static int kidList(const char* path, Item* tmp, char* title, size_t titleLen, char* err, size_t errLen) {
  int n = 0;
  if (!strcmp(path, "radio")) {
    ucopy(title, titleLen, "Radio");
    n = kids::list(tmp, BROWSE_MAX, "radio:");
    if (!n) ucopy(err, errLen, "Noch kein Sender freigegeben");
  } else if (!strcmp(path, "stories")) {
    ucopy(title, titleLen, "Hörspiele");
    n = kids::list(tmp, BROWSE_MAX - 1, "ard:");
    xSemaphoreTake(mtx, portMAX_DELAY);
    int r = recN;
    xSemaphoreGive(mtx);
    if (r) {  // „Zuletzt gehört“ als erster Eintrag
      memmove(tmp + 1, tmp, n * sizeof(Item));
      n++;
      char sub[56];
      snprintf(sub, sizeof(sub), "%d %s", r, r == 1 ? "Folge" : "Folgen");
      int k = 0;
      addList(tmp, &k, "recent", "Zuletzt gehört", sub);
    }
    if (!n) ucopy(err, errLen, "Noch nichts freigegeben");
  } else if (!strcmp(path, "recent")) {
    ucopy(title, titleLen, "Zuletzt gehört");
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < recN; i++) tmp[n++] = rec[i];
    xSemaphoreGive(mtx);
    if (!n) ucopy(err, errLen, "Noch nichts gehört");
  } else if (kids::released(path)) {
    n = fetch(path, tmp, title, titleLen, err, errLen);
  } else {
    ucopy(err, errLen, "Nicht freigegeben");
  }
  markResume(tmp, n);
  return n;
}

int query(const char* path, char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen) {
  while (qWanted) delay(10);
  ucopy(qPath, sizeof(qPath), path);
  qDone = false;
  qWanted = true;
  uint32_t t0 = millis();
  while (!qDone && millis() - t0 < 20000) delay(20);
  if (!qDone) { ucopy(err, errLen, "Keine Antwort"); return 0; }
  ucopy(title, titleLen, qTitle);
  ucopy(err, errLen, qErr);
  int n = min(max, qCount);
  memcpy(out, qItems, n * sizeof(Item));
  return n;
}

void browse(const char* path) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ucopy(want, sizeof(want), path);
  wanted = true;
  busy = true;
  xSemaphoreGive(mtx);
}

bool browseBusy() { return busy; }
uint32_t browseSeq() { return seq; }

int browseCopy(char* title, size_t titleLen, Item* out, int max, char* err, size_t errLen) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ucopy(title, titleLen, listTitle);
  ucopy(err, errLen, listErr);
  int n = min(max, itemCount);
  memcpy(out, items, n * sizeof(Item));
  xSemaphoreGive(mtx);
  return n;
}

static void worker(void*) {
  Item* tmp = (Item*)heap_caps_calloc(BROWSE_MAX, sizeof(Item), MALLOC_CAP_SPIRAM);
  for (;;) {
    if (qWanted) {
      qTitle[0] = qErr[0] = 0;
      qCount = !strcmp(qPath, "recent") ? 0 : fetch(qPath, qItems, qTitle, sizeof(qTitle), qErr, sizeof(qErr));
      markResume(qItems, qCount);
      qWanted = false;
      qDone = true;
    }
    if (!wanted) { vTaskDelay(pdMS_TO_TICKS(pwr::dozing() ? 1000 : 30)); continue; }  // dösend seltener nachsehen
    char path[240], title[72] = "", err[64] = "";
    xSemaphoreTake(mtx, portMAX_DELAY);
    ucopy(path, sizeof(path), want);
    wanted = false;
    xSemaphoreGive(mtx);
    int n = kidList(path, tmp, title, sizeof(title), err, sizeof(err));
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (!wanted) {  // inzwischen schon wieder etwas anderes gewünscht? dann verwerfen
      memcpy(items, tmp, n * sizeof(Item));
      itemCount = n;
      ucopy(listTitle, sizeof(listTitle), title);
      ucopy(listErr, sizeof(listErr), err);
      busy = false;
      seq = seq + 1;
    }
    xSemaphoreGive(mtx);
  }
}

// ------------------------------------------------------------------ Wiedergabe

// Radio: die Senderliste, aus der gespielt wurde – Weiter/Zurück wechseln den Sender
static std::vector<Item> stations;
static int stationIdx = 0;

static void playStation(int i) {
  if (stations.empty()) return;
  stationIdx = (i % (int)stations.size() + stations.size()) % stations.size();
  const Item& s = stations[stationIdx];
  const char* url = s.id + 6;  // „radio:“
  player::playUrl(url, s.title, s.title, wradio::logo(url).c_str(), 0, true);
}

void playList(const Item* list, int n, int start, bool parent) {
  if (start < 0 || start >= n || list[start].kind != K_TRACK) return;
  if (kids::musicLocked() && !parent) return;  // Schlafenszeit
  const char* id = list[start].id;
  byParent = parent;
  if (isRadio(id)) {
    stations.clear();
    int index = 0;
    for (int i = 0; i < n; i++) {
      if (list[i].kind != K_TRACK || !isRadio(list[i].id)) continue;
      if (i == start) index = stations.size();
      stations.push_back(list[i]);
    }
    playStation(index);
    radio = true;
  } else if (!strncmp(id, "ard:a:", 6)) {
    remember(list[start]);
    String show, image, set;
    ard::meta(id + 6, show, image, set);
    player::playUrl(id + 6, list[start].title, show.length() ? show.c_str() : list[start].sub, image.c_str(), list[start].dur, false);
    radio = false;
  } else {
    return;
  }
  playing = true;
  sleepSet(0);
}

bool parentStarted() { return playing && byParent; }

void toggle() { if (playing) player::toggle(); }
void next() {
  if (radio) playStation(stationIdx + 1);
  else if (playing) player::skip(30);
}
void prev() {
  if (radio) playStation(stationIdx - 1);
  else if (playing) player::skip(-15);
}
void stop() {
  if (playing) player::stop();
  playing = false;
  byParent = false;
}

State state() {
  if (playing) return player::state();
  State e{};
  return e;
}

const uint16_t* art(uint32_t* version) { return art::image(version); }

// ------------------------------------------------------------------ Lautstärke: Ausblenden (Sleep-Timer, Zeit um)

void sleepSet(int minutes) {
  fadeMs = 30000;
  sleepEnd = minutes > 0 ? millis() + minutes * 60000UL : 0;
  if (!sleepEnd) audio::setVolume(cfg.volume);
}

void fadeOut() {
  if (!playing || (sleepEnd && sleepEnd - millis() < 20000)) return;
  fadeMs = 20000;
  sleepEnd = millis() + fadeMs;
}

int sleepLeftS() { return sleepEnd ? max(0L, (long)(sleepEnd - millis()) / 1000) : 0; }

void poll() {
  if (playing && !player::state().active) { playing = false; byParent = false; }  // Folge zu Ende
  if (!sleepEnd) return;
  uint32_t now = millis();
  if (!playing) sleepSet(0);  // nichts läuft mehr: Timer aus
  else if ((int32_t)(sleepEnd - now) <= 0) {
    sleepEnd = 0;
    stop();
    audio::setVolume(cfg.volume);
  } else if (sleepEnd - now < fadeMs) {
    audio::setVolume((int)(cfg.volume * (sleepEnd - now) / (float)fadeMs));
  }
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  items = (Item*)heap_caps_calloc(BROWSE_MAX, sizeof(Item), MALLOC_CAP_SPIRAM);
  qItems = (Item*)heap_caps_calloc(BROWSE_MAX, sizeof(Item), MALLOC_CAP_SPIRAM);
  art::begin();
  player::begin();
  rec = (Item*)heap_caps_calloc(RECENT_MAX, sizeof(Item), MALLOC_CAP_SPIRAM);
  Preferences p;
  p.begin("music", true);
  size_t len = p.getBytesLength("hist");
  if (rec && len && len % sizeof(Item) == 0 && len <= RECENT_MAX * sizeof(Item)) {
    p.getBytes("hist", rec, len);
    recN = len / sizeof(Item);
  }
  p.end();
  xTaskCreatePinnedToCore(worker, "browse", 12288, nullptr, 3, nullptr, 1);
}

}  // namespace music
