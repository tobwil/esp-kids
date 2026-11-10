// Zifferblatt: das 8-Bit-Bild aus faceart.cpp (Uhrzeit, Wetter), ×4 vergrößert.
//   Antippen: Apps (läuft Musik: die Wiedergabe) · 1,5 s gedrückt halten: Eltern-Menü (mit PIN)
//   hochwischen: Apps · runterwischen: Kontrollzentrum · am Rand drehen: Lautstärke (wenn Musik läuft)
#include <time.h>
#include "../faceart.h"
#include "../music.h"
#include "../net.h"
#include "../weather.h"
#include "ui.h"

using namespace ui;

namespace {

constexpr int S = faceart::S, Z = 4, OUT = S * Z;
uint16_t* px = nullptr;   // Raster
uint16_t* big = nullptr;  // vergrößert (Puffer der Leinwand)
lv_obj_t* canvas;
int lastFrame = -1;
int testCode = -1, testTemp = 0;
bool testDay = true;

void render(int f) {
  faceart::Scene s{};
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  s.timeValid = timeValid();
  s.hour = t.tm_hour;
  s.minute = t.tm_min;
  s.wday = t.tm_wday;
  s.mday = t.tm_mday;
  s.month = t.tm_mon + 1;
  weather::Now w = weather::now();
  int code = testCode >= 0 ? testCode : w.code;
  s.weather = testCode >= 0 || w.ok;
  s.sky = faceart::skyOf(code);
  s.heavy = faceart::heavyOf(code);
  s.temp = testCode >= 0 ? testTemp : (int)lroundf(w.temp);
  s.day = testCode >= 0 ? testDay : w.ok ? w.day : (t.tm_hour >= 7 && t.tm_hour < 19);
  s.battery = hwBattery();
  s.charging = hwCharging();
  s.playing = music::state().playing;
  s.frame = f;
  faceart::draw(px, s);
  // ×4 vergrößern
  for (int y = 0; y < S; y++) {
    uint16_t* row = big + y * Z * OUT;
    const uint16_t* src = px + y * S;
    for (int x = 0; x < S; x++) {
      uint16_t c = src[x];
      uint16_t* d = row + x * Z;
      d[0] = d[1] = d[2] = d[3] = c;
    }
    for (int r = 1; r < Z; r++) memcpy(row + r * OUT, row, OUT * 2);
  }
  lv_obj_invalidate(canvas);
}

void build(lv_obj_t* scr) {
  if (!px) px = (uint16_t*)heap_caps_malloc(S * S * 2, MALLOC_CAP_SPIRAM);
  if (!big) big = (uint16_t*)heap_caps_malloc(OUT * OUT * 2, MALLOC_CAP_SPIRAM);
  canvas = lv_canvas_create(scr);
  lv_canvas_set_buffer(canvas, big, OUT, OUT, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(canvas, (W - OUT) / 2, (H - OUT) / 2);
  lv_obj_t* touch = lv_obj_create(scr);
  lv_obj_remove_style_all(touch);
  lv_obj_set_size(touch, W, H);
  lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(touch, [](lv_event_t*) {
    if (bezelMoved()) return;
    play(0);
    if (music::state().active) push(&PAGE_MUSIC);
    else push(&PAGE_APPS);
  }, LV_EVENT_SHORT_CLICKED, nullptr);
  // Eltern-Menü: 1,5 s ruhig gedrückt halten (nicht beim Wischen oder Drehen am Rand)
  lv_obj_add_event_cb(touch, [](lv_event_t* e) {
    static uint32_t at = 0;
    static lv_point_t start;
    static bool fired = false;
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) { at = millis(); start = p; fired = false; return; }
    if (fired || bezelMoved() || abs(p.x - start.x) > 20 || abs(p.y - start.y) > 20) { fired = true; return; }
    if (millis() - at < 1500) return;
    fired = true;
    lv_indev_wait_release(lv_indev_active());
    askPin("Eltern-Menü", []() { push(&PAGE_PARENT); });
  }, LV_EVENT_ALL, nullptr);
  bezel(touch, []() { return music::state().active; });
  lastFrame = -1;
  render(0);
}

void update() {
  int f = millis() / 350;  // Regen, Schnee und Wolken bewegen sich
  if (f == lastFrame) return;
  lastFrame = f;
  render(f);
}

void resume() { lastFrame = -1; }

}  // namespace

void faceTest(int code, int temp, bool day) {
  testCode = code;
  testTemp = temp;
  testDay = day;
  lastFrame = -1;
}

const Page PAGE_FACE = {build, update, nullptr, resume, true};
