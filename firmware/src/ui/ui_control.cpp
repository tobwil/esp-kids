// Kontrollzentrum wie bei Wear OS / watchOS: vom Zifferblatt herunterwischen, hochwischen schließt.
// Oben Uhrzeit, Akku und WLAN, darunter Lautstärke, Helligkeit und Display aus.
#include <time.h>
#include "../net.h"
#include "../power.h"
#include "../settings.h"
#include "ui.h"

using namespace ui;

static lv_obj_t *timeL, *statL, *hintL;

static void build(lv_obj_t* scr) {
  timeL = label(scr, &font_text_24, C_TEXT(), "");
  lv_obj_align(timeL, LV_ALIGN_TOP_MID, 0, 50);
  statL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(statL, LV_ALIGN_TOP_MID, 0, 86);

  lv_obj_t* row = lv_obj_create(scr);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 350, 110);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, 6);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 20, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* vol = roundButton(row, I_VOL_UP, 96, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(vol, [](lv_event_t*) { volumeShow(true); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* br = roundButton(row, I_BRIGHT, 96, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(br, [](lv_event_t*) {
    // reihum 20 → 50 → 80 → 100 %
    cfg.brightness = cfg.brightness < 20 ? 20 : cfg.brightness < 50 ? 50 : cfg.brightness < 80 ? 80 : cfg.brightness < 100 ? 100 : 20;
    hwBrightness(cfg.brightness);
    settingsSave();
    char b[32];
    snprintf(b, sizeof(b), "Helligkeit %d %%", cfg.brightness);
    lv_label_set_text(hintL, b);
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* off = roundButton(row, I_SCREEN_OFF, 96, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(off, [](lv_event_t*) {
    home();
    hwScreen(false);
  }, LV_EVENT_CLICKED, nullptr);

  hintL = label(scr, &font_text_20, C_DIM(), "Hochwischen zum Schließen");
  lv_obj_align(hintL, LV_ALIGN_BOTTOM_MID, 0, -60);
}

static void update() {
  char b[64] = "--:--";
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(b, sizeof(b), "%d:%02d", t.tm_hour, t.tm_min);
  }
  if (strcmp(lv_label_get_text(timeL), b)) lv_label_set_text(timeL, b);
  int bat = hwBattery();
  char batS[24];
  if (bat >= 0) snprintf(batS, sizeof(batS), "%s %d %%", hwCharging() ? I_CHARGING : I_BATTERY, bat);
  else snprintf(batS, sizeof(batS), I_BOLT " USB");
  snprintf(b, sizeof(b), "%s%s   %s", batS, pwr::saving() ? " · Sparen" : "", netOnline() ? I_WIFI : I_WIFI_OFF);
  if (strcmp(lv_label_get_text(statL), b)) lv_label_set_text(statL, b);
}

const Page PAGE_CONTROL = {build, update, nullptr, nullptr, false, ENTER_TOP};
