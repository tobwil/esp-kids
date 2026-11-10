// Eltern-PIN an der Uhr und Freigaben: Ziffernfeld mit jedes Mal anders verteilten Ziffern, danach „wie lange?“.
#include <esp_random.h>
#include <time.h>
#include "../audio.h"
#include "../kids.h"
#include "../quota.h"
#include "ui.h"

namespace ui {

static lv_obj_t* padObj = nullptr;
static lv_obj_t* dotsL = nullptr;
static char entered[9];
static uint8_t digitAt[10];   // Ziffer je Taste
static void (*padOk)() = nullptr;
static void (*padNew)(const char* pin) = nullptr;  // neue PIN festlegen statt prüfen
static uint8_t wrong = 0;
static uint32_t lockedUntil = 0;

// Schließen auch aus einem Ereignis der eigenen Tasten heraus: LVGL löscht nach dem Ereignis. Nicht mit lv_async_call –
// verzögerte Aufrufe laufen in umgekehrter Reihenfolge, ein gleich danach geöffnetes Feld würde wieder geschlossen.
static void padClose() {
  if (padObj) { lv_obj_delete_async(padObj); padObj = nullptr; }
}

static void shuffle() {
  for (int i = 0; i < 10; i++) digitAt[i] = i;
  for (int i = 9; i > 0; i--) {
    int k = esp_random() % (i + 1);
    uint8_t t = digitAt[i];
    digitAt[i] = digitAt[k];
    digitAt[k] = t;
  }
}

// ein Kreis je Ziffer der PIN: gefüllt = schon getippt
static void showDots() {
  int want = padNew ? 4 : (int)strlen(kids::cfg().pin), have = strlen(entered);
  while ((int)lv_obj_get_child_count(dotsL) < want) {
    lv_obj_t* d = lv_obj_create(dotsL);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 16, 16);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 2, 0);
    lv_obj_set_style_border_color(d, C_DIM(), 0);
  }
  for (int i = 0; i < (int)lv_obj_get_child_count(dotsL); i++)
    lv_obj_set_style_bg_color(lv_obj_get_child(dotsL, i), i < have ? C_TEXT() : C_BG(), 0);
}

static void padBuild(const char* why);

static void digit(int d) {
  size_t n = strlen(entered);
  if (n >= 8) return;
  entered[n] = '0' + d;
  entered[n + 1] = 0;
  showDots();
  if (padNew) {
    if (strlen(entered) < 4) return;
    char pin[9];
    snprintf(pin, sizeof(pin), "%s", entered);
    void (*done)(const char*) = padNew;
    padClose();
    done(pin);
    return;
  }
  if (strlen(entered) < strlen(kids::cfg().pin)) return;
  if (kids::pinOk(entered)) {
    wrong = 0;
    void (*ok)() = padOk;
    padClose();
    audio::play(audio::FX_OK);
    if (ok) ok();
    return;
  }
  audio::play(audio::FX_ERROR);
  entered[0] = 0;
  if (++wrong >= 3) {
    wrong = 0;
    lockedUntil = millis() + 60000;
    toast("Dreimal falsch – eine Minute Pause");
    padClose();
    return;
  }
  toast("Falsche PIN");
  shuffle();  // neue Anordnung
  for (int i = 0; i < 10; i++) {
    lv_obj_t* b = lv_obj_get_child(lv_obj_get_child(padObj, 2), i < 9 ? i : 10);
    char t[2] = {(char)('0' + digitAt[i]), 0};
    lv_label_set_text(lv_obj_get_child(b, 0), t);
  }
  showDots();
}

static void padBuild(const char* why) {
  padClose();
  entered[0] = 0;
  shuffle();
  padObj = overlay(C_BG(), LV_OPA_COVER);
  lv_obj_add_flag(padObj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* t = label(padObj, &font_text_20, C_DIM(), why);  // Kind 0
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 44);
  dotsL = lv_obj_create(padObj);                            // Kind 1
  lv_obj_remove_style_all(dotsL);
  lv_obj_set_size(dotsL, LV_SIZE_CONTENT, 20);
  lv_obj_set_flex_flow(dotsL, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(dotsL, 14, 0);
  lv_obj_align(dotsL, LV_ALIGN_TOP_MID, 0, 86);
  lv_obj_t* grid = lv_obj_create(padObj);                    // Kind 2
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, 3 * 70 + 2 * 10, 4 * 66 + 3 * 8);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 122);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(grid, 10, 0);
  lv_obj_set_style_pad_row(grid, 8, 0);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  // 3 × 3 Ziffern, unten: Abbrechen · Ziffer · Löschen
  for (int k = 0; k < 12; k++) {
    int slot = k < 9 ? k : k == 10 ? 9 : -1;
    const char* txt = k == 9 ? I_CLOSE : k == 11 ? I_BACKSPACE : "";
    lv_obj_t* b = lv_button_create(grid);
    lv_obj_set_size(b, 70, 66);
    lv_obj_set_style_radius(b, 33, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, slot >= 0 ? C_SURF2() : C_SURF(), 0);
    lv_obj_set_style_bg_color(b, C_DIM(), LV_STATE_PRESSED);
    char d[2] = {(char)('0' + (slot >= 0 ? digitAt[slot] : 0)), 0};
    lv_obj_t* l = label(b, &font_text_32, C_TEXT(), slot >= 0 ? d : txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t* e) {
      int k = (int)(intptr_t)lv_event_get_user_data(e);
      audio::play(audio::FX_TICK);
      if (k == 9) { padClose(); return; }
      if (k == 11) { size_t n = strlen(entered); if (n) entered[n - 1] = 0; showDots(); return; }
      digit(digitAt[k < 9 ? k : 9]);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)k);
  }
  showDots();
}

void askPin(const char* why, void (*ok)()) {
  if (!kids::hasPin()) { ok(); return; }
  if (lockedUntil && (int32_t)(millis() - lockedUntil) < 0) { toast("Noch gesperrt – bitte kurz warten"); return; }
  padOk = ok;
  padNew = nullptr;
  padBuild(why);
}

// neue PIN an der Uhr (Eltern-Menü): viermal tippen, dann zur Bestätigung noch einmal
static char firstPin[9];
void newPin() {
  padNew = [](const char* p) {
    snprintf(firstPin, sizeof(firstPin), "%s", p);
    padNew = [](const char* p2) {
      if (strcmp(p2, firstPin)) { toast("Nicht gleich – bitte noch einmal"); audio::play(audio::FX_ERROR); return; }
      kids::Cfg c = kids::cfg();
      snprintf(c.pin, sizeof(c.pin), "%s", p2);
      kids::set(c);
      audio::play(audio::FX_OK);
      toast("Eltern-PIN gespeichert");
    };
    padBuild("PIN wiederholen");
  };
  padOk = nullptr;
  padBuild("Neue PIN (4 Ziffern)");
}

// ------------------------------------------------------------------ Freigeben: wie lange?

static lv_obj_t* durObj = nullptr;
static quota::App durApp;

static void durClose() {
  if (durObj) { lv_obj_delete_async(durObj); durObj = nullptr; }
}

static void openPage(quota::App a) {
  if (a == quota::GAME) push(&PAGE_GAME);
  else browseOpen(a == quota::RADIO ? "radio" : "stories");
}

static void durationDialog() {
  durClose();
  quota::App a = durApp;
  durObj = overlay(C_BG(), LV_OPA_COVER);
  lv_obj_add_flag(durObj, LV_OBJ_FLAG_CLICKABLE);
  char b[64];
  snprintf(b, sizeof(b), "%s freigeben", quota::name(a));
  lv_obj_t* t = label(durObj, &font_text_24, appColor(a), b);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 56);
  int lim = quota::limitMin(a), used = quota::usedMin(a);
  if (lim) snprintf(b, sizeof(b), "Heute %d von %d Min. genutzt", used, lim);
  else snprintf(b, sizeof(b), "Heute %d Min. genutzt", used);
  lv_obj_t* u = label(durObj, &font_text_20, C_DIM(), b);
  lv_obj_align(u, LV_ALIGN_TOP_MID, 0, 92);
  static const int MIN[] = {15, 30, 60, quota::UNTIL_BED};
  static const char* const TXT[] = {"15 Min.", "30 Min.", "1 Std.", "bis Bettzeit"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t* p = pill(durObj, TXT[i], i == 3 ? C_SURF2() : appColor(a), i == 3 ? C_TEXT() : C_BG());
    lv_obj_set_size(p, 150, 64);
    lv_obj_set_style_radius(p, 32, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(p, 0), &font_text_24, 0);
    lv_obj_align(p, LV_ALIGN_CENTER, i % 2 ? 82 : -82, i < 2 ? -36 : 40);
    lv_obj_add_event_cb(p, [](lv_event_t* e) {
      int m = MIN[(int)(intptr_t)lv_event_get_user_data(e)];
      quota::unlock(durApp, m, true);
      quota::App a = durApp;
      durClose();
      char b[64];
      long left = quota::leftS(a);
      if (left < 0) snprintf(b, sizeof(b), "%s ist frei", quota::name(a));
      else fmtLeft(b, sizeof(b), left);
      toast(b);
      if (top() == &PAGE_PARENT) { if (PAGE_PARENT.resume) PAGE_PARENT.resume(); }
      else openPage(a);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  bool open = quota::until(a) != 0;
  lv_obj_t* x = pill(durObj, open ? I_LOCK "  Sperren" : "Abbrechen", C_SURF(), C_TEXT());
  lv_obj_align(x, LV_ALIGN_CENTER, 0, 124);
  lv_obj_add_event_cb(x, [](lv_event_t* e) {
    if (lv_event_get_user_data(e)) {
      quota::lock(durApp);
      toast("Gesperrt");
      if (top() == &PAGE_PARENT && PAGE_PARENT.resume) PAGE_PARENT.resume();
    }
    durClose();
  }, LV_EVENT_CLICKED, (void*)(intptr_t)open);
}

void closeLockDialogs() {
  padClose();
  durClose();
}

void unlockDialog(quota::App a) {
  durApp = a;
  durationDialog();
}

void openApp(quota::App a) {
  switch (quota::status(a)) {
    case quota::OPEN:
      openPage(a);
      return;
    case quota::NIGHT: {
      char b[64];
      snprintf(b, sizeof(b), "Schlafenszeit – morgen ab %d:%02d", kids::cfg().wake / 60, kids::cfg().wake % 60);
      toast(b);
      return;
    }
    default:
      break;
  }
  if (!kids::hasPin()) { toast("Erst eine Eltern-PIN festlegen (Zifferblatt 1,5 s gedrückt halten)", 3500); return; }
  durApp = a;
  char b[48];
  snprintf(b, sizeof(b), quota::status(a) == quota::USED_UP ? "Für heute aufgebraucht · Eltern-PIN" : "%s · Eltern-PIN",
           quota::name(a));
  askPin(b, durationDialog);
}

}  // namespace ui
