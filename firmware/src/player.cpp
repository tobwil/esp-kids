#include "player.h"
#include <Preferences.h>
#include <mp3dec.h>
#include "art.h"
#include "audio.h"
#include "power.h"
#include "web.h"

namespace player {

static SemaphoreHandle_t mtx;
static State st;

static void ucopy(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  size_t len = strlen(src);
  if (len < n) { memcpy(dst, src, len + 1); return; }
  size_t cut = n - 1;
  while (cut > 0 && ((uint8_t)src[cut] & 0xC0) == 0x80) cut--;
  memcpy(dst, src, cut);
  dst[cut] = 0;
}

static uint32_t hash(const char* s) {  // FNV-1a
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

// ------------------------------------------------------------------ Position je Folge (Bytes + Sekunden)

static void savePos(const char* url, uint32_t bytes, uint32_t sec) {
  char key[12];
  snprintf(key, sizeof(key), "p%08x", (unsigned)hash(url));
  Preferences p;
  p.begin("pcpos", false);
  if (p.freeEntries() < 4) p.clear();  // Platz knapp: alte Stände verwerfen
  if (bytes) p.putULong64(key, ((uint64_t)bytes << 20) | (sec & 0xFFFFF));
  else p.remove(key);
  p.end();
}

static void loadPos(const char* url, uint32_t* bytes, uint32_t* sec) {
  char key[12];
  snprintf(key, sizeof(key), "p%08x", (unsigned)hash(url));
  Preferences p;
  p.begin("pcpos", true);
  uint64_t v = p.getULong64(key, 0);
  p.end();
  *bytes = v >> 20;
  *sec = v & 0xFFFFF;
}

void forgetPos(const char* url) { savePos(url, 0, 0); }

uint32_t resumeSec(const char* url) {
  uint32_t b, s;
  loadPos(url, &b, &s);
  return b ? s : 0;
}

// ------------------------------------------------------------------ Wiedergabe

enum Cmd : uint8_t { C_NONE, C_PLAY, C_TOGGLE, C_SKIP, C_STOP };
static volatile Cmd cmd = C_NONE;
static volatile int skipBy = 0;
static String reqUrl, reqTitle, reqShow, reqArt;
static uint16_t reqDur = 0;
static bool reqLive = false;
static volatile bool paused = false;
static volatile uint32_t baseSec = 0;

void playUrl(const char* url, const char* title, const char* show, const char* image, uint16_t dur, bool live) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  reqUrl = url;
  reqTitle = title;
  reqShow = show;
  reqArt = image;
  reqDur = dur;
  reqLive = live;
  st.active = true;  // gleich als aktiv melden – der Task übernimmt den Auftrag erst im nächsten Durchlauf
  st.live = live;
  ucopy(st.title, sizeof(st.title), title);
  ucopy(st.artist, sizeof(st.artist), show);
  xSemaphoreGive(mtx);
  cmd = C_PLAY;
}

void toggle() { cmd = C_TOGGLE; }
void skip(int s) { skipBy = s; cmd = C_SKIP; }
void stop() { cmd = C_STOP; }

State state() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  State s = st;
  xSemaphoreGive(mtx);
  s.playing = s.active && !paused;
  s.pos = baseSec + audio::musicPlayed() / (float)audio::SR;
  return s;
}

static void setMsg(const char* m) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  ucopy(st.msg, sizeof(st.msg), m);
  xSemaphoreGive(mtx);
}

// Internetradio: „StreamTitle='Interpret - Titel';“ aus dem Einschub lesen
static String streamTitle(const char* m) {
  const char* a = strstr(m, "StreamTitle='");
  if (!a) return "";
  a += 13;
  const char* b = strstr(a, "';");
  String t = b ? String(a).substring(0, b - a) : String(a);
  t.trim();
  return t;
}

// Länge eines ID3v2-Kopfs am Dateianfang (0 = keiner)
static uint32_t id3Size(const uint8_t* b, int n) {
  if (n < 10 || memcmp(b, "ID3", 3)) return 0;
  return 10 + ((b[6] & 0x7F) << 21 | (b[7] & 0x7F) << 14 | (b[8] & 0x7F) << 7 | (b[9] & 0x7F));
}

static void task(void*) {
  HMP3Decoder dec = MP3InitDecoder();
  // Puffer im PSRAM – der interne Speicher wird für TLS-Verbindungen gebraucht
  const int IN_SZ = 8192, MONO_N = 1152 * 3;
  uint8_t* in = (uint8_t*)heap_caps_malloc(IN_SZ, MALLOC_CAP_SPIRAM);
  int16_t* pcm = (int16_t*)heap_caps_malloc(1152 * 2 * 2, MALLOC_CAP_SPIRAM);
  int16_t* mono = (int16_t*)heap_caps_malloc(MONO_N * 2, MALLOC_CAP_SPIRAM);
  uint8_t* meta = (uint8_t*)heap_caps_malloc(16 * 255 + 1, MALLOC_CAP_SPIRAM);
  web::Conn* c = nullptr;
  String url;
  uint32_t offset = 0, pausedAt = 0, savedAt = 0, kbps = 128;
  int have = 0;
  uint32_t skipId3 = 0;
  bool id3Checked = false;
  float rsPhase = 0;
  int16_t rsPrev = 0;
  bool reopen = false;
  bool live = false;          // Radiosender
  int metaint = 0, metaLeft = 0;
  String station;
  uint32_t retryAt = 0;

  auto close = [&]() { if (c) { delete c; c = nullptr; } };
  auto openAt = [&](uint32_t at, uint32_t sec) -> bool {
    close();
    audio::musicPause(true);
    audio::musicFlush();
    baseSec = sec;
    have = 0;
    c = new web::Conn();
    char range[40];
    if (live) at = 0;
    snprintf(range, sizeof(range), "Range: bytes=%u-", (unsigned)at);
    if (!web::open(*c, url, live ? "Icy-MetaData: 1" : at ? range : nullptr, 15000)) {
      Serial.printf("Player: %s nicht erreichbar (%d)\n", live ? "Sender" : "Folge", c->code);
      close();
      setMsg(live ? "Sender nicht erreichbar" : "Folge nicht erreichbar");
      if (live) retryAt = millis() + 5000;
      return false;
    }
    metaint = metaLeft = live ? c->metaint : 0;
    retryAt = 0;
    if (at && c->code != 206) { at = 0; baseSec = 0; }  // Server kann keine Bereiche: von vorn
    offset = at;
    skipId3 = 0;
    id3Checked = false;
    rsPhase = 0;
    setMsg("");
    if (!paused) audio::musicPause(false);
    return true;
  };

  for (;;) {
    Cmd k = cmd;
    if (k != C_NONE) {
      cmd = C_NONE;
      if (k == C_PLAY) {
        if (c && url.length() && !live) savePos(url.c_str(), offset, baseSec + audio::musicPlayed() / audio::SR);
        xSemaphoreTake(mtx, portMAX_DELAY);
        url = reqUrl;
        live = reqLive;
        station = reqTitle;
        st.active = true;
        st.live = live;
        ucopy(st.title, sizeof(st.title), reqTitle.c_str());
        ucopy(st.artist, sizeof(st.artist), reqShow.c_str());
        snprintf(st.id, sizeof(st.id), "%08x", (unsigned)hash(url.c_str()));
        st.dur = reqDur;
        xSemaphoreGive(mtx);
        art::request(reqArt.c_str());
        uint32_t at = 0, sec = 0;
        if (!live) loadPos(url.c_str(), &at, &sec);  // dort weiter, wo man aufgehört hat
        paused = false;
        Serial.printf("Player: %s (ab %u s)\n", reqTitle.c_str(), (unsigned)sec);
        openAt(at, sec);
      } else if (k == C_TOGGLE && st.active) {
        paused = !paused;
        audio::musicPause(paused);
        if (paused) { pausedAt = millis(); if (!live) savePos(url.c_str(), offset, baseSec + audio::musicPlayed() / audio::SR); }
        else if (reopen || !c) { reopen = false; openAt(offset, baseSec + audio::musicPlayed() / audio::SR); }
      } else if (k == C_SKIP && st.active && !live) {
        uint32_t sec = baseSec + audio::musicPlayed() / audio::SR;
        int target = max(0, (int)sec + skipBy);
        // Byte-Position aus der Bitrate schätzen (MP3 meist mit konstanter Bitrate):
        // gelesen − noch im Eingangspuffer − noch im Musikpuffer, dann um die Sprungweite verschieben
        int32_t bps = (int32_t)kbps * 125;
        int32_t heard = (int32_t)offset - have - (int32_t)(audio::musicUsed() / audio::SR) * bps;
        int32_t at = max((int32_t)0, heard + (target - (int)sec) * bps);
        openAt(at, target);
      } else if (k == C_STOP) {
        if (c && !live) savePos(url.c_str(), offset, baseSec + audio::musicPlayed() / audio::SR);
        close();
        retryAt = 0;
        audio::musicPause(true);
        audio::musicFlush();
        xSemaphoreTake(mtx, portMAX_DELAY);
        st.active = false;
        xSemaphoreGive(mtx);
      }
    }
    // Sender weg: nach kurzer Pause neu verbinden
    if (!c && live && retryAt && !paused && st.active && millis() > retryAt) { setMsg("Verbinde neu …"); openAt(0, 0); continue; }
    if (!c) { vTaskDelay(pdMS_TO_TICKS(pwr::dozing() ? 1000 : 50)); continue; }  // dösend seltener nachsehen

    // lange Pause: Verbindung schließen, beim Weiterhören mit Range neu öffnen
    if (paused && millis() - pausedAt > 30000) { close(); reopen = true; continue; }
    // Position regelmäßig sichern
    if (!live && millis() - savedAt > 15000) { savedAt = millis(); savePos(url.c_str(), offset, baseSec + audio::musicPlayed() / audio::SR); }

    // Puffer füllen
    NetworkClient* s = c->stream();
    if (have < IN_SZ / 2) {
      int room = IN_SZ - have;
      if (metaint) room = min(room, metaLeft);
      int n = s->read(in + have, room);
      if (n > 0) { have += n; offset += n; if (metaint) metaLeft -= n; }
      if (metaint && metaLeft == 0) {
        // Titel-Einschub: 1 Byte Länge (×16), dann Text
        uint8_t* m = meta;
        int len = -1, got = 0;
        uint32_t t0 = millis();
        while (millis() - t0 < 3000) {
          if (len < 0) { int b = s->read(); if (b >= 0) len = b * 16; else { vTaskDelay(pdMS_TO_TICKS(5)); continue; } }
          if (got >= len) break;
          int r = s->read(m + got, len - got);
          if (r > 0) got += r; else vTaskDelay(pdMS_TO_TICKS(5));
        }
        metaLeft = metaint;
        if (len > 0 && got == len) {
          m[got] = 0;
          String t = streamTitle((const char*)m);
          if (t.length()) {
            xSemaphoreTake(mtx, portMAX_DELAY);
            ucopy(st.title, sizeof(st.title), t.c_str());
            ucopy(st.artist, sizeof(st.artist), station.c_str());
            xSemaphoreGive(mtx);
          }
        }
        continue;
      }
      if (n <= 0 && live && !s->connected() && !s->available()) {
        // Sender hat die Verbindung beendet: neu verbinden
        Serial.println("Radio: Verbindung weg, verbinde neu");
        close();
        retryAt = millis() + 2000;
        continue;
      }
      if (n <= 0 && !s->connected() && !s->available() && have < 4) {
        // Folge zu Ende: nach dem Leerlaufen des Puffers als gehört markieren
        close();
        while (audio::musicUsed() && cmd == C_NONE) vTaskDelay(pdMS_TO_TICKS(100));
        if (cmd == C_NONE) { savePos(url.c_str(), 0, 0); xSemaphoreTake(mtx, portMAX_DELAY); st.active = false; xSemaphoreGive(mtx); }
        continue;
      }
    }
    if (!id3Checked && have >= 10) {  // ID3-Kopf (oft mit großem Bild) am Dateianfang überspringen
      id3Checked = true;
      if (offset == (uint32_t)have) skipId3 = id3Size(in, have);
    }
    if (skipId3) {
      uint32_t k2 = min((uint32_t)have, skipId3);
      memmove(in, in + k2, have - k2);
      have -= k2;
      skipId3 -= k2;
      continue;
    }
    if (have < 2048) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
    // Platz im Musikpuffer abwarten
    if (audio::musicSpace() < 1152 * 3) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
    int sync = MP3FindSyncWord(in, have);
    if (sync < 0) { have = 0; continue; }
    unsigned char* p = in + sync;
    int left = have - sync;
    int err = MP3Decode(dec, &p, &left, pcm, 0);
    int used = (have - sync) - left;
    if (err) {  // kaputter Rahmen: ein Byte weiter suchen
      used = max(used, 1);
    } else {
      MP3FrameInfo fi;
      MP3GetLastFrameInfo(dec, &fi);
      if (fi.bitrate) kbps = fi.bitrate / 1000;
      int frames = fi.outputSamps / max(1, fi.nChans);
      // auf 44,1 kHz mono umrechnen (linear)
      float step = (float)fi.samprate / audio::SR;
      int on = 0;
      for (int i = 0; i < frames; i++) {
        int16_t x = fi.nChans == 2 ? ((int)pcm[2 * i] + pcm[2 * i + 1]) / 2 : pcm[i];
        while (rsPhase < 1 && on < MONO_N) {
          mono[on++] = rsPrev + (x - rsPrev) * rsPhase;
          rsPhase += step;
        }
        rsPhase -= 1;
        rsPrev = x;
      }
      audio::musicWrite(mono, on);
    }
    memmove(in, in + sync + used, have - sync - used);
    have -= sync + used;
  }
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "player", 12288, nullptr, 5, nullptr, 1);
}

}  // namespace player
