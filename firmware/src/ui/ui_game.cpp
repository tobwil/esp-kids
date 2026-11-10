// Balance: Kugeln durch Neigen der Uhr in Löcher rollen (Bewegungssensor QMI8658).
//   „Ins Ziel“: eine Kugel ins goldene Loch. Die Stufen wechseln reihum: Mauern (Slalom), Labyrinth (Ringe mit
//   Öffnungen, Ziel in der Mitte), Blockaden (verstreute Hindernisse) – rote Löcher als Fallen nur wenige
//   „Alle Kugeln“: drei Kugeln in drei Löcher, so schnell wie möglich
#include <Preferences.h>
#include <math.h>
#include "../audio.h"
#include "../imu.h"
#include "../quota.h"
#include "game_theme.h"
#include "ui.h"

using namespace ui;

static const float FIELD_R = gtheme::R;       // Spielfeld-Radius
static const float BALL_R = 15;        // Kugel-Radius
static const float GRAV = 1100;    // px/s² bei 1 g
static const float WALL_T = 6;     // halbe Wandstärke
static const int MAXB = 3, MAXH = 12, MAXW = 128, MAXP = 420;

struct Ball { float x, y, vx, vy; bool in; float sink; lv_obj_t* o; };
struct Hole { float x, y, r; bool goal, filled; lv_obj_t* o; };
struct Wall { float x1, y1, x2, y2; };  // Kapsel: Strecke mit Dicke 2·WALL_T

static Ball balls[MAXB];
static Hole holes[MAXH];
static Wall walls[MAXW];
static lv_point_precise_t pts[MAXP];  // Punkte der gezeichneten Wände (müssen bestehen bleiben)
static int nb = 0, nh = 0, nw = 0, np = 0, level = 1, mode = 0;  // mode 0 = Menü, 1 = Ins Ziel, 2 = Alle Kugeln
static float startX, startY;
static uint32_t t0 = 0, endAt = 0, resetAt = 0, lastStep = 0;
static lv_obj_t *field, *hud, *menu, *result;
static lv_timer_t* timer = nullptr;
static int bestLevel = 0;
static uint32_t bestMs = 0;
static uint32_t seed = 1;
static uint16_t* bg = nullptr;  // Untergrund (PSRAM)
static int theme = 0, drawnTheme = -1;

static float frand() { seed = seed * 1103515245u + 12345u; return ((seed >> 8) & 0xFFFF) / 65535.0f; }

static lv_obj_t* disc(lv_obj_t* parent, int r, lv_color_t bg, lv_color_t border, int bw) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, 2 * r, 2 * r);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, border, 0);
  lv_obj_set_style_border_width(o, bw, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

static void place(lv_obj_t* o, float x, float y, float r) { lv_obj_set_pos(o, (int)(CX + x - r), (int)(CY + y - r)); }

static void loadBest() {
  Preferences p;
  p.begin("game", true);
  bestLevel = p.getUChar("lvl", 0);
  bestMs = p.getUInt("ms", 0);
  p.end();
}

static void saveBest() {
  Preferences p;
  p.begin("game", false);
  p.putUChar("lvl", bestLevel);
  p.putUInt("ms", bestMs);
  p.end();
}

// ------------------------------------------------------------------ Spielzeit (Zeitkonto, quota.h)
// Gezählt wird nur, solange eine Runde läuft. Zwei Minuten vorher ein Hinweis; ist die Zeit um, darf die laufende
// Stufe noch zu Ende gespielt werden (höchstens 3 Minuten länger), dann ist Schluss.

static bool warned = false;
static uint32_t overSince = 0;

static bool timeUp() { return quota::status(quota::GAME) != quota::OPEN; }

static void showTimeUp();

// ------------------------------------------------------------------ Aufbau einer Runde

static void clearField(bool withTheme) {
  lv_obj_clean(field);
  nb = nh = nw = np = 0;
  if (!withTheme) return;
  // Spielfeld: Schale mit dem Thema der Stufe
  if (!bg) bg = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
  if (!bg) return;
  if (drawnTheme != theme) { gtheme::draw(bg, theme, 12345 + theme * 977); drawnTheme = theme; }
  lv_obj_t* c = lv_canvas_create(field);
  lv_canvas_set_buffer(c, bg, W, H, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(c, 0, 0);
}

// Abstand eines Punktes zu einer Wand (Mittellinie)
static float wallDist(const Wall& w, float x, float y, float* px = nullptr, float* py = nullptr) {
  float ex = w.x2 - w.x1, ey = w.y2 - w.y1, len2 = ex * ex + ey * ey;
  float t = len2 > 0 ? fmaxf(0, fminf(1, ((x - w.x1) * ex + (y - w.y1) * ey) / len2)) : 0;
  float qx = w.x1 + ex * t, qy = w.y1 + ey * t;
  if (px) { *px = qx; *py = qy; }
  return hypotf(x - qx, y - qy);
}

static bool freeSpot(float x, float y, float r, float gap) {
  if (sqrtf(x * x + y * y) > FIELD_R - r - 10) return false;
  if (y - r < -FIELD_R + 52 && fabsf(x) < 120) return false;  // frei unter der Anzeige oben
  for (int i = 0; i < nh; i++)
    if (hypotf(holes[i].x - x, holes[i].y - y) < holes[i].r + r + gap) return false;
  for (int i = 0; i < nw; i++)
    if (wallDist(walls[i], x, y) < WALL_T + r + gap) return false;
  return true;
}

// Wand als Linienzug: gezeichnet mit dunklem Rand, für die Physik in Strecken zerlegt
static void addWalls(const float* xy, int n) {
  if (n < 2 || np + n > MAXP) return;
  lv_point_precise_t* p = &pts[np];
  for (int i = 0; i < n; i++) p[i] = {(lv_value_precise_t)(CX + xy[2 * i]), (lv_value_precise_t)(CY + xy[2 * i + 1])};
  np += n;
  for (int k = 0; k < 2; k++) {  // erst der dunkle Rand, dann der helle Stein
    lv_obj_t* l = lv_line_create(field);
    lv_line_set_points(l, p, n);
    lv_obj_set_style_line_width(l, (int)(2 * WALL_T) + (k ? 0 : 5), 0);
    lv_obj_set_style_line_color(l, lv_color_hex(k ? 0xe2d8c6 : 0x2b241c), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_pos(l, 0, 0);
  }
  for (int i = 0; i + 1 < n && nw < MAXW; i++) walls[nw++] = {xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3]};
}

static void addWall(float x1, float y1, float x2, float y2) {
  float xy[4] = {x1, y1, x2, y2};
  addWalls(xy, 2);
}

// Ring mit einer Öffnung (Winkel in Bogenmaß, Mitte und halbe Breite der Öffnung)
static void addRing(float r, float gap, float half) {
  float xy[2 * 40];
  int n = 0;
  float span = 6.2832f - 2 * half;
  int seg = max(8, (int)(span / 0.17f));
  for (int i = 0; i <= seg && n < 40; i++, n++) {
    float a = gap + half + span * i / seg;
    xy[2 * n] = cosf(a) * r;
    xy[2 * n + 1] = sinf(a) * r;
  }
  addWalls(xy, n);
}

// Strecke quer zur Richtung (dx, dy) durch den Punkt (x, y), entlang der Strecke auf das Spielfeld gekürzt
static void addCross(float x, float y, float dx, float dy, float from, float to) {
  float nx = -dy, ny = dx, lim = FIELD_R - 4;
  // |p + n·s| = lim  →  s² + 2·b·s + c = 0
  float b = x * nx + y * ny, c = x * x + y * y - lim * lim, disc = b * b - c;
  if (disc <= 0) return;
  float s1 = -b - sqrtf(disc), s2 = -b + sqrtf(disc);
  if (from > to) { float t = from; from = to; to = t; }
  from = fmaxf(from, s1);
  to = fminf(to, s2);
  if (to - from < 10) return;
  addWall(x + nx * from, y + ny * from, x + nx * to, y + ny * to);
}

static void addHole(float x, float y, float r, bool goal) {
  Hole& h = holes[nh++];
  h = {x, y, r, goal, false, nullptr};
  lv_color_t rim = goal ? lv_color_hex(0xffc83a) : mode == 2 ? lv_color_hex(gtheme::accent(theme)) : lv_color_hex(0xe0302a);
  h.o = disc(field, (int)r, lv_color_hex(0x000000), rim, goal || mode == 2 ? 4 : 3);
  lv_obj_set_style_bg_grad_color(h.o, lv_color_hex(0x221d17), 0);
  lv_obj_set_style_bg_grad_dir(h.o, LV_GRAD_DIR_VER, 0);
  place(h.o, x, y, r);
}

static void addBall(float x, float y) {
  Ball& b = balls[nb++];
  b = {x, y, 0, 0, false, 0, nullptr};
  b.o = disc(field, (int)BALL_R, lv_color_hex(0xf2efe8), lv_color_hex(0x2a2a2a), 2);
  lv_obj_set_style_bg_grad_color(b.o, lv_color_hex(0x8e8a82), 0);
  lv_obj_set_style_bg_grad_dir(b.o, LV_GRAD_DIR_VER, 0);
  lv_obj_t* shine = disc(b.o, 4, lv_color_white(), lv_color_white(), 0);
  lv_obj_set_pos(shine, 6, 5);
  place(b.o, x, y, BALL_R);
}

// Anzeige oben: Stufe bzw. Kugeln und laufende Zeit, darunter der Rekord
// Die Anzeige oben (Stufe, Rekord bzw. Kugeln und Zeit) erscheint nur kurz: zu Beginn jeder Stufe und wenn eine
// Kugel ins Loch fällt – sonst verdeckt sie Eingänge im Spielfeld
static uint32_t hudUntil = 0;

static void hudFlash(uint32_t ms) {
  lv_anim_delete(hud, nullptr);  // laufendes Ausblenden abbrechen
  lv_obj_set_style_opa(hud, LV_OPA_COVER, 0);
  hudUntil = millis() + ms;
}

static void setHud() {
  char t[80], r[32] = "";
  if (mode == 1) {
    if (bestLevel) snprintf(r, sizeof(r), "\nRekord: Stufe %d", bestLevel);
    snprintf(t, sizeof(t), "Stufe %d · %s%s", level, gtheme::name(theme), r);
  } else {
    int in = 0;
    for (int i = 0; i < nb; i++) in += balls[i].in;
    uint32_t ms = millis() - t0;  // nach dem Ziel ruht die Anzeige (das Ergebnis liegt darüber)
    if (bestMs) snprintf(r, sizeof(r), "\nRekord: %u,%u s", (unsigned)(bestMs / 1000), (unsigned)(bestMs / 100 % 10));
    snprintf(t, sizeof(t), "%d von %d · %u,%u s%s", in, nb, (unsigned)(ms / 1000), (unsigned)(ms / 100 % 10), r);
  }
  if (strcmp(lv_label_get_text(hud), t)) lv_label_set_text(hud, t);
}

static void newRound() {
  seed = millis() | 1;
  theme = mode == 1 ? (level - 1) % gtheme::COUNT : (int)(frand() * gtheme::COUNT) % gtheme::COUNT;
  clearField(true);
  if (mode == 1) {
    float gr = fmaxf(20, 30 - level * 1.0f);
    int kind = (level - 1) % 3, traps = 0;
    if (kind == 1) {
      // Labyrinth: Ringe mit je einer Öffnung, die nächste liegt gegenüber; Ziel in der Mitte
      int rings = level >= 8 ? 3 : 2;
      static const float R3[] = {0.30f, 0.53f, 0.76f}, R2[] = {0.38f, 0.68f};
      const float* rr = rings == 3 ? R3 : R2;
      float gap = frand() * 6.2832f;
      for (int i = rings - 1; i >= 0; i--) {
        float r = rr[i] * FIELD_R;
        addRing(r, gap, (BALL_R + WALL_T + 12) / r);
        // Sperre neben der Öffnung: im Gang dahinter geht es nur in eine Richtung
        if (level >= 5 && i > 0) {
          float a = gap - (BALL_R + WALL_T + 26) / r, ri = rr[i - 1] * FIELD_R;
          addWall(cosf(a) * (ri + 2), sinf(a) * (ri + 2), cosf(a) * (r - 2), sinf(a) * (r - 2));
        }
        gap += 3.1416f + (frand() - 0.5f) * 1.2f;
      }
      addHole(0, 0, gr, true);
      // Start außen, unten (oben steht die Anzeige)
      float a = 0.6f + frand() * 1.9f, r0 = (rr[rings - 1] * FIELD_R + FIELD_R) / 2;
      startX = cosf(a) * r0;
      startY = sinf(a) * r0;
      traps = level >= 8 ? 1 : 0;
    } else {
      // Start und Ziel gegenüber
      float a = frand() * 6.2832f;
      startX = cosf(a) * 150;
      startY = sinf(a) * 150;
      float gx = -startX * 0.95f, gy = -startY * 0.95f, dx = -cosf(a), dy = -sinf(a);
      if (kind == 0) {
        // Mauern: Querwände, abwechselnd links und rechts offen – Slalom
        int n = min(4, 2 + level / 4);
        float side = frand() < 0.5f ? 1 : -1;
        for (int i = 0; i < n; i++) {
          float t = (i + 1.0f) / (n + 1), x = startX + (gx - startX) * t, y = startY + (gy - startY) * t;
          // höhere Stufe: längere Wände, engerer Durchlass (bis Stufe 16)
          addCross(x, y, dx, dy, side * -FIELD_R, side * (FIELD_R * 0.55f - BALL_R * 2.6f + min(level, 16) * 3));
          side = -side;
        }
        traps = min(3, level / 3);
      } else {
        // Blockaden: kurze Hindernisse in zufälliger Lage, mit Platz dazwischen
        int n = min(8, 4 + level / 3), placed = 0;
        float cx[8], cy[8];
        for (int tries = 0; placed < n && tries < 300; tries++) {
          float x = (frand() * 2 - 1) * FIELD_R * 0.8f, y = (frand() * 2 - 1) * FIELD_R * 0.8f;
          // Abstand zu Start und Ziel: halbe Länge + Kugel bzw. Loch + Luft
          if (hypotf(x, y) > FIELD_R - 50 || hypotf(x - startX, y - startY) < 105 || hypotf(x - gx, y - gy) < 110) continue;
          bool ok = true;
          for (int k = 0; k < placed; k++) ok &= hypotf(x - cx[k], y - cy[k]) > 95;
          if (!ok) continue;
          float a2 = frand() * 3.1416f, len = 26 + frand() * 22;
          addWall(x - cosf(a2) * len, y - sinf(a2) * len, x + cosf(a2) * len, y + sinf(a2) * len);
          cx[placed] = x;
          cy[placed++] = y;
        }
        traps = min(3, 1 + level / 4);
      }
      addHole(gx, gy, gr, true);
    }
    for (int i = 0, tries = 0; i < traps && tries < 400; tries++) {
      float x = (frand() * 2 - 1) * FIELD_R, y = (frand() * 2 - 1) * FIELD_R;
      if (!freeSpot(x, y, 22, 20) || hypotf(x - startX, y - startY) < 70) continue;
      addHole(x, y, 22, false);
      i++;
    }
    addBall(startX, startY);
  } else {
    float a0 = frand() * 6.2832f;
    for (int i = 0; i < 3; i++) addHole(cosf(a0 + i * 2.094f) * 140, sinf(a0 + i * 2.094f) * 140, 24, false);
    // zwei kurze Mauern zwischen Mitte und Löchern
    for (int i = 0; i < 2; i++) {
      float a = a0 + 1.047f + i * 2.094f, x = cosf(a) * 95, y = sinf(a) * 95;
      addWall(x - sinf(a) * 34, y + cosf(a) * 34, x + sinf(a) * 34, y - cosf(a) * 34);
    }
    for (int i = 0; i < 3; i++) addBall(cosf(a0 + 1.047f + i * 2.094f) * 30, sinf(a0 + 1.047f + i * 2.094f) * 30);
  }
  t0 = millis();
  endAt = resetAt = 0;
  setHud();
  lv_obj_move_foreground(hud);
  hudFlash(2500);
}

// ------------------------------------------------------------------ Ergebnis

static void showResult(const char* line1, const char* line2) {
  result = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(result);
  lv_obj_set_size(result, W, H);
  lv_obj_set_style_bg_color(result, C_BG(), 0);
  lv_obj_set_style_bg_opa(result, LV_OPA_80, 0);
  lv_obj_t* a = label(result, &font_text_32, C_GOLD(), line1);
  lv_obj_align(a, LV_ALIGN_CENTER, 0, -60);
  lv_obj_t* b = label(result, &font_text_20, C_TEXT(), line2);
  lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(b, LV_ALIGN_CENTER, 0, -10);
  lv_obj_t* again = pill(result, I_REPLAY "  Nochmal", C_GOLD(), C_BG());
  lv_obj_align(again, LV_ALIGN_CENTER, 0, 60);
  lv_obj_add_event_cb(again, [](lv_event_t*) {
    lv_obj_delete(result);
    result = nullptr;
    if (timeUp()) { showTimeUp(); return; }
    if (mode == 1) level = 1;
    newRound();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* m = pill(result, "Menü", C_SURF2(), C_TEXT());
  lv_obj_align(m, LV_ALIGN_CENTER, 0, 120);
  lv_obj_add_event_cb(m, [](lv_event_t*) {
    lv_obj_delete(result);
    result = nullptr;
    mode = 0;
    clearField(false);
    lv_obj_remove_flag(menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hud, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, nullptr);
}

// ------------------------------------------------------------------ Physik

static void step(lv_timer_t*) {
  uint32_t now = millis();
  float dt = fminf(0.04f, (now - lastStep) / 1000.0f);
  lastStep = now;
  if (!mode || result) return;
  hwActivity();  // Display bleibt beim Spielen an
  quota::use(quota::GAME, (uint32_t)(dt * 1000));
  long left = quota::leftS(quota::GAME);
  if (!warned && left > 0 && left <= 120) {
    warned = true;
    toast("Noch 2 Minuten Balance");
  }
  if (left == 0 && !overSince) overSince = now;
  if (overSince && now - overSince > 180000) { showTimeUp(); return; }  // an einer Stufe festgebissen: trotzdem Schluss
  float gx = 0, gy = 0;
  imu::gravity(gx, gy);
  if (mode == 2) setHud();  // laufende Zeit (sichtbar nur, solange die Anzeige eingeblendet ist)
  if (hudUntil && now > hudUntil) {
    hudUntil = 0;
    lv_obj_fade_out(hud, 400, 0);
  }

  if (resetAt && now > resetAt) {  // nach einem Fehler: Kugel zurück zum Start
    resetAt = 0;
    Ball& b = balls[0];
    b = {startX, startY, 0, 0, false, 0, b.o};
    lv_obj_set_style_transform_scale(b.o, 256, 0);
    lv_obj_set_style_opa(b.o, LV_OPA_COVER, 0);
  }
  if (endAt && now > endAt) {
    endAt = 0;
    if (mode == 1) {
      level++;
      if (level - 1 > bestLevel) { bestLevel = level - 1; saveBest(); }
      if (timeUp()) showTimeUp();  // Stufe geschafft, Zeit um: hier aufhören
      else newRound();
    }
    return;
  }

  for (int i = 0; i < nb; i++) {
    Ball& b = balls[i];
    if (b.in) {  // fällt ins Loch: kleiner werden
      if (b.sink < 1) {
        b.sink = fminf(1, b.sink + dt * 4);
        lv_obj_set_style_transform_pivot_x(b.o, (int)BALL_R, 0);
        lv_obj_set_style_transform_pivot_y(b.o, (int)BALL_R, 0);
        lv_obj_set_style_transform_scale(b.o, (int)(256 * (1 - 0.55f * b.sink)), 0);
        lv_obj_set_style_opa(b.o, (lv_opa_t)(255 * (1 - 0.6f * b.sink)), 0);
      }
      continue;
    }
    // in kleinen Schritten, damit die Kugel auch schnell nicht durch eine Wand rutscht
    int sub = max(1, (int)ceilf(hypotf(b.vx, b.vy) * dt / 4) + 1);
    float h = dt / sub;
    bool knock = false;
    for (int s = 0; s < sub; s++) {
      b.vx += gx * GRAV * h;
      b.vy += gy * GRAV * h;
      // Sog am Lochrand
      for (int k = 0; k < nh; k++) {
        Hole& hl = holes[k];
        if (hl.filled) continue;
        float dx = hl.x - b.x, dy = hl.y - b.y, d = hypotf(dx, dy);
        if (d < hl.r && d > 0.1f) {
          float pull = 1600 * (1 - d / hl.r);
          b.vx += dx / d * pull * h;
          b.vy += dy / d * pull * h;
        }
      }
      float damp = 1 - 1.1f * h;  // Rollreibung
      b.vx *= damp;
      b.vy *= damp;
      b.x += b.vx * h;
      b.y += b.vy * h;
      // Rand: zurückprallen
      float d = hypotf(b.x, b.y);
      if (d > FIELD_R - BALL_R) {
        float nx = b.x / d, ny = b.y / d, vn = b.vx * nx + b.vy * ny;
        b.x = nx * (FIELD_R - BALL_R);
        b.y = ny * (FIELD_R - BALL_R);
        if (vn > 0) {
          knock |= vn > 220;
          b.vx -= 1.45f * vn * nx;
          b.vy -= 1.45f * vn * ny;
        }
      }
      // Wände: aus der Kapsel schieben und abprallen
      for (int k = 0; k < nw; k++) {
        float px, py, dw = wallDist(walls[k], b.x, b.y, &px, &py), mind = BALL_R + WALL_T;
        if (dw >= mind || dw < 0.001f) continue;
        float nx = (b.x - px) / dw, ny = (b.y - py) / dw, vn = b.vx * nx + b.vy * ny;
        b.x = px + nx * mind;
        b.y = py + ny * mind;
        if (vn < 0) {
          knock |= vn < -220;
          b.vx -= 1.45f * vn * nx;
          b.vy -= 1.45f * vn * ny;
        }
      }
    }
    if (knock) audio::play(audio::FX_TICK);
  }
  // Kugeln stoßen sich
  for (int i = 0; i < nb; i++)
    for (int j = i + 1; j < nb; j++) {
      Ball &a = balls[i], &c = balls[j];
      if (a.in || c.in) continue;
      float dx = c.x - a.x, dy = c.y - a.y, d = hypotf(dx, dy);
      if (d >= 2 * BALL_R || d < 0.01f) continue;
      float nx = dx / d, ny = dy / d, over = (2 * BALL_R - d) / 2;
      a.x -= nx * over; a.y -= ny * over;
      c.x += nx * over; c.y += ny * over;
      float rel = (c.vx - a.vx) * nx + (c.vy - a.vy) * ny;
      if (rel < 0) {
        float imp = -0.9f * rel;
        a.vx -= imp * nx; a.vy -= imp * ny;
        c.vx += imp * nx; c.vy += imp * ny;
      }
    }
  // Löcher: langsam genug in der Mitte = drin
  for (int i = 0; i < nb; i++) {
    Ball& b = balls[i];
    if (b.in) continue;
    place(b.o, b.x, b.y, BALL_R);
    for (int k = 0; k < nh; k++) {
      Hole& h = holes[k];
      if (h.filled) continue;
      float d = hypotf(h.x - b.x, h.y - b.y);
      if (d > h.r - BALL_R * 0.6f || hypotf(b.vx, b.vy) > 320) continue;
      b.in = true;
      b.x = h.x;
      b.y = h.y;
      place(b.o, b.x, b.y, BALL_R);
      if (mode == 1) {
        if (h.goal) {
          audio::play(audio::FX_OK);
          endAt = now + 900;
        } else {
          audio::play(audio::FX_ERROR);
          resetAt = now + 700;
        }
      } else {
        h.filled = true;
        audio::play(audio::FX_TAP);
        setHud();
        hudFlash(1500);  // „2 von 3“ kurz zeigen
        int in = 0;
        for (int q = 0; q < nb; q++) in += balls[q].in;
        if (in == nb) {
          uint32_t ms = now - t0;
          bool rec = !bestMs || ms < bestMs;
          if (rec) { bestMs = ms; saveBest(); }
          char a[32], b2[64];
          snprintf(a, sizeof(a), "%u,%u s", (unsigned)(ms / 1000), (unsigned)(ms / 100 % 10));
          snprintf(b2, sizeof(b2), rec ? "Neuer Rekord!" : "Rekord: %u,%u s", (unsigned)(bestMs / 1000), (unsigned)(bestMs / 100 % 10));
          audio::play(audio::FX_CHIME);
          showResult(a, b2);
        }
      }
      break;
    }
  }
}

// ------------------------------------------------------------------ Seite

static void showTimeUp() {  // freundlich, ohne Knopf zum Weiterspielen
  bool usedUp = quota::status(quota::GAME) == quota::USED_UP;
  mode = 0;
  clearField(false);
  lv_obj_add_flag(hud, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(menu, LV_OBJ_FLAG_HIDDEN);
  imu::sleep(true);
  result = lv_obj_create(lv_obj_get_screen(menu));  // nicht lv_screen_active(): beim Aufbau der Seite ist noch die alte aktiv
  lv_obj_remove_style_all(result);
  lv_obj_set_size(result, W, H);
  lv_obj_set_style_bg_color(result, C_BG(), 0);
  lv_obj_set_style_bg_opa(result, LV_OPA_COVER, 0);
  lv_obj_t* a = label(result, &font_text_32, C_GOLD(), usedUp ? "Für heute genug gekugelt!" : "Die Spielzeit ist um!");
  lv_label_set_long_mode(a, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(a, 330);
  lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(a, LV_ALIGN_CENTER, 0, -70);
  char b[80];
  const char* next = usedUp ? "Morgen geht's weiter." : "Gut gespielt!";
  if (bestLevel) snprintf(b, sizeof(b), "%s\nDein Rekord: Stufe %d", next, bestLevel);
  else snprintf(b, sizeof(b), "%s", next);
  lv_obj_t* t = label(result, &font_text_20, C_TEXT(), b);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 10);
  lv_obj_t* ok = pill(result, "OK", C_GOLD(), C_BG());
  lv_obj_align(ok, LV_ALIGN_CENTER, 0, 90);
  lv_obj_add_event_cb(ok, [](lv_event_t*) { back(); }, LV_EVENT_CLICKED, nullptr);
  audio::play(audio::FX_CHIME);
}

static void start(int m) {
  if (timeUp()) { showTimeUp(); return; }
  mode = m;
  level = 1;
  lv_obj_add_flag(menu, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(hud, LV_OBJ_FLAG_HIDDEN);
  imu::sleep(false);
  newRound();
  play(0);
}

static void build(lv_obj_t* scr) {
  loadBest();
  field = lv_obj_create(scr);
  lv_obj_remove_style_all(field);
  lv_obj_set_size(field, W, H);
  lv_obj_remove_flag(field, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  hud = label(scr, &font_text_20, C_TEXT(), "");
  lv_obj_set_style_text_align(hud, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(hud, C_BG(), 0);  // dunkles Schild, damit es auf jedem Untergrund lesbar ist
  lv_obj_set_style_bg_opa(hud, LV_OPA_70, 0);
  lv_obj_set_style_radius(hud, 14, 0);
  lv_obj_set_style_pad_hor(hud, 12, 0);
  lv_obj_set_style_pad_ver(hud, 3, 0);
  lv_obj_align(hud, LV_ALIGN_TOP_MID, 0, 22);
  lv_obj_add_flag(hud, LV_OBJ_FLAG_HIDDEN);

  menu = lv_obj_create(scr);
  lv_obj_remove_style_all(menu);
  lv_obj_set_size(menu, W, H);
  lv_obj_t* t = title(menu, "Balance", C_GOLD());
  (void)t;
  lv_obj_t* h = label(menu, &font_text_20, C_DIM(), "Uhr neigen, Kugel rollen lassen");
  lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 84);
  char b[64];
  lv_obj_t* a = pill(menu, I_FLAG "  Ins Ziel", C_GOLD(), C_BG());
  lv_obj_set_size(a, 240, 64);
  lv_obj_align(a, LV_ALIGN_CENTER, 0, -30);
  lv_obj_add_event_cb(a, [](lv_event_t*) { start(1); }, LV_EVENT_CLICKED, nullptr);
  snprintf(b, sizeof(b), bestLevel ? "Rekord: Stufe %d" : "Mauern, Labyrinthe, Fallen", bestLevel);
  lv_obj_t* ra = label(menu, &font_text_20, C_DIM(), b);
  lv_obj_align(ra, LV_ALIGN_CENTER, 0, 14);
  lv_obj_t* c = pill(menu, I_GROUP "  Alle Kugeln", C_SURF2(), C_TEXT());
  lv_obj_set_size(c, 240, 64);
  lv_obj_align(c, LV_ALIGN_CENTER, 0, 80);
  lv_obj_add_event_cb(c, [](lv_event_t*) { start(2); }, LV_EVENT_CLICKED, nullptr);
  if (bestMs) snprintf(b, sizeof(b), "Rekord: %u,%u s", (unsigned)(bestMs / 1000), (unsigned)(bestMs / 100 % 10));
  else snprintf(b, sizeof(b), "Drei Kugeln, drei Löcher");
  lv_obj_t* rc = label(menu, &font_text_20, C_DIM(), b);
  lv_obj_align(rc, LV_ALIGN_CENTER, 0, 124);

  mode = 0;
  result = nullptr;
  lastStep = millis();
  timer = lv_timer_create(step, 16, nullptr);
  warned = false;
  overSince = 0;
  if (timeUp()) showTimeUp();
}

static void leave() {
  if (timer) { lv_timer_delete(timer); timer = nullptr; }
  imu::sleep(true);
  mode = 0;
  if (bg) { heap_caps_free(bg); bg = nullptr; drawnTheme = -1; }
}

// Rekorde für die Weboberfläche; Zurücksetzen nur dort (Eltern)
void gameRecords(int& lvl, uint32_t& ms) {
  loadBest();
  lvl = bestLevel;
  ms = bestMs;
}

void gameResetRecords() {
  bestLevel = 0;
  bestMs = 0;
  saveBest();
}

// Für Bildschirmfotos: direkt eine Stufe zeigen
void gameTestLevel(int l) {
  if (!timer) return;
  if (!mode) start(1);
  level = max(1, l);
  newRound();
}

const Page PAGE_GAME = {build, nullptr, leave, nullptr, false};
