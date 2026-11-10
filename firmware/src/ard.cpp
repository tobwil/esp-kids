#include "ard.h"
#include <ArduinoJson.h>
#include <map>
#include "web.h"
#include "webradio.h"
#include <algorithm>

namespace ard {

static const char* API = "https://api.ardaudiothek.de/graphql";
struct Meta { String show, image, set; };
static std::map<uint32_t, Meta> metas;  // MP3-Adresse (Hash) → Sendung, Bild
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

void meta(const char* url, String& show, String& image, String& setPath) {
  show = image = setPath = "";
  if (!mtx) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  auto it = metas.find(hash(url));
  if (it != metas.end()) { show = it->second.show; image = it->second.image; setPath = it->second.set; }
  xSemaphoreGive(mtx);
}

// Bildadressen enthalten einen Platzhalter für die Breite
static String img(const char* u) {
  String s = u ? u : "";
  s.replace("{width}", "300");
  return s;
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

static bool gql(const String& query, JsonDocument& d, char* err, size_t errLen) {
  JsonDocument req;
  req["query"] = query;
  String body, out;
  serializeJson(req, body);
  int code = web::post(API, body, "application/json", out);
  if (code != 200) { snprintf(err, errLen, "Audiothek nicht erreichbar (%d)", code); return false; }
  if (deserializeJson(d, out)) { snprintf(err, errLen, "Antwort unlesbar"); return false; }
  return true;
}

// GraphQL-Zeichenkette: Anführungszeichen und Backslash maskieren
static String quote(const char* s) {
  String r = "\"";
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') r += '\\';
    r += *s;
  }
  return r + "\"";
}

static void addSet(JsonObject s, music::Item* items, int max, int* count) {
  char id[40], sub[56];
  snprintf(id, sizeof(id), "ard:set:%s", (const char*)(s["id"] | ""));
  int n = s["numberOfElements"] | 0;
  const char* by = s["publicationService"]["title"] | "";
  snprintf(sub, sizeof(sub), "%s%s%d Folgen", by, by[0] ? " · " : "", n);
  addList(items, max, count, id, s["title"] | "", sub);
}

// „2026-11-13T06:15:00+01:00“ → „13.11.2026“
static String date(const char* d) {
  int y, m, dd;
  if (!d || sscanf(d, "%d-%d-%d", &y, &m, &dd) != 3) return "";
  char b[16];
  snprintf(b, sizeof(b), "%d.%d.%d", dd, m, y);
  return b;
}

static void addEpisode(JsonObject e, const char* show, const char* setImage, const char* setId, music::Item* items, int max, int* count) {
  if (*count >= max) return;
  // erste MP3-Adresse, die in die Kennung passt
  String url;
  for (JsonObject a : e["audios"].as<JsonArray>()) {
    String u = a["url"] | "";
    if (u.startsWith("http") && u.length() + 7 < sizeof(items[0].id)) { url = u; break; }
  }
  if (!url.length()) return;
  music::Item& o = items[(*count)++];
  o.kind = music::K_TRACK;
  o.dur = e["duration"] | 0;
  snprintf(o.id, sizeof(o.id), "ard:a:%s", url.c_str());
  ucopy(o.title, sizeof(o.title), e["title"] | "");
  const char* ps = e["programSet"]["title"] | show;
  String sub = date(e["publishDate"] | "");
  if (!sub.length()) sub = ps;
  ucopy(o.sub, sizeof(o.sub), sub.c_str());
  const char* im = e["image"]["url"] | setImage;
  xSemaphoreTake(mtx, portMAX_DELAY);
  const char* sid = e["programSet"]["id"] | setId;
  metas[hash(url.c_str())] = {ps ? ps : "", img(im), sid && sid[0] ? String("ard:set:") + sid : String()};
  xSemaphoreGive(mtx);
}

// Livestream als Sender in die Liste (spielt live, ohne Position)
static bool addStation(const char* url, const char* title, const char* sub, const String& image, music::Item* items,
                       int max, int* count) {
  if (*count >= max || strlen(url) + 7 >= sizeof(items[0].id)) return false;
  music::Item& o = items[(*count)++];
  o.kind = music::K_TRACK;
  o.dur = 0;
  snprintf(o.id, sizeof(o.id), "radio:%s", url);
  ucopy(o.title, sizeof(o.title), title);
  ucopy(o.sub, sizeof(o.sub), sub);
  wradio::setLogo(url, image);
  return true;
}

static String mp3Of(JsonObject n) {
  for (JsonObject a : n["audios"].as<JsonArray>())
    if (!strcmp(a["mimeType"] | "", "audio/mp3")) return a["url"] | "";
  return "";
}

bool browse(const char* path, char* title, size_t titleLen, music::Item* items, int max, int* count, char* err,
            size_t errLen) {
  if (!mtx) mtx = xSemaphoreCreateMutex();
  *count = 0;
  err[0] = 0;
  if (!strcmp(path, "ard:home")) {
    ucopy(title, titleLen, "ARD Audiothek");
    static const char* CATS[][2] = {{"42914714", "Für Kinder"}, {"42914712", "Hörspiel"}, {"42914713", "Hörbuch"},
                                    {"42914742", "Wissen"}, {"42914724", "Musik entdecken"}};
    for (auto& c : CATS) {
      char id[24];
      snprintf(id, sizeof(id), "ard:cat:%s", c[0]);
      addList(items, max, count, id, c[1], "Rubrik");
    }
    addList(items, max, count, "ard:live", "Radiosender live", "Alle ARD-Programme, z. B. KiRaKa");
    return true;
  }
  JsonDocument d;
  if (!strcmp(path, "ard:live")) {
    if (!gql("{ permanentLivestreams(first:120){ nodes{ title image{url} audios{url mimeType} } } }", d, err, errLen)) return false;
    ucopy(title, titleLen, "Radiosender live");
    for (JsonObject n : d["data"]["permanentLivestreams"]["nodes"].as<JsonArray>()) {
      String u = mp3Of(n);
      String t = n["title"] | "";
      t.replace("Livestream ", "");
      t.replace(" Livestream", "");
      if (u.length()) addStation(u.c_str(), t.c_str(), "ARD live", img(n["image"]["url"] | ""), items, max, count);
    }
    std::sort(items, items + *count, [](const music::Item& a, const music::Item& b) { return strcasecmp(a.title, b.title) < 0; });
  } else if (!strncmp(path, "ard:cat:", 8)) {
    String q = "{ editorialCategory(id:" + quote(path + 8) +
               "){ title sections { nodes { ... on ProgramSet { id title numberOfElements publicationService{title} } } } } }";
    if (!gql(q, d, err, errLen)) return false;
    JsonObject c = d["data"]["editorialCategory"];
    ucopy(title, titleLen, c["title"] | "Rubrik");
    for (JsonObject sec : c["sections"].as<JsonArray>())
      for (JsonObject s : sec["nodes"].as<JsonArray>()) {
        if (!s["id"].is<const char*>()) continue;
        bool dup = false;
        char id[40];
        snprintf(id, sizeof(id), "ard:set:%s", (const char*)(s["id"] | ""));
        for (int i = 0; i < *count && !dup; i++) dup = !strcmp(items[i].id, id);
        if (!dup) addSet(s, items, max, count);
      }
  } else if (!strncmp(path, "ard:set:", 8)) {
    String q = "{ programSet(id:" + quote(path + 8) +
               "){ title image{url} items(first:50){ nodes{ id title duration publishDate image{url} audios{url} } } } }";
    if (!gql(q, d, err, errLen)) return false;
    JsonObject s = d["data"]["programSet"];
    const char* show = s["title"] | "";
    ucopy(title, titleLen, show[0] ? show : "Sendung");
    for (JsonObject e : s["items"]["nodes"].as<JsonArray>()) addEpisode(e, show, s["image"]["url"] | "", path + 8, items, max, count);
  } else if (!strncmp(path, "ardsearch:", 10)) {
    String q = "{ search(query:" + quote(path + 10) +
               ", limit:15){ programSets{ nodes{ id title numberOfElements publicationService{title} } }"
               " items{ nodes{ id title duration publishDate image{url} audios{url}"
               " programSet{ id title numberOfElements publicationService{title} } } } } }";
    if (!gql(q, d, err, errLen)) return false;
    snprintf(title, titleLen, "„%s“", path + 10);
    JsonObject r = d["data"]["search"];
    for (JsonObject s : r["programSets"]["nodes"].as<JsonArray>()) addSet(s, items, max, count);
    // Die Suche findet Sendungen oft nicht („Tigerentenclub“ nur als „Tigerenten Club“), die Folgen aber schon –
    // deshalb auch deren Sendungen anbieten, damit man sie als Ganzes freigeben kann
    // (die Sendung mit den meisten gefundenen Folgen zuerst)
    JsonArray found = r["items"]["nodes"].as<JsonArray>();
    for (;;) {
      JsonObject best;
      int bestN = 0;
      for (JsonObject e : found) {
        JsonObject s = e["programSet"];
        const char* id = s["id"] | "";
        char full[40];
        snprintf(full, sizeof(full), "ard:set:%s", id);
        bool seen = !id[0];
        for (int i = 0; i < *count && !seen; i++) seen = !strcmp(items[i].id, full);
        if (seen) continue;
        int n = 0;
        for (JsonObject f : found) n += !strcmp(f["programSet"]["id"] | "", id);
        if (n > bestN) { bestN = n; best = s; }
      }
      if (!bestN) break;
      addSet(best, items, max, count);
    }
    for (JsonObject e : r["items"]["nodes"].as<JsonArray>()) addEpisode(e, "", "", "", items, max, count);
  } else {
    snprintf(err, errLen, "Unbekannte Liste");
    return false;
  }
  if (!*count) snprintf(err, errLen, "Nichts gefunden");
  return *count > 0;
}

}  // namespace ard
