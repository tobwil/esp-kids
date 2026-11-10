#include "webradio.h"
#include <ArduinoJson.h>
#include <map>
#include "web.h"

namespace wradio {

static std::map<uint32_t, String> logos;  // Stream-Adresse (Hash) → Logo
static SemaphoreHandle_t mtx = nullptr;

static uint32_t hash(const char* s) {  // FNV-1a
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

static void ucopy(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  size_t len = strlen(src);
  if (len < n) { memcpy(dst, src, len + 1); return; }
  size_t cut = n - 1;
  while (cut > 0 && ((uint8_t)src[cut] & 0xC0) == 0x80) cut--;
  memcpy(dst, src, cut);
  dst[cut] = 0;
}

String logo(const char* url) {
  if (!mtx) return "";
  xSemaphoreTake(mtx, portMAX_DELAY);
  auto it = logos.find(hash(url));
  String r = it == logos.end() ? String() : it->second;
  xSemaphoreGive(mtx);
  return r;
}

void setLogo(const char* url, const String& image) {
  if (!mtx) mtx = xSemaphoreCreateMutex();
  xSemaphoreTake(mtx, portMAX_DELAY);
  logos[hash(url)] = image;
  xSemaphoreGive(mtx);
}

static void addList(music::Item* items, int max, int* count, const char* id, const char* title, const char* sub) {
  if (*count >= max) return;
  music::Item& o = items[(*count)++];
  o.kind = music::K_LIST;
  o.dur = 0;
  ucopy(o.id, sizeof(o.id), id);
  ucopy(o.title, sizeof(o.title), title);
  ucopy(o.sub, sizeof(o.sub), sub);
}

// Abfrage an das Senderverzeichnis. Bei schwachem WLAN reißt die Antwort manchmal mittendrin ab – dann
// erneut versuchen, abwechselnd bei zwei Servern.
static bool query(const String& q, music::Item* items, int max, int* count, char* err, size_t errLen) {
  static const char* HOSTS[] = {"de1", "de2", "de1", "fi1"};
  JsonDocument filter;
  filter[0]["name"] = true;
  filter[0]["url_resolved"] = true;
  filter[0]["favicon"] = true;
  filter[0]["tags"] = true;
  filter[0]["bitrate"] = true;
  JsonDocument d;
  int code = -1;
  bool ok = false;
  for (const char* h : HOSTS) {
    String body;
    code = web::get(String("https://") + h + ".api.radio-browser.info/json/stations/search?" + q +
                        "&codec=MP3&hidebroken=true&order=votes&reverse=true&limit=30",
                    body);
    if (code != 200 || body.length() < 2) continue;
    DeserializationError e = deserializeJson(d, body, DeserializationOption::Filter(filter));
    if (!e) { ok = true; break; }
    Serial.printf("Radio: Antwort von %s unvollständig (%s, %u Zeichen), versuche erneut\n", h, e.c_str(), body.length());
  }
  if (!ok) {
    snprintf(err, errLen, code == 200 ? "Verbindung zu schwach – nochmal versuchen" : "Senderliste nicht erreichbar (%d)", code);
    return false;
  }
  for (JsonObject s : d.as<JsonArray>()) {
    if (*count >= max) break;
    String url = s["url_resolved"] | "";
    url.trim();
    if (!url.startsWith("http") || url.length() + 7 >= sizeof(items[0].id)) continue;
    bool dup = false;
    for (int i = 0; i < *count && !dup; i++) dup = !strcmp(items[i].id + 6, url.c_str());
    if (dup) continue;
    music::Item& o = items[(*count)++];
    o.kind = music::K_TRACK;
    o.dur = 0;
    snprintf(o.id, sizeof(o.id), "radio:%s", url.c_str());
    String name = s["name"] | "";
    name.trim();
    ucopy(o.title, sizeof(o.title), name.c_str());
    // Untertitel: die ersten zwei Stichwörter
    String tags = s["tags"] | "";
    int c1 = tags.indexOf(','), c2 = c1 < 0 ? -1 : tags.indexOf(',', c1 + 1);
    if (c2 > 0) tags = tags.substring(0, c2);
    tags.replace(",", " · ");
    ucopy(o.sub, sizeof(o.sub), tags.c_str());
    xSemaphoreTake(mtx, portMAX_DELAY);
    logos[hash(url.c_str())] = s["favicon"] | "";
    xSemaphoreGive(mtx);
  }
  if (!*count) snprintf(err, errLen, "Keine Sender gefunden");
  return *count > 0;
}

bool browse(const char* path, char* title, size_t titleLen, music::Item* items, int max, int* count, char* err,
            size_t errLen) {
  if (!mtx) mtx = xSemaphoreCreateMutex();
  *count = 0;
  err[0] = 0;
  if (!strcmp(path, "rl:home")) {
    ucopy(title, titleLen, "Radio");
    addList(items, max, count, "rl:tag:kids", "Kinderradio", "Radio Teddy, TOGGO, Die Maus …");
    addList(items, max, count, "rl:top", "Beliebt in Deutschland", "Die meistgehörten Sender");
    addList(items, max, count, "rl:tag:pop", "Pop", "");
    addList(items, max, count, "rl:tag:classical", "Klassik", "");
    return true;
  }
  if (!strcmp(path, "rl:top")) {
    ucopy(title, titleLen, "Beliebt in Deutschland");
    return query("countrycode=DE", items, max, count, err, errLen);
  }
  if (!strncmp(path, "rl:tag:", 7)) {
    const char* t = path + 7;
    static const char* NAMES[][2] = {{"kids", "Kinderradio"}, {"pop", "Pop"}, {"classical", "Klassik"}};
    const char* name = t;
    for (auto& n : NAMES) if (!strcmp(n[0], t)) name = n[1];
    ucopy(title, titleLen, name);
    bool de = strcmp(t, "classical");  // deutschsprachig, Klassik weltweit
    return query(String("tag=") + web::urlencode(t) + (de ? "&countrycode=DE" : ""), items, max, count, err, errLen);
  }
  if (!strncmp(path, "rlsearch:", 9)) {
    snprintf(title, titleLen, "„%s“", path + 9);
    return query(String("name=") + web::urlencode(path + 9), items, max, count, err, errLen);
  }
  snprintf(err, errLen, "Unbekannte Liste");
  return false;
}

}  // namespace wradio
