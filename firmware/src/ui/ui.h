// Oberfläche: Seiten-Stapel, Farben und Bausteine – für den runden 466er und den eckigen 480er Bildschirm.
#pragma once
#include <lvgl.h>
#include "../fonts/fonts.h"
#include "../hw.h"
#include "../quota.h"
#include "pins.h"

namespace ui {

// ------------------------------------------------------------------ Farben (AMOLED: Schwarz = aus)
inline lv_color_t C_BG() { return lv_color_hex(0x000000); }
inline lv_color_t C_SURF() { return lv_color_hex(0x17171B); }
inline lv_color_t C_SURF2() { return lv_color_hex(0x26262C); }
inline lv_color_t C_TEXT() { return lv_color_hex(0xF4F1EA); }
inline lv_color_t C_DIM() { return lv_color_hex(0x8E8A82); }
inline lv_color_t C_GOLD() { return lv_color_hex(0xFFC83A); }    // Balance, Akzente
inline lv_color_t C_RADIO() { return lv_color_hex(0x3DDC97); }   // Radio
inline lv_color_t C_STORY() { return lv_color_hex(0xFF7A59); }   // Hörspiele
inline lv_color_t C_SKY() { return lv_color_hex(0x63B3FF); }     // Eltern, WLAN
inline lv_color_t C_NIGHT() { return lv_color_hex(0xB592FF); }   // Schlafenszeit
inline lv_color_t C_RED() { return lv_color_hex(0xFF453A); }
lv_color_t appColor(quota::App a);
const char* appIcon(quota::App a);

static const int W = LCD_W, H = LCD_H, CX = LCD_W / 2, CY = LCD_H / 2;
static const bool SQUARE = BOARD_SQUARE;  // eckiges Display: Listen über die volle Breite

// ------------------------------------------------------------------ Seiten
struct Page {
  void (*build)(lv_obj_t* scr);
  void (*update)();             // ca. 10 × pro Sekunde, solange die Seite oben liegt
  void (*leave)();              // beim Verlassen (zurück)
  void (*resume)();             // wieder oben (nach „zurück“ von einer Unterseite)
  bool watchFace;               // Startseite (eigene Gesten)
  uint8_t enter;                // Einflug: 0 = von rechts, 1 = von unten (Apps), 2 = von oben (Kontrollzentrum)
};
enum : uint8_t { ENTER_RIGHT = 0, ENTER_BOTTOM = 1, ENTER_TOP = 2 };

void begin();
void tick();                    // aus der Hauptschleife
void push(const Page* p);
void back();
void home();
const Page* top();
lv_obj_t* topScreen();
void show(const Page* p);      // zur Seite zurück, falls sie schon offen ist, sonst öffnen
void onButton(BtnEvent e);
void wake();                    // Display ging an

// ------------------------------------------------------------------ Bausteine
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text = "");
lv_obj_t* title(lv_obj_t* scr, const char* text, lv_color_t color);  // oben mittig
lv_obj_t* roundButton(lv_obj_t* parent, const char* icon, int size, lv_color_t bg, lv_color_t fg,
                      const lv_font_t* font = &font_icon_40);
lv_obj_t* pill(lv_obj_t* parent, const char* text, lv_color_t bg, lv_color_t fg);

// Liste, die sich der Rundung anpasst (Zeilen werden zum Rand hin schmaler)
lv_obj_t* roundList(lv_obj_t* scr, int top = 96, int bottom = 70);
lv_obj_t* listRow(lv_obj_t* list, const char* icon, lv_color_t iconColor, const char* text, const char* sub = nullptr);
void listRowSub(lv_obj_t* row, const char* sub);
void listTitle(lv_obj_t* list, const char* text, lv_color_t c);
void listFit(lv_obj_t* list);   // nach dem Füllen aufrufen

// Hinweise und Dialoge (oberste Ebene)
void toast(const char* text, uint32_t ms = 2500);
lv_obj_t* overlay(lv_color_t bg, lv_opa_t opa);  // ganzer Bildschirm auf der obersten Ebene
void keyboard(const char* title, const char* initial, bool password, void (*done)(const char* text));
void powerDialog();
// Uhr finden (aus der Weboberfläche): 30 s klingeln mit „Hier bin ich!“, Antippen oder BOOT beendet
void findMe(bool on, int volume = 70);

// Eltern-PIN abfragen: Ziffern jedes Mal anders angeordnet (Mitlesen über die Schulter hilft nicht), nach drei
// falschen Versuchen eine Minute Pause. ok läuft nach der richtigen PIN. Ohne gesetzte PIN: gleich ok.
void askPin(const char* why, void (*ok)());
void newPin();                  // neue Eltern-PIN an der Uhr festlegen (zweimal eingeben)
// App öffnen: frei → gleich los; gesperrt → Eltern-PIN, dann wie lange freigeben
void openApp(quota::App a);
void unlockDialog(quota::App a);  // „wie lange freigeben?“ (Eltern-Menü, PIN schon eingegeben)
void closeLockDialogs();          // PIN-Feld und „wie lange?“ schließen (Display ging an)

// Lautstärke: großer Regler (interactive) bzw. nur Anzeige beim Drehen am Rand
void volumeShow(bool interactive);
void volumeSet(int pct);
// „Am Rand drehen“ = Lautstärke: Finger am Bildschirmrand entlang ziehen (im Uhrzeigersinn lauter).
// when: nur aktiv, wenn die Funktion true liefert (nullptr = immer)
void bezel(lv_obj_t* obj, bool (*when)() = nullptr);
bool bezelMoved();              // letzter Druck hat am Rand gedreht (dann kein „Antippen“ auslösen)

// Kurzbefehle
inline void hide(lv_obj_t* o, bool h) {
  if (h) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}
void play(uint8_t fx);          // Klick-Klang
void fmtLeft(char* out, size_t n, long s);  // „noch 12 Min.“, „noch 1 Std. 5 Min.“

}  // namespace ui

// Seiten (in den einzelnen Dateien)
extern const ui::Page PAGE_FACE, PAGE_APPS, PAGE_CONTROL, PAGE_GAME, PAGE_MUSIC, PAGE_BROWSE, PAGE_PARENT;
void browseOpen(const char* path);  // Liste der Uhr öffnen („radio“, „stories“ …)
// Balance (ui_game.cpp)
void gameRecords(int& lvl, uint32_t& ms);  // Rekorde: höchste Stufe, schnellste Zeit „Alle Kugeln“ (0 = keine)
void gameResetRecords();
void gameTestLevel(int level);     // Stufe direkt zeigen (Bildschirmfotos)
// Zifferblatt (ui_face.cpp): Testwetter für Bildschirmfotos, code < 0 = echtes Wetter
void faceTest(int code, int temp, bool day);
