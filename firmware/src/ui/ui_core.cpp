// Navigation, Bausteine, Tastatur und Einblendungen (Hinweise, Uhr finden, Ausschalten, Lautstärke).
#include <math.h>
#include "../audio.h"
#include "../kids.h"
#include "../music.h"
#include "../settings.h"
#include "ui.h"

namespace ui {

// ------------------------------------------------------------------ Seiten-Stapel

struct Entry { const Page* page; lv_obj_t* scr; };
static Entry stack[10];
static int depth = 0;

const Page* top() { return depth ? stack[depth - 1].page : nullptr; }
lv_obj_t* topScreen() { return depth ? stack[depth - 1].scr : nullptr; }

void play(uint8_t fx) { audio::play((audio::Fx)fx); }

static bool bezelActive = false, bzMoved = false;
bool bezelMoved() { return bzMoved; }

lv_color_t appColor(quota::App a) { return a == quota::GAME ? C_GOLD() : a == quota::RADIO ? C_RADIO() : C_STORY(); }
const char* appIcon(quota::App a) { return a == quota::GAME ? I_GAME : a == quota::RADIO ? I_RADIO : I_LIBRARY; }

void fmtLeft(char* out, size_t n, long s) {
  long m = (s + 59) / 60;
  if (m >= 60 && m % 60) snprintf(out, n, "noch %ld Std. %ld Min.", m / 60, m % 60);
  else if (m >= 60) snprintf(out, n, "noch %ld Std.", m / 60);
  else snprintf(out, n, "noch %ld Min.", max(1L, m));
}

// Gesten wie bei Wear OS / watchOS:
//   Zifferblatt: ↑ Apps · ↓ Kontrollzentrum
//   sonst: → zurück; Apps auch ↓, Kontrollzentrum auch ↑ (zurück in die Richtung, aus der sie kamen)
static void gestureCb(lv_event_t*) {
  if (bezelActive) return;  // Drehen am Rand ist kein Wischen
  lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
  const Page* p = top();
  if (!p) return;
  if (p->watchFace) {
    if (d == LV_DIR_TOP) push(&PAGE_APPS);
    else if (d == LV_DIR_BOTTOM) push(&PAGE_CONTROL);
    else return;
  } else if (d == LV_DIR_RIGHT || (p->enter == ENTER_BOTTOM && d == LV_DIR_BOTTOM) ||
             (p->enter == ENTER_TOP && d == LV_DIR_TOP)) {
    back();
  } else {
    return;
  }
  lv_indev_wait_release(lv_indev_active());
}

static lv_obj_t* newScreen() {
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s, C_BG(), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s, C_TEXT(), 0);
  lv_obj_set_style_text_font(s, &font_text_20, 0);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s, gestureCb, LV_EVENT_GESTURE, nullptr);
  return s;
}

void push(const Page* p) {
  if (depth == 10) return;
  lv_obj_t* s = newScreen();
  stack[depth++] = {p, s};
  p->build(s);
  static const lv_screen_load_anim_t IN[] = {LV_SCR_LOAD_ANIM_OVER_LEFT, LV_SCR_LOAD_ANIM_OVER_TOP, LV_SCR_LOAD_ANIM_OVER_BOTTOM};
  if (depth == 1) lv_screen_load(s);
  else lv_screen_load_anim(s, IN[p->enter % 3], 200, 0, false);
}

void back() {
  if (depth <= 1) return;
  Entry cur = stack[--depth];
  if (cur.page->leave) cur.page->leave();
  play(audio::FX_TAP);
  static const lv_screen_load_anim_t OUT[] = {LV_SCR_LOAD_ANIM_OUT_RIGHT, LV_SCR_LOAD_ANIM_OUT_BOTTOM, LV_SCR_LOAD_ANIM_OUT_TOP};
  lv_screen_load_anim(stack[depth - 1].scr, OUT[cur.page->enter % 3], 200, 0, true);
  if (stack[depth - 1].page->resume) stack[depth - 1].page->resume();
}

// Zurück bis zur Stapel-Position at: Zwischenseiten sofort löschen, die sichtbare oberste per Animation
static void popTo(int at) {
  if (depth <= at + 1) return;
  for (int i = depth - 2; i > at; i--) {
    if (stack[i].page->leave) stack[i].page->leave();
    lv_obj_delete(stack[i].scr);
  }
  stack[at + 1] = stack[depth - 1];
  depth = at + 2;
  back();
}

void home() { popTo(0); }

void show(const Page* p) {
  int at = -1;
  for (int i = 0; i < depth; i++) if (stack[i].page == p) at = i;
  if (at < 0) { push(p); return; }
  if (at == depth - 1) return;
  popTo(at);
}

// ------------------------------------------------------------------ Bausteine

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t* title(lv_obj_t* scr, const char* text, lv_color_t color) {
  lv_obj_t* l = label(scr, &font_text_24, color, text);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 46);
  return l;
}

lv_obj_t* roundButton(lv_obj_t* parent, const char* icon, int size, lv_color_t bg, lv_color_t fg, const lv_font_t* font) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, size, size);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_mix(lv_color_white(), bg, 50), LV_STATE_PRESSED);
  lv_obj_set_style_transform_scale(b, 235, LV_STATE_PRESSED);
  lv_obj_set_style_transform_pivot_x(b, size / 2, 0);
  lv_obj_set_style_transform_pivot_y(b, size / 2, 0);
  lv_obj_t* l = label(b, font, fg, icon);
  lv_obj_center(l);
  return b;
}

lv_obj_t* pill(lv_obj_t* parent, const char* text, lv_color_t bg, lv_color_t fg) {
  lv_obj_t* p = lv_button_create(parent);
  lv_obj_set_height(p, 48);
  lv_obj_set_width(p, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(p, 22, 0);
  lv_obj_set_style_radius(p, 24, 0);
  lv_obj_set_style_bg_color(p, bg, 0);
  lv_obj_set_style_shadow_width(p, 0, 0);
  lv_obj_set_style_bg_color(p, lv_color_mix(lv_color_white(), bg, 40), LV_STATE_PRESSED);
  lv_obj_t* l = label(p, &font_text_20, fg, text);
  lv_obj_center(l);
  return p;
}

static void listScrollCb(lv_event_t* e) { listFit((lv_obj_t*)lv_event_get_target(e)); }

lv_obj_t* roundList(lv_obj_t* scr, int top, int bottom) {
  lv_obj_t* l = lv_obj_create(scr);
  lv_obj_set_size(l, W, H);
  lv_obj_set_pos(l, 0, 0);
  lv_obj_set_style_bg_opa(l, 0, 0);
  lv_obj_set_style_border_width(l, 0, 0);
  lv_obj_set_style_radius(l, 0, 0);
  lv_obj_set_style_pad_top(l, top, 0);
  lv_obj_set_style_pad_bottom(l, bottom + 60, 0);
  lv_obj_set_style_pad_hor(l, 0, 0);
  lv_obj_set_style_pad_row(l, 10, 0);
  lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(l, LV_DIR_VER);
  lv_obj_add_event_cb(l, listScrollCb, LV_EVENT_SCROLL, nullptr);
  return l;
}

lv_obj_t* listRow(lv_obj_t* list, const char* icon, lv_color_t iconColor, const char* text, const char* sub) {
  lv_obj_t* r = lv_button_create(list);
  lv_obj_set_size(r, 380, sub ? 78 : 66);
  lv_obj_set_style_radius(r, 39, 0);
  lv_obj_set_style_bg_color(r, C_SURF(), 0);
  lv_obj_set_style_bg_color(r, C_SURF2(), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(r, 0, 0);
  lv_obj_set_style_pad_hor(r, 22, 0);
  lv_obj_set_style_pad_column(r, 14, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);  // für listFit
  if (icon) label(r, &font_text_32, iconColor, icon);
  lv_obj_t* col = lv_obj_create(r);
  lv_obj_remove_flag(col, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_add_flag(col, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_style_bg_opa(col, 0, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_row(col, 0, 0);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  // feste Höhe = eine Zeile, sonst bricht LONG_DOT um statt „…“ zu setzen
  lv_obj_t* t = label(col, &font_text_24, C_TEXT(), text);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_size(t, lv_pct(100), lv_font_get_line_height(&font_text_24));
  lv_obj_t* s = label(col, &font_text_20, C_DIM(), sub ? sub : "");
  lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
  lv_obj_set_size(s, lv_pct(100), lv_font_get_line_height(&font_text_20));
  if (!sub) lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
  return r;
}

void listRowSub(lv_obj_t* row, const char* sub) {
  lv_obj_t* col = lv_obj_get_child(row, -1);
  lv_obj_t* s = lv_obj_get_child(col, 1);
  lv_label_set_text(s, sub);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_HIDDEN);
}

void listTitle(lv_obj_t* list, const char* text, lv_color_t c) {
  lv_obj_t* t = label(list, &font_text_24, c, text);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
}

void listFit(lv_obj_t* list) {
  lv_obj_update_layout(list);
  int n = lv_obj_get_child_count(list);
  for (int i = 0; i < n; i++) {
    lv_obj_t* c = lv_obj_get_child(list, i);
    lv_area_t a;
    lv_obj_get_coords(c, &a);
    // Breite nach der Sehne knapp innerhalb der Zeilenmitte; zum Rand hin schmaler, aber nie winzig
    int dy = abs((a.y1 + a.y2) / 2 - CY) + (a.y2 - a.y1) / 4;
    int chord = dy >= 228 ? 0 : (int)(2 * sqrtf(228.0f * 228 - dy * dy)) - 24;
    int maxW = lv_obj_has_flag(c, LV_OBJ_FLAG_USER_1) ? 384 : 330;
    int w = SQUARE ? maxW + 56 : constrain(chord, 240, maxW);  // eckig: volle Breite, rund: der Rundung folgen
    if (lv_obj_get_width(c) != w) lv_obj_set_width(c, w);
    int mid = abs((a.y1 + a.y2) / 2 - CY);
    lv_obj_set_style_opa(c, SQUARE || mid < 150 ? LV_OPA_COVER : (lv_opa_t)max(60, 255 - (mid - 150) * 3), 0);
  }
}

// ------------------------------------------------------------------ Einblendungen

lv_obj_t* overlay(lv_color_t bg, lv_opa_t opa) {
  lv_obj_t* o = lv_obj_create(lv_layer_top());
  lv_obj_set_size(o, W, H);
  lv_obj_set_pos(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t* toastObj = nullptr;
static lv_timer_t* toastTimer = nullptr;

void toast(const char* text, uint32_t ms) {
  if (!toastObj) {
    toastObj = lv_obj_create(lv_layer_top());
    lv_obj_set_style_bg_color(toastObj, C_SURF2(), 0);
    lv_obj_set_style_border_width(toastObj, 0, 0);
    lv_obj_set_style_radius(toastObj, 26, 0);
    lv_obj_set_style_pad_hor(toastObj, 22, 0);
    lv_obj_set_style_pad_ver(toastObj, 12, 0);
    lv_obj_set_size(toastObj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(toastObj, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_t* l = label(toastObj, &font_text_20, C_TEXT(), "");
    lv_obj_set_style_max_width(l, 320, 0);
  }
  lv_label_set_text(lv_obj_get_child(toastObj, 0), text);
  lv_obj_align(toastObj, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_move_foreground(toastObj);
  if (toastTimer) lv_timer_delete(toastTimer);
  toastTimer = lv_timer_create([](lv_timer_t*) {
    if (toastObj) { lv_obj_delete(toastObj); toastObj = nullptr; }
    toastTimer = nullptr;
  }, ms, nullptr);
  lv_timer_set_repeat_count(toastTimer, 1);
}

// Uhr finden: laut piepen, bis jemand die Uhr in die Hand nimmt
static lv_obj_t* findObj = nullptr;
static uint32_t findAt = 0, findBeep = 0;

void findMe(bool on, int volume) {
  if (findObj) { lv_obj_delete(findObj); findObj = nullptr; }
  audio::setVolume(cfg.volume);
  if (!on) return;
  hwScreen(true);
  hwActivity();
  findObj = overlay(C_BG(), LV_OPA_COVER);
  lv_obj_t* i = label(findObj, &font_icon_64, C_GOLD(), I_BELL);
  lv_obj_align(i, LV_ALIGN_CENTER, 0, -50);
  lv_obj_t* t = label(findObj, &font_text_32, C_TEXT(), "Hier bin ich!");
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 40);
  lv_obj_t* h = label(findObj, &font_text_20, C_DIM(), "Antippen zum Beenden");
  lv_obj_align(h, LV_ALIGN_CENTER, 0, 90);
  lv_obj_add_flag(findObj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(findObj, [](lv_event_t*) { lv_async_call([](void*) { findMe(false); }, nullptr); }, LV_EVENT_CLICKED, nullptr);
  audio::setVolume(constrain(volume, 5, 100));
  findAt = millis();
  findBeep = 0;
}

static void findTick() {
  if (!findObj) return;
  if (millis() - findAt > 30000) { findMe(false); return; }
  if (millis() - findBeep > 1300) { findBeep = millis(); audio::play(audio::FX_ALARM); hwActivity(); }
}

// Ausschalten / Neustart
static lv_obj_t* powerObj = nullptr;

static void powerClose() {
  if (powerObj) { lv_obj_delete(powerObj); powerObj = nullptr; }
}

void powerDialog() {
  if (powerObj) return;
  powerObj = overlay(C_BG(), LV_OPA_90);
  struct B { const char* icon; const char* text; lv_color_t col; int x; };
  const B bs[3] = {{I_POWER, "Aus", C_RED(), -120}, {I_RESTART, "Neustart", C_SURF2(), 0}, {I_CLOSE, "Zurück", C_SURF2(), 120}};
  for (int i = 0; i < 3; i++) {
    lv_obj_t* b = roundButton(powerObj, bs[i].icon, 96, bs[i].col, C_TEXT());
    lv_obj_align(b, LV_ALIGN_CENTER, bs[i].x, -10);
    lv_obj_t* l = label(powerObj, &font_text_20, C_DIM(), bs[i].text);
    lv_obj_align_to(l, b, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      int k = (int)(intptr_t)lv_event_get_user_data(e);
      if (k == 0) { audio::shutdown(); delay(50); hwPowerOff(); }
      else if (k == 1) ESP.restart();
      else powerClose();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}


// ------------------------------------------------------------------ Lautstärke

static lv_obj_t *volObj = nullptr, *volArc, *volNum, *volIcon;
static uint32_t volHideAt = 0;
static bool volDirty = false;

static void volRefresh() {
  lv_arc_set_value(volArc, cfg.volume);
  char b[8];
  snprintf(b, sizeof(b), "%d", cfg.volume);
  lv_label_set_text(volNum, b);
  lv_label_set_text(volIcon, cfg.volume == 0 ? I_VOL_OFF : cfg.volume < 40 ? I_VOL_DOWN : I_VOL_UP);
}

void volumeSet(int pct) {
  pct = kids::limitVolume(constrain(pct, 0, 100));
  if (pct == cfg.volume) return;
  cfg.volume = pct;
  audio::setVolume(pct);
  volDirty = true;
  if (volObj) volRefresh();
}

static void volClose() {
  if (volObj) { lv_obj_delete(volObj); volObj = nullptr; }
  if (volDirty) { volDirty = false; settingsSave(); }
}

void volumeShow(bool interactive) {
  volHideAt = millis() + (interactive ? 3500 : 1200);
  if (volObj) { volRefresh(); return; }
  volObj = overlay(C_BG(), interactive ? LV_OPA_80 : LV_OPA_TRANSP);
  if (interactive) lv_obj_add_event_cb(volObj, [](lv_event_t*) { volClose(); }, LV_EVENT_CLICKED, nullptr);
  else lv_obj_remove_flag(volObj, LV_OBJ_FLAG_CLICKABLE);

  volArc = lv_arc_create(volObj);
  lv_obj_set_size(volArc, 446, 446);
  lv_obj_center(volArc);
  lv_arc_set_bg_angles(volArc, 135, 45);
  lv_arc_set_range(volArc, 0, 100);
  lv_obj_set_style_arc_width(volArc, interactive ? 26 : 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(volArc, interactive ? 26 : 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(volArc, C_SURF2(), LV_PART_MAIN);
  lv_obj_set_style_arc_color(volArc, C_GOLD(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(volArc, C_TEXT(), LV_PART_KNOB);
  lv_obj_set_style_pad_all(volArc, 4, LV_PART_KNOB);
  if (!interactive) {
    lv_obj_set_style_opa(volArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(volArc, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_remove_flag(volArc, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(volArc, [](lv_event_t*) {
    volumeSet(lv_arc_get_value(volArc));
    volHideAt = millis() + 3500;
  }, LV_EVENT_VALUE_CHANGED, nullptr);

  // Mitte: dunkle Scheibe mit Symbol und Zahl (liegt beim Drehen über dem Cover)
  lv_obj_t* disc = lv_obj_create(volObj);
  lv_obj_set_size(disc, 210, 210);
  lv_obj_align(disc, LV_ALIGN_CENTER, 0, interactive ? -30 : 0);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(disc, C_BG(), 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_80, 0);
  lv_obj_set_style_border_width(disc, 0, 0);
  lv_obj_remove_flag(disc, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  volIcon = label(disc, &font_icon_40, C_GOLD(), "");
  lv_obj_align(volIcon, LV_ALIGN_CENTER, 0, -58);
  volNum = label(disc, &font_num_80, C_TEXT(), "");
  lv_obj_align(volNum, LV_ALIGN_CENTER, 0, 6);
  lv_obj_t* t = label(disc, &font_text_20, C_DIM(), "Lautstärke");
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 62);

  if (interactive) {
    for (int k = 0; k < 2; k++) {
      lv_obj_t* b = roundButton(volObj, k ? I_PLUS : I_MINUS, 72, C_SURF2(), C_TEXT());
      lv_obj_align(b, LV_ALIGN_CENTER, k ? 70 : -70, 128);
      // Tippen = ±5 %, Halten = läuft weiter
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        int dir = (int)(intptr_t)lv_event_get_user_data(e);
        volumeSet(cfg.volume + dir * (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED_REPEAT ? 2 : 5));
        volHideAt = millis() + 3500;
      }, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)(k ? 1 : -1));
      lv_obj_add_event_cb(b, [](lv_event_t* e) {
        int dir = (int)(intptr_t)lv_event_get_user_data(e);
        volumeSet(cfg.volume + dir * 2);
        volHideAt = millis() + 3500;
      }, LV_EVENT_LONG_PRESSED_REPEAT, (void*)(intptr_t)(k ? 1 : -1));
    }
  }
  volRefresh();
}

// Drehen am Rand: Winkeländerung → Lautstärke (eine volle Umdrehung = 0 … 100 %).
// Wischgesten beginnen oft am Rand – deshalb entscheidet erst die erste Bewegung: am Rand entlang = Drehen,
// zur Mitte hin oder quer = Wischen. Bis dahin passiert nichts.
static float bzLast = 0, bzAcc = 0;
static bool bzMaybe = false;
static lv_point_t bzStart;

static void bezelCb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  float dx = p.x - CX, dy = p.y - CY;
  float ang = atan2f(dx, -dy) * 57.2958f;
  if (code == LV_EVENT_PRESSED) {
    bool (*when)() = (bool (*)())lv_event_get_user_data(e);
    float rIn = min(W, H) / 2 - 48;  // nur der äußere Streifen
    bzMaybe = dx * dx + dy * dy > rIn * rIn && (!when || when());
    bezelActive = false;
    bzMoved = false;
    bzStart = p;
    bzAcc = 0;
  } else if (code == LV_EVENT_PRESSING && bzMaybe) {
    float mx = p.x - bzStart.x, my = p.y - bzStart.y;
    if (mx * mx + my * my < 18 * 18) return;
    bzMaybe = false;
    float ux = bzStart.x - CX, uy = bzStart.y - CY, ul = sqrtf(ux * ux + uy * uy);
    ux /= ul;
    uy /= ul;
    float radial = fabsf(mx * ux + my * uy), tangential = fabsf(mx * uy - my * ux);
    bezelActive = tangential > 2 * radial;
    bzLast = ang;
  } else if (code == LV_EVENT_PRESSING && bezelActive) {
    float d = ang - bzLast;
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    bzLast = ang;
    bzAcc += d;
    int steps = (int)(bzAcc / 3.6f);
    if (steps) {
      bzAcc -= steps * 3.6f;
      bzMoved = true;
      volumeSet(cfg.volume + steps);
      volumeShow(false);
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    bezelActive = bzMaybe = false;
  }
}

void bezel(lv_obj_t* obj, bool (*when)()) {
  // Druck festhalten, auch wenn der Finger am Rand über andere Elemente (Cover, Knöpfe) gleitet
  lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(obj, bezelCb, c, (void*)when);
}

// ------------------------------------------------------------------ Tastatur

static lv_obj_t* kbObj = nullptr;
static lv_obj_t* kbTa = nullptr;
static lv_obj_t* kbBm = nullptr;
static void (*kbDone)(const char*) = nullptr;
static bool kbShift = false;

// Große Tasten: oben 10, dann 9 Tasten pro Reihe, unten Abbrechen / Zahlen / Leerzeichen / Fertig.
// Umlaute und Satzzeichen auf der Zahlenseite („123“; mit ⇧ davor als Großbuchstaben).
#define KB_ROW4(k) "_", I_CLOSE, k, " ", I_CHECK, "_", ""
static const char* MAP_LOWER[] = {"q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "\n",
                                  "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
                                  I_SHIFT, "y", "x", "c", "v", "b", "n", "m", I_BACKSPACE, "\n",
                                  KB_ROW4("123")};
static const char* MAP_UPPER[] = {"Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "\n",
                                  "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
                                  I_SHIFT, "Y", "X", "C", "V", "B", "N", "M", I_BACKSPACE, "\n",
                                  KB_ROW4("123")};
static const char* MAP_NUM[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                                "ä", "ö", "ü", "ß", "@", "&", "-", "'", ".", "\n",
                                "!", "?", ",", ":", "/", "(", ")", "+", I_BACKSPACE, "\n",
                                KB_ROW4("abc")};
static const char* MAP_NUM_UP[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                                   "Ä", "Ö", "Ü", "ß", "@", "&", "-", "'", ".", "\n",
                                   "!", "?", ",", ":", "/", "(", ")", "+", I_BACKSPACE, "\n",
                                   KB_ROW4("abc")};
#define K(w) ((lv_buttonmatrix_ctrl_t)(w))
#define KH ((lv_buttonmatrix_ctrl_t)(1 | LV_BUTTONMATRIX_CTRL_HIDDEN))
#define KB_CTRL4 KH, K(2), K(2), K(5), K(2), KH
static const lv_buttonmatrix_ctrl_t CTRL_ALPHA[] = {K(1), K(1), K(1), K(1), K(1), K(1), K(1), K(1), K(1), K(1),  // q … p
                                                    K(1), K(1), K(1), K(1), K(1), K(1), K(1), K(1), K(1),        // a … l
                                                    K(3), K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(3),        // ⇧ y … m ⌫
                                                    KB_CTRL4};
static const lv_buttonmatrix_ctrl_t CTRL_NUM[] = {K(1), K(1), K(1), K(1), K(1), K(1), K(1), K(1), K(1), K(1),
                                                  K(1), K(1), K(1), K(1), K(1), K(1), K(1), K(1), K(1),
                                                  K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(3),
                                                  KB_CTRL4};

static void kbSetMap(const char** map) {
  lv_buttonmatrix_set_map(kbBm, map);
  lv_buttonmatrix_set_ctrl_map(kbBm, map == MAP_NUM || map == MAP_NUM_UP ? CTRL_NUM : CTRL_ALPHA);
}

static void kbClose(bool ok) {
  if (!kbObj) return;
  String text = lv_textarea_get_text(kbTa);
  lv_obj_delete(kbObj);
  kbObj = nullptr;
  if (ok && kbDone) kbDone(text.c_str());
}

static void kbKey(lv_event_t*) {
  uint32_t id = lv_buttonmatrix_get_selected_button(kbBm);
  const char* t = lv_buttonmatrix_get_button_text(kbBm, id);
  if (!t) return;
  play(audio::FX_TICK);
  // Schließen erst nach dem Ereignis (die Tastatur löscht sich dabei selbst)
  if (!strcmp(t, I_CHECK)) { lv_async_call([](void*) { kbClose(true); }, nullptr); return; }
  if (!strcmp(t, I_CLOSE)) { lv_async_call([](void*) { kbClose(false); }, nullptr); return; }
  if (!strcmp(t, I_BACKSPACE)) lv_textarea_delete_char(kbTa);
  else if (!strcmp(t, I_SHIFT)) { kbShift = !kbShift; kbSetMap(kbShift ? MAP_UPPER : MAP_LOWER); }
  else if (!strcmp(t, "123")) kbSetMap(kbShift ? MAP_NUM_UP : MAP_NUM);
  else if (!strcmp(t, "abc")) { kbShift = false; kbSetMap(MAP_LOWER); }
  else {
    lv_textarea_add_text(kbTa, t);
    if (kbShift) { kbShift = false; kbSetMap(MAP_LOWER); }
  }
}

void keyboard(const char* ttl, const char* initial, bool password, void (*done)(const char*)) {
  if (kbObj) lv_obj_delete(kbObj);
  kbDone = done;
  kbShift = false;
  kbObj = overlay(C_BG(), LV_OPA_COVER);
  lv_obj_t* t = label(kbObj, &font_text_20, C_DIM(), ttl);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 22);
  kbTa = lv_textarea_create(kbObj);
  lv_textarea_set_one_line(kbTa, true);
  lv_textarea_set_password_mode(kbTa, password);
  lv_textarea_set_password_show_time(kbTa, 1200);
  lv_textarea_set_text(kbTa, initial ? initial : "");
  lv_obj_set_size(kbTa, 296, 54);
  lv_obj_align(kbTa, LV_ALIGN_TOP_MID, 0, 52);
  lv_obj_set_style_text_font(kbTa, &font_text_24, 0);
  lv_obj_set_style_text_color(kbTa, C_TEXT(), 0);
  lv_obj_set_style_bg_color(kbTa, C_SURF(), 0);
  lv_obj_set_style_border_width(kbTa, 0, 0);
  lv_obj_set_style_radius(kbTa, 28, 0);
  lv_obj_set_style_pad_hor(kbTa, 20, 0);
  lv_obj_set_style_pad_ver(kbTa, 12, 0);
  lv_obj_add_state(kbTa, LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(kbTa, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(kbTa, C_GOLD(), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(kbTa, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_width(kbTa, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);

  kbBm = lv_buttonmatrix_create(kbObj);
  lv_obj_set_size(kbBm, 404, 290);
  lv_obj_align(kbBm, LV_ALIGN_TOP_MID, 0, 114);
  lv_obj_set_style_bg_opa(kbBm, 0, 0);
  lv_obj_set_style_border_width(kbBm, 0, 0);
  lv_obj_set_style_pad_all(kbBm, 0, 0);
  lv_obj_set_style_pad_gap(kbBm, 6, 0);
  lv_obj_set_style_bg_color(kbBm, C_SURF(), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kbBm, C_SURF2(), (lv_style_selector_t)(LV_PART_ITEMS | LV_STATE_PRESSED));
  lv_obj_set_style_radius(kbBm, 14, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kbBm, 0, LV_PART_ITEMS);
  lv_obj_set_style_text_font(kbBm, &font_text_24, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kbBm, C_TEXT(), LV_PART_ITEMS);
  kbSetMap(MAP_LOWER);
  lv_obj_add_event_cb(kbBm, kbKey, LV_EVENT_VALUE_CHANGED, nullptr);
  // „Fertig“ grün hervorheben
  lv_obj_add_event_cb(kbBm, [](lv_event_t* e) {
    lv_draw_task_t* dt = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t* base = (lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(dt);
    if (base->part != LV_PART_ITEMS) return;
    const char* txt = lv_buttonmatrix_get_button_text(kbBm, base->id1);
    if (!txt || strcmp(txt, I_CHECK)) return;
    lv_draw_fill_dsc_t* f = lv_draw_task_get_fill_dsc(dt);
    if (f) f->color = C_RADIO();
    lv_draw_label_dsc_t* l = lv_draw_task_get_label_dsc(dt);
    if (l) l->color = C_BG();
  }, LV_EVENT_DRAW_TASK_ADDED, nullptr);
  lv_obj_add_flag(kbBm, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
}

// ------------------------------------------------------------------ Tasten und Takt

void onButton(BtnEvent e) {
  if (findObj && e == BOOT_SHORT) { findMe(false); return; }
  switch (e) {
    case BOOT_SHORT:
      if (powerObj) powerClose();
      else if (kbObj) kbClose(false);
      else if (volObj) volClose();
      // wie die Krone: auf dem Zifferblatt die Apps, sonst zurück zum Zifferblatt
      else if (top() && top()->watchFace) push(&PAGE_APPS);
      else home();
      break;
    case PWR_LONG:
      powerDialog();
      break;
    default:
      break;
  }
}

void tick() {
  static uint32_t at = 0;
  findTick();
  if (volObj && !bezelActive && millis() > volHideAt && !(volArc && lv_obj_has_state(volArc, LV_STATE_PRESSED)))
    volClose();
  if (millis() - at >= 100) {
    at = millis();
    const Page* p = top();
    if (p && p->update) p->update();
  }
}

// Display wieder an: Eltern-Menü und offene Freigabe-Dialoge sind dann zu – sonst fände das Kind sie offen vor
void wake() {
  closeLockDialogs();
  for (int i = 0; i < depth; i++)
    if (stack[i].page == &PAGE_PARENT) { home(); return; }
  const Page* p = top();
  if (p && p->resume) p->resume();
}

void begin() {
  lv_theme_t* th = lv_theme_default_init(lv_display_get_default(), C_GOLD(), C_RADIO(), true, &font_text_20);
  lv_display_set_theme(lv_display_get_default(), th);
  lv_obj_set_style_bg_opa(lv_layer_top(), 0, 0);
  push(&PAGE_FACE);
}

}  // namespace ui
