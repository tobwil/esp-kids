// Hören: „Läuft gerade“ (Cover, Fortschritt am Rand, Steuerung, Restzeit) und die Listen der freigegebenen Sender,
// Sendungen und Folgen.
#include "../music.h"
#include "../quota.h"
#include "ui.h"

using namespace ui;

// ================================================================== Läuft gerade

static lv_obj_t* phIcon;  // Symbol im Cover-Platzhalter
static lv_obj_t* ring;      // Fortschritt am Rand
static lv_obj_t* cover;
static lv_obj_t* coverPh;   // Platzhalter ohne Cover
static lv_obj_t* titleL;
static lv_obj_t* artistL;
static lv_obj_t* playB;
static lv_obj_t* emptyBox;
static lv_obj_t* ctrlBox;
static lv_obj_t* hintL;
static lv_obj_t* sleepB;    // Sleep-Timer
static lv_obj_t* leftL;     // Restzeit
static lv_image_dsc_t artDsc;
static uint32_t artSeen = 0xFFFFFFFF;
static char lastId[16];
static bool hintShown = false;

static void build(lv_obj_t* scr) {
  ring = lv_arc_create(scr);
  lv_obj_set_size(ring, 456, 456);
  lv_obj_center(ring);
  lv_arc_set_rotation(ring, 270);
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_arc_set_range(ring, 0, 1000);
  lv_obj_set_style_arc_width(ring, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, C_SURF2(), LV_PART_MAIN);
  lv_obj_set_style_arc_color(ring, C_STORY(), LV_PART_INDICATOR);
  lv_obj_set_style_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

  // Cover (oder Platzhalter); Antippen öffnet den Lautstärkeregler
  coverPh = lv_obj_create(scr);
  lv_obj_set_size(coverPh, music::ART, music::ART);
  lv_obj_align(coverPh, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_style_radius(coverPh, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(coverPh, C_SURF(), 0);
  lv_obj_set_style_border_width(coverPh, 0, 0);
  lv_obj_remove_flag(coverPh, LV_OBJ_FLAG_SCROLLABLE);
  phIcon = label(coverPh, &font_icon_64, C_SURF2(), I_ALBUM);
  lv_obj_center(phIcon);
  cover = lv_image_create(scr);
  lv_obj_align(cover, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_add_flag(cover, LV_OBJ_FLAG_HIDDEN);
  for (lv_obj_t* o : {coverPh, (lv_obj_t*)cover}) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, [](lv_event_t*) { volumeShow(true); }, LV_EVENT_CLICKED, nullptr);
  }

  titleL = label(scr, &font_text_24, C_TEXT(), "");
  lv_label_set_long_mode(titleL, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(titleL, 330);
  lv_obj_set_style_text_align(titleL, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(titleL, LV_ALIGN_TOP_MID, 0, 264);
  artistL = label(scr, &font_text_20, C_DIM(), "");
  lv_label_set_long_mode(artistL, LV_LABEL_LONG_DOT);
  lv_obj_set_size(artistL, 300, lv_font_get_line_height(&font_text_20));
  lv_obj_set_style_text_align(artistL, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(artistL, LV_ALIGN_TOP_MID, 0, 297);

  // Steuerleiste: Bibliothek · zurück · Pause · weiter · Lautstärke
  ctrlBox = lv_obj_create(scr);
  lv_obj_remove_style_all(ctrlBox);
  lv_obj_set_size(ctrlBox, 370, 92);
  lv_obj_align(ctrlBox, LV_ALIGN_TOP_MID, 0, 330);
  lv_obj_set_flex_flow(ctrlBox, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctrlBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ctrlBox, 10, 0);
  lv_obj_t* lib = roundButton(ctrlBox, I_LIBRARY, 52, C_BG(), C_DIM(), &font_text_32);
  lv_obj_add_event_cb(lib, [](lv_event_t*) { browseOpen(music::state().live ? "radio" : "stories"); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pv = roundButton(ctrlBox, I_PREV, 62, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(pv, [](lv_event_t*) { music::prev(); }, LV_EVENT_CLICKED, nullptr);
  playB = roundButton(ctrlBox, I_PAUSE, 86, C_STORY(), C_BG(), &font_icon_64);
  lv_obj_add_event_cb(playB, [](lv_event_t*) { music::toggle(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* nx = roundButton(ctrlBox, I_NEXT, 62, C_SURF(), C_TEXT());
  lv_obj_add_event_cb(nx, [](lv_event_t*) { music::next(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* vol = roundButton(ctrlBox, I_VOL_UP, 52, C_BG(), C_DIM(), &font_text_32);
  lv_obj_add_event_cb(vol, [](lv_event_t*) { volumeShow(true); }, LV_EVENT_CLICKED, nullptr);

  // Sleep-Timer: Antippen schaltet 15 → 30 → 45 → 60 → 90 Min → aus
  sleepB = pill(scr, I_SLEEP, C_BG(), C_DIM());
  lv_obj_set_style_text_font(lv_obj_get_child(sleepB, 0), &font_text_20, 0);
  lv_obj_set_style_pad_ver(sleepB, 4, 0);
  lv_obj_align(sleepB, LV_ALIGN_BOTTOM_MID, 60, -14);
  // Restzeit der Freigabe bzw. des Tageslimits
  leftL = label(scr, &font_text_20, C_DIM(), "");
  lv_obj_align(leftL, LV_ALIGN_BOTTOM_MID, -50, -24);
  lv_obj_add_event_cb(sleepB, [](lv_event_t*) {
    static const int STEPS[] = {15, 30, 45, 60, 90, 0};
    int left = (music::sleepLeftS() + 59) / 60, next = 15;
    for (int i = 0; i < 6; i++) if (left > 0 && left <= STEPS[i]) { next = STEPS[(i + 1) % 6]; break; }
    if (!left) next = 15;
    music::sleepSet(next);
    play(next ? 1 : 0);
    char b[48];
    if (next) snprintf(b, sizeof(b), "Musik geht in %d Min. aus", next);
    else snprintf(b, sizeof(b), "Sleep-Timer aus");
    toast(b, 1800);
  }, LV_EVENT_CLICKED, nullptr);

  hintL = label(scr, &font_text_20, C_GOLD(), "Am Rand drehen = Lautstärke");
  lv_obj_align(hintL, LV_ALIGN_BOTTOM_MID, 0, -22);
  hide(hintL, hintShown);
  hintShown = true;
  lv_timer_t* ht = lv_timer_create([](lv_timer_t* t) {
    lv_obj_t* l = (lv_obj_t*)lv_timer_get_user_data(t);
    if (lv_obj_is_valid(l)) hide(l, true);
  }, 5000, hintL);
  lv_timer_set_repeat_count(ht, 1);

  // nichts läuft
  emptyBox = lv_obj_create(scr);
  lv_obj_remove_style_all(emptyBox);
  lv_obj_set_size(emptyBox, 340, 200);
  lv_obj_align(emptyBox, LV_ALIGN_TOP_MID, 0, 262);
  lv_obj_set_flex_flow(emptyBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(emptyBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(emptyBox, 16, 0);
  label(emptyBox, &font_text_24, C_TEXT(), "Gerade läuft nichts");
  lv_obj_t* go = pill(emptyBox, I_LIBRARY "  Zu den Apps", C_STORY(), C_BG());
  lv_obj_add_event_cb(go, [](lv_event_t*) { home(); push(&PAGE_APPS); }, LV_EVENT_CLICKED, nullptr);

  bezel(scr, []() { return music::state().active; });
  artSeen = 0xFFFFFFFF;
  lastId[0] = 0;
}

static void update() {
  music::State s = music::state();
  // „Nichts läuft“: nur Platzhalter und Bibliothek-Knopf, keine Steuerung
  hide(emptyBox, s.active);
  hide(ctrlBox, !s.active);
  hide(ring, !s.active);
  if (!s.active) hide(hintL, true);
  if (strcmp(lastId, s.id)) {
    snprintf(lastId, sizeof(lastId), "%s", s.id);
    lv_label_set_text(titleL, s.active ? s.title : "");
    lv_label_set_text(phIcon, s.live ? I_RADIO : I_ALBUM);  // Radio ohne Logo
    lv_obj_add_flag(cover, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(coverPh, LV_OBJ_FLAG_HIDDEN);
  }
  // Hinweis statt Interpret, solange gepuffert wird oder etwas schiefging
  const char* sub = !s.active ? "" : s.msg[0] ? s.msg : s.artist;
  if (strcmp(lv_label_get_text(artistL), sub)) lv_label_set_text(artistL, sub);
  lv_obj_set_style_text_color(artistL, s.msg[0] ? C_GOLD() : C_DIM(), 0);
  lv_label_set_text(lv_obj_get_child(playB, 0), s.playing ? I_PAUSE : I_PLAY);
  // Sleep-Timer: nur zu sehen, wenn etwas läuft und der Hinweis weg ist
  hide(sleepB, !s.active || !lv_obj_has_flag(hintL, LV_OBJ_FLAG_HIDDEN));
  long left = music::parentStarted() ? -1 : quota::leftS(s.live ? quota::RADIO : quota::STORIES);
  char lb[32] = "";
  if (s.active && left >= 0) fmtLeft(lb, sizeof(lb), left);
  if (strcmp(lv_label_get_text(leftL), lb)) lv_label_set_text(leftL, lb);
  hide(leftL, !lv_obj_has_flag(hintL, LV_OBJ_FLAG_HIDDEN));
  int sl = music::sleepLeftS();
  char sb[24];
  if (sl) snprintf(sb, sizeof(sb), I_SLEEP " %d Min.", (sl + 59) / 60);
  else snprintf(sb, sizeof(sb), I_SLEEP);
  if (strcmp(lv_label_get_text(lv_obj_get_child(sleepB, 0)), sb)) lv_label_set_text(lv_obj_get_child(sleepB, 0), sb);
  lv_obj_set_style_text_color(lv_obj_get_child(sleepB, 0), sl ? C_NIGHT() : C_DIM(), 0);

  uint32_t v;
  const uint16_t* px = music::art(&v);
  if (px && v != artSeen) {
    artSeen = v;
    artDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    artDsc.header.cf = LV_COLOR_FORMAT_RGB565;
    artDsc.header.w = music::ART;
    artDsc.header.h = music::ART;
    artDsc.header.stride = music::ART * 2;
    artDsc.data = (const uint8_t*)px;
    artDsc.data_size = music::ART * music::ART * 2;
    lv_image_cache_drop(&artDsc);
    lv_image_set_src(cover, &artDsc);
    lv_obj_remove_flag(cover, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(coverPh, LV_OBJ_FLAG_HIDDEN);
  }

  int p = s.active && s.dur ? (int)(s.pos * 1000 / s.dur) : 0;
  lv_arc_set_value(ring, constrain(p, 0, 1000));
}

static void resume() { artSeen = 0xFFFFFFFF; lastId[0] = 0; }

const Page PAGE_MUSIC = {build, update, nullptr, resume, false};

// ================================================================== Listen

struct BrowseCtx {
  char path[240];
  uint32_t seq;      // browseSeq() beim Anfragen
  bool waiting;
  lv_obj_t* list;
  lv_obj_t* spin;
  lv_obj_t* mini;    // „läuft gerade“-Knopf
  music::Item* items;
  int n;
};

static char pendingPath[240];

void browseOpen(const char* path) {
  snprintf(pendingPath, sizeof(pendingPath), "%s", path);
  push(&PAGE_BROWSE);
}

static BrowseCtx* ctx() {
  lv_obj_t* s = topScreen();
  return s ? (BrowseCtx*)lv_obj_get_user_data(s) : nullptr;
}

static void itemClicked(lv_event_t* e) {
  BrowseCtx* c = ctx();
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (!c || i >= c->n) return;
  const music::Item& it = c->items[i];
  play(0);
  if (it.kind == music::K_LIST) { browseOpen(it.id); return; }
  quota::App a = music::isRadio(it.id) ? quota::RADIO : quota::STORIES;
  if (quota::status(a) != quota::OPEN) { home(); openApp(a); return; }  // Zeit inzwischen um
  music::playList(c->items, c->n, i);
  show(&PAGE_MUSIC);
}


static void fill(BrowseCtx* c) {
  char title[72], err[64];
  if (!c->items) c->items = (music::Item*)lv_malloc(sizeof(music::Item) * 80);
  c->n = music::browseCopy(title, sizeof(title), c->items, 80, err, sizeof(err));
  lv_obj_clean(c->list);
  lv_obj_t* t = label(c->list, &font_text_24, C_STORY(), title[0] ? title : "Hören");
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  if (err[0]) {
    lv_obj_t* l = label(c->list, &font_text_20, C_DIM(), err);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* r = pill(c->list, I_REFRESH "  Nochmal", C_SURF2(), C_TEXT());
    lv_obj_add_event_cb(r, [](lv_event_t*) {
      BrowseCtx* c = ctx();
      if (!c) return;
      c->seq = music::browseSeq();
      c->waiting = true;
      lv_obj_remove_flag(c->spin, LV_OBJ_FLAG_HIDDEN);
      music::browse(c->path);
    }, LV_EVENT_CLICKED, nullptr);
  } else if (!c->n) {
    label(c->list, &font_text_20, C_DIM(), "Keine Einträge");
  }
  for (int i = 0; i < c->n; i++) {
    const music::Item& it = c->items[i];
    char sub[80];
    if (it.kind == music::K_TRACK && it.dur >= 3600)
      snprintf(sub, sizeof(sub), "%s%s%d:%02d:%02d", it.sub, it.sub[0] ? " · " : "", it.dur / 3600, it.dur / 60 % 60, it.dur % 60);
    else if (it.kind == music::K_TRACK && it.dur)
      snprintf(sub, sizeof(sub), "%s%s%d:%02d", it.sub, it.sub[0] ? " · " : "", it.dur / 60, it.dur % 60);
    else
      snprintf(sub, sizeof(sub), "%s", it.sub);
    bool station = music::isRadio(it.id);
    const char* icon = station ? I_RADIO : it.kind == music::K_TRACK ? I_MUSIC : !strcmp(it.id, "recent") ? I_HISTORY : I_LIBRARY;
    lv_color_t ic = station ? C_RADIO() : it.kind == music::K_TRACK ? C_DIM() : C_STORY();
    lv_obj_t* r = listRow(c->list, icon, ic, it.title, sub[0] ? sub : nullptr);
    lv_obj_add_event_cb(r, itemClicked, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    // „Zuletzt gehört“: lange drücken entfernt den Eintrag (und die gemerkte Stelle)
    if (!strcmp(c->path, "recent"))
      lv_obj_add_event_cb(r, [](lv_event_t* e) {
        BrowseCtx* c = ctx();
        int k = (int)(intptr_t)lv_event_get_user_data(e);
        if (!c || k >= c->n) return;
        music::recentRemove(c->items[k].id);
        play(0);
        toast("Aus „Zuletzt gehört“ entfernt");
        c->seq = music::browseSeq();  // Liste neu holen
        c->waiting = true;
        lv_obj_remove_flag(c->spin, LV_OBJ_FLAG_HIDDEN);
        music::browse(c->path);
      }, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
  }
  listFit(c->list);
}

static void browseBuild(lv_obj_t* scr) {
  BrowseCtx* c = (BrowseCtx*)lv_malloc_zeroed(sizeof(BrowseCtx));
  snprintf(c->path, sizeof(c->path), "%s", pendingPath);
  lv_obj_set_user_data(scr, c);
  lv_obj_add_event_cb(scr, [](lv_event_t* e) {
    BrowseCtx* c = (BrowseCtx*)lv_event_get_user_data(e);
    if (c->items) lv_free(c->items);
    lv_free(c);
  }, LV_EVENT_DELETE, c);

  c->list = roundList(scr, 70, 40);
  c->spin = lv_spinner_create(scr);
  lv_obj_set_size(c->spin, 64, 64);
  lv_obj_center(c->spin);
  lv_obj_set_style_arc_color(c->spin, C_STORY(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(c->spin, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(c->spin, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(c->spin, C_SURF(), LV_PART_MAIN);

  c->mini = pill(scr, I_MUSIC "  Läuft gerade", C_STORY(), C_BG());
  lv_obj_align(c->mini, LV_ALIGN_BOTTOM_MID, 0, -22);
  lv_obj_add_event_cb(c->mini, [](lv_event_t*) { show(&PAGE_MUSIC); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(c->mini, LV_OBJ_FLAG_HIDDEN);

  c->seq = music::browseSeq();
  c->waiting = true;
  music::browse(c->path);
}

static void browseUpdate() {
  BrowseCtx* c = ctx();
  if (!c) return;
  if (c->waiting && music::browseSeq() != c->seq && !music::browseBusy()) {
    c->waiting = false;
    lv_obj_add_flag(c->spin, LV_OBJ_FLAG_HIDDEN);
    fill(c);
  }
  bool act = music::state().active;
  if (act == lv_obj_has_flag(c->mini, LV_OBJ_FLAG_HIDDEN)) {
    if (act) lv_obj_remove_flag(c->mini, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(c->mini, LV_OBJ_FLAG_HIDDEN);
  }
}

const Page PAGE_BROWSE = {browseBuild, browseUpdate, nullptr, nullptr, false};
