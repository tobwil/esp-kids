// Apps: Balance, Radio und Hörspiele – jede mit ihrem Stand („Freigabe nötig“, „noch 12 Min.“, „Für heute
// aufgebraucht“). Öffnen: auf dem Zifferblatt hochwischen oder BOOT drücken. Schließen: runter- oder nach rechts wischen.
#include <time.h>
#include "../kids.h"
#include "../music.h"
#include "../net.h"
#include "ui.h"

using namespace ui;

static lv_obj_t *timeL, *list, *nowRow;
static lv_obj_t* rows[quota::APPS];
static uint32_t strokeSeen = 0, strokeDone = 0;
static int32_t strokeScroll = 0;

static void statusText(quota::App a, char* b, size_t n) {
  switch (quota::status(a)) {
    case quota::NIGHT: snprintf(b, n, "Schlafenszeit"); break;
    case quota::USED_UP: snprintf(b, n, "Für heute aufgebraucht"); break;
    case quota::LOCKED: snprintf(b, n, I_LOCK " Eltern fragen"); break;
    default: {
      long left = quota::leftS(a);
      if (left < 0) snprintf(b, n, "frei");
      else fmtLeft(b, n, left);
    }
  }
}

static void refresh() {
  char b[48];
  for (int a = 0; a < quota::APPS; a++) {
    statusText((quota::App)a, b, sizeof(b));
    listRowSub(rows[a], b);
    bool open = quota::status((quota::App)a) == quota::OPEN;
    lv_obj_set_style_opa(lv_obj_get_child(rows[a], 0), open ? LV_OPA_COVER : LV_OPA_50, 0);
  }
  hide(nowRow, !music::state().active);
}

static void build(lv_obj_t* scr) {
  lv_obj_t* l = list = roundList(scr, 56, 40);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLL_ELASTIC);  // oben angekommen: runterwischen schließt
  timeL = label(l, &font_text_24, C_TEXT(), "");
  lv_obj_set_style_text_align(timeL, LV_TEXT_ALIGN_CENTER, 0);
  nowRow = listRow(l, I_MUSIC, C_STORY(), "Läuft gerade");
  lv_obj_add_event_cb(nowRow, [](lv_event_t*) { play(0); push(&PAGE_MUSIC); }, LV_EVENT_CLICKED, nullptr);
  for (int a = 0; a < quota::APPS; a++) {
    lv_color_t c = appColor((quota::App)a);
    lv_obj_t* r = rows[a] = listRow(l, nullptr, c, quota::name((quota::App)a), " ");
    lv_obj_set_height(r, 84);
    // farbiger Kreis mit Symbol vor dem Namen
    lv_obj_t* dot = lv_obj_create(r);
    lv_obj_move_to_index(dot, 0);
    lv_obj_set_size(dot, 58, 58);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_mix(c, C_BG(), 70), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_remove_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_t* ic = label(dot, &font_text_32, c, appIcon((quota::App)a));
    lv_obj_center(ic);
    lv_obj_set_style_pad_left(r, 12, 0);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      play(0);
      openApp((quota::App)(intptr_t)lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, (void*)(intptr_t)a);
  }
  if (kids::night()) {
    char b[64];
    snprintf(b, sizeof(b), I_SLEEP "  Gute Nacht!\nBis morgen um %d:%02d", kids::cfg().wake / 60, kids::cfg().wake % 60);
    lv_obj_t* g = label(l, &font_text_24, C_NIGHT(), b);
    lv_obj_set_style_text_align(g, LV_TEXT_ALIGN_CENTER, 0);
  }
  refresh();
  listFit(l);
  int dx, dy;
  bool released;
  hwStroke(strokeSeen, dx, dy, released);  // der Wisch, der diese Seite geöffnet hat, zählt nicht
  strokeDone = strokeSeen;
}

// Oben angekommen schließt Runterwischen. LVGL meldet die Geste nicht, solange die Liste die Berührung zum Rollen
// übernimmt – deshalb den ganzen Wisch vom Touch-Treiber nehmen.
static void swipeClose() {
  uint32_t id;
  int dx, dy;
  bool released;
  hwStroke(id, dx, dy, released);
  if (id != strokeSeen) {
    strokeSeen = id;
    strokeScroll = lv_obj_get_scroll_y(list);
  }
  if (!released || id == strokeDone) return;
  strokeDone = id;
  if (strokeScroll <= 4 && dy > 60 && abs(dx) < dy) back();
}

static void update() {
  swipeClose();
  if (top() != &PAGE_APPS) return;
  char b[8] = "--:--";
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(b, sizeof(b), "%d:%02d", t.tm_hour, t.tm_min);
  }
  if (strcmp(lv_label_get_text(timeL), b)) lv_label_set_text(timeL, b);
  static uint32_t at = 0;
  if (millis() - at > 1000) { at = millis(); refresh(); }
}

static void resume() { refresh(); listFit(list); }

const Page PAGE_APPS = {build, update, nullptr, resume, false, ENTER_BOTTOM};
