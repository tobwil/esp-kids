#include "faceart.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace faceart {

namespace {

constexpr int HZ = 82;        // Horizont: darüber Himmel, darunter Wiese
constexpr float R = 58.0f;    // Zifferblatt-Radius
constexpr uint16_t BLACK = 0, WHITE = 0xFFFF, INK = rgb(0x141826);

uint16_t* px = nullptr;

uint16_t mix(uint16_t a, uint16_t b, int t) {  // t = 0 … 255 Anteil von b
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((ar + (br - ar) * t / 255) << 11) | ((ag + (bg - ag) * t / 255) << 5) | (ab + (bb - ab) * t / 255));
}

void put(int x, int y, uint16_t c) { if ((unsigned)x < S && (unsigned)y < S) px[y * S + x] = c; }
uint16_t get(int x, int y) { return (unsigned)x < S && (unsigned)y < S ? px[y * S + x] : BLACK; }
void fill(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) put(i, j, c);
}
void blend(int x, int y, uint16_t c, int t) { if ((unsigned)x < S && (unsigned)y < S) px[y * S + x] = mix(px[y * S + x], c, t); }

uint32_t hash(uint32_t i) {
  i ^= i >> 16;
  i *= 0x7feb352d;
  i ^= i >> 15;
  i *= 0x846ca68b;
  i ^= i >> 16;
  return i;
}

// ------------------------------------------------------------------ Schriften

// große Ziffern 5 × 7 (Uhrzeit, Temperatur)
struct Big { char c; uint8_t w; const char* r[7]; };
const Big BIG[] = {
    {'0', 5, {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'1', 5, {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', 5, {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', 5, {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', 5, {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', 5, {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', 5, {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', 5, {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', 5, {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', 5, {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {':', 1, {".", "#", "#", ".", "#", "#", "."}},
    {'-', 4, {"....", "....", "....", "####", "....", "....", "...."}},
    {'^', 3, {".#.", "#.#", ".#.", "...", "...", "...", "..."}},  // Grad
};

// kleine Schrift 3 × 5 (Wochentag, Datum); M, N und W breiter
struct Small { char c; uint8_t w; const char* r[5]; };
const Small SMALL[] = {
    {'A', 3, {".#.", "#.#", "###", "#.#", "#.#"}},     {'C', 3, {".##", "#..", "#..", "#..", ".##"}},
    {'D', 3, {"##.", "#.#", "#.#", "#.#", "##."}},     {'E', 3, {"###", "#..", "##.", "#..", "###"}},
    {'F', 3, {"###", "#..", "##.", "#..", "#.."}},     {'G', 3, {".##", "#..", "#.#", "#.#", ".##"}},
    {'H', 3, {"#.#", "#.#", "###", "#.#", "#.#"}},     {'I', 3, {"###", ".#.", ".#.", ".#.", "###"}},
    {'M', 5, {"#...#", "##.##", "#.#.#", "#...#", "#...#"}},
    {'N', 4, {"#..#", "##.#", "#.##", "#..#", "#..#"}},
    {'O', 3, {".#.", "#.#", "#.#", "#.#", ".#."}},     {'R', 3, {"##.", "#.#", "##.", "#.#", "#.#"}},
    {'S', 3, {".##", "#..", ".#.", "..#", "##."}},     {'T', 3, {"###", ".#.", ".#.", ".#.", ".#."}},
    {'W', 5, {"#...#", "#...#", "#.#.#", "##.##", "#...#"}},
    {'Z', 3, {"###", "..#", ".#.", "#..", "###"}},
    {'0', 3, {"###", "#.#", "#.#", "#.#", "###"}},     {'1', 3, {".#.", "##.", ".#.", ".#.", "###"}},
    {'2', 3, {"###", "..#", "###", "#..", "###"}},     {'3', 3, {"###", "..#", ".##", "..#", "###"}},
    {'4', 3, {"#.#", "#.#", "###", "..#", "..#"}},     {'5', 3, {"###", "#..", "###", "..#", "###"}},
    {'6', 3, {"###", "#..", "###", "#.#", "###"}},     {'7', 3, {"###", "..#", ".#.", ".#.", ".#."}},
    {'8', 3, {"###", "#.#", "###", "#.#", "###"}},     {'9', 3, {"###", "#.#", "###", "..#", "###"}},
    {'.', 1, {".", ".", ".", ".", "#"}},               {' ', 2, {"..", "..", "..", "..", ".."}},
};

template <class G, int N, int ROWS>
const G* find(const G (&tab)[N], char c) {
  for (auto& g : tab) if (g.c == c) return &g;
  return nullptr;
}

template <class G, int N, int ROWS>
int textW(const G (&tab)[N], const char* t, int sc) {
  int w = 0;
  for (const char* p = t; *p; p++) {
    const G* g = find<G, N, ROWS>(tab, *p);
    if (g) w += (g->w + 1) * sc;
  }
  return w ? w - sc : 0;
}

template <class G, int N, int ROWS>
void text(const G (&tab)[N], int x, int y, const char* t, int sc, uint16_t c) {
  for (const char* p = t; *p; p++) {
    const G* g = find<G, N, ROWS>(tab, *p);
    if (!g) continue;
    for (int j = 0; j < ROWS; j++)
      for (int i = 0; i < g->w; i++)
        if (g->r[j][i] == '#') fill(x + i * sc, y + j * sc, sc, sc, c);
    x += (g->w + 1) * sc;
  }
}

// Text mit dunklem Rand (lesbar auf jedem Himmel)
template <class G, int N, int ROWS>
void label(const G (&tab)[N], int cx, int y, const char* t, int sc, uint16_t c) {
  int x = cx - textW<G, N, ROWS>(tab, t, sc) / 2;
  for (int oy = -1; oy <= 1; oy++)
    for (int ox = -1; ox <= 1; ox++)
      if (ox || oy) text<G, N, ROWS>(tab, x + ox, y + oy, t, sc, INK);
  text<G, N, ROWS>(tab, x, y, t, sc, c);
}

void bigLabel(int cx, int y, const char* t, int sc, uint16_t c) { label<Big, sizeof(BIG) / sizeof(BIG[0]), 7>(BIG, cx, y, t, sc, c); }
void smallLabel(int cx, int y, const char* t, uint16_t c) { label<Small, sizeof(SMALL) / sizeof(SMALL[0]), 5>(SMALL, cx, y, t, 1, c); }

// ------------------------------------------------------------------ Himmel

struct Palette { uint32_t top, bottom; };

Palette skyColors(const Scene& s) {
  if (!s.day) {
    switch (s.sky) {
      case CLEAR: case PARTLY: return {0x080c2a, 0x263670};
      case SNOW: return {0x1c2238, 0x46527a};
      case FOG: return {0x22262f, 0x4a4f5a};
      default: return {0x10141f, 0x2c3142};
    }
  }
  switch (s.sky) {
    case CLEAR: return {0x2f86ea, 0xa6dbff};
    case PARTLY: return {0x3f8fe0, 0xb4dcf5};
    case CLOUDY: return {0x7d93ab, 0xc3cdd8};
    case FOG: return {0xa4aab2, 0xd8dcdf};
    case DRIZZLE: return {0x6f8296, 0xa9b5c1};
    case RAIN: return {0x55667a, 0x8d9aa8};
    case SNOW: return {0x9fb1c4, 0xe4ebf2};
    case STORM: return {0x343850, 0x60657c};
    default: return {0x2f86ea, 0xa6dbff};
  }
}

// Verlauf in wenigen Stufen mit Schachbrett-Übergang – wie früher auf dem Bildschirm
void sky(const Scene& s, bool flash) {
  Palette p = skyColors(s);
  uint16_t top = rgb(p.top), bot = rgb(p.bottom);
  const int STEPS = 6;
  for (int y = 0; y < HZ; y++) {
    float t = (float)y / (HZ - 1) * STEPS;
    int k = (int)t;
    float f = t - k;
    for (int x = 0; x < S; x++) {
      int kk = k + (f > 0.5f && ((x + y) & 1) ? 1 : 0);
      uint16_t c = mix(top, bot, (kk > STEPS ? STEPS : kk) * 255 / STEPS);
      if (flash) c = mix(c, WHITE, 150);
      px[y * S + x] = c;
    }
  }
}

void stars(const Scene& s) {
  for (int i = 0; i < 26; i++) {
    uint32_t h = hash(i * 7 + 3);
    int x = h % S, y = (h >> 8) % (HZ - 14);
    bool twinkle = (hash(i + s.frame / 3) & 7) == 0;
    uint16_t c = twinkle ? rgb(0x8890b8) : (h >> 20) & 1 ? WHITE : rgb(0xfff2b0);
    put(x, y, c);
    if (((h >> 16) & 7) == 0 && !twinkle) {  // ein paar große Sterne
      put(x - 1, y, mix(c, get(x - 1, y), 140));
      put(x + 1, y, mix(c, get(x + 1, y), 140));
      put(x, y - 1, mix(c, get(x, y - 1), 140));
      put(x, y + 1, mix(c, get(x, y + 1), 140));
    }
  }
}

constexpr int SX = 58, SY = 13;  // Sonne bzw. Mond über der Uhrzeit

void sun(int frame) {
  for (int y = -7; y <= 7; y++)
    for (int x = -7; x <= 7; x++) {
      float d = sqrtf(x * x + y * y);
      if (d <= 5.6f) put(SX + x, SY + y, d > 4.4f ? rgb(0xffa51f) : (x < 0 && y < 0 && d < 3.2f) ? rgb(0xfff3a0) : rgb(0xffd23f));
    }
  // Strahlen drehen sich langsam
  for (int k = 0; k < 8; k++) {
    float a = k * 0.7854f + (frame % 2) * 0.3927f;
    for (float r = 8; r <= 10.5f; r += 1) put(SX + (int)lroundf(cosf(a) * r), SY + (int)lroundf(sinf(a) * r), rgb(0xffc83a));
  }
}

void moon() {
  for (int y = -6; y <= 6; y++)
    for (int x = -6; x <= 6; x++) {
      bool in = x * x + y * y <= 30;
      bool cut = (x - 3) * (x - 3) + (y + 2) * (y + 2) <= 24;
      if (in && !cut) put(SX + x, SY + y, x < -3 ? rgb(0xd8d2a0) : rgb(0xf6f0c4));
    }
}

// Pixel-Wolke: drei Bäuche und ein flacher Boden, unten schattiert
void cloud(int x, int y, int w, uint16_t body, uint16_t shade, uint16_t light) {
  int r1 = w / 4, r2 = w / 3;
  int cx1 = x + w / 4, cx2 = x + w / 2, cx3 = x + 3 * w / 4, base = y + r2;
  for (int j = y - r2; j <= base + 2; j++)
    for (int i = x - 1; i <= x + w + 1; i++) {
      auto inC = [&](int cx, int cy, int r) { return (i - cx) * (i - cx) + (j - cy) * (j - cy) <= r * r; };
      bool in = inC(cx1, base - r1 + 1, r1) || inC(cx2, base - r2 + 1, r2) || inC(cx3, base - r1 + 1, r1) ||
                (j >= base - 2 && j <= base + 1 && i >= x + 1 && i <= x + w - 1);
      if (!in) continue;
      uint16_t c = j >= base ? shade : (j < base - r2 + 3 && i < cx2) ? light : body;
      put(i, j, c);
    }
}

void clouds(const Scene& s) {
  uint16_t body, shade, light;
  int n;
  if (s.sky == STORM) { body = rgb(0x4a4e63); shade = rgb(0x30334a); light = rgb(0x5f6378); n = 5; }
  else if (s.sky == RAIN || s.sky == DRIZZLE) { body = rgb(0x8592a0); shade = rgb(0x5f6b7a); light = rgb(0xa2adb8); n = 5; }
  else if (s.sky == CLOUDY) { body = rgb(0xd5dbe2); shade = rgb(0xa5afbb); light = WHITE; n = 5; }
  else if (s.sky == SNOW) { body = rgb(0xe6ebf0); shade = rgb(0xb7c2ce); light = WHITE; n = 3; }
  else if (s.sky == PARTLY) { body = WHITE; shade = rgb(0xc9dbea); light = WHITE; n = 2; }
  else return;
  if (!s.day) { body = mix(body, BLACK, 140); shade = mix(shade, BLACK, 140); light = mix(light, BLACK, 120); }
  for (int i = 0; i < n; i++) {
    uint32_t h = hash(i * 31 + 11);
    int w = 18 + h % 10;
    int y = i % 2 ? 4 + (h >> 8) % 6 : 54 + (h >> 8) % 10;  // abwechselnd oben und unter der Uhrzeit
    if (n >= 5 && i == 4) y = 16;
    int speed = 1 + (h >> 12) % 2;
    int span = S + w + 10;
    int x = (int)((h >> 16) % span + s.frame * speed / 3) % span - w - 5;
    cloud(x, y, w, body, shade, light);
  }
}

void rain(const Scene& s) {
  int n = s.sky == DRIZZLE ? 14 : s.heavy || s.sky == STORM ? 46 : 30;
  int len = s.sky == DRIZZLE ? 2 : 4;
  uint16_t c = s.day ? rgb(0xcfe6ff) : rgb(0x7f9cc4);
  for (int i = 0; i < n; i++) {
    uint32_t h = hash(i * 13 + 5);
    int x0 = h % (S + 20);
    int y = (int)((h >> 8) % 120 + s.frame * 5) % 120 - 10;
    for (int k = 0; k < len; k++) put(x0 - (y + k) / 5, y + k, c);
  }
}

void snow(const Scene& s) {
  int n = s.heavy ? 50 : 32;
  for (int i = 0; i < n; i++) {
    uint32_t h = hash(i * 17 + 9);
    int y = (int)((h >> 8) % 120 + s.frame * (1 + (h >> 20) % 2)) % 120 - 4;
    int x = (int)(h % S + 2 * sinf((s.frame + i * 5) * 0.35f));
    put(x, y, WHITE);
    if ((h >> 24) % 4 == 0) {  // ein paar große Flocken
      put(x - 1, y, rgb(0xdfe8f2));
      put(x + 1, y, rgb(0xdfe8f2));
      put(x, y - 1, rgb(0xdfe8f2));
      put(x, y + 1, rgb(0xdfe8f2));
    }
  }
}

void lightning(int frame) {
  int x = 82 + (int)(hash(frame / 14) % 14), y = 22;
  uint16_t c = rgb(0xfff6a8);
  while (y < HZ + 4) {
    for (int k = 0; k < 4 && y < HZ + 4; k++) {
      put(x, y, c);
      put(x + 1, y, WHITE);
      y++;
    }
    x += (y / 4) % 2 ? -2 : 1;
  }
}

void fog(const Scene& s) {
  uint16_t c = s.day ? rgb(0xeef0f2) : rgb(0x6b707a);
  for (int y = 8; y < S; y++) {
    int band = (y + s.frame / 2) / 3;
    if (band % 3) continue;
    int shift = (int)(hash(band) % 20);
    for (int x = 0; x < S; x++)
      if (((x + shift) / 9) % 4 != 3) blend(x, y, c, 120);
  }
}

// ------------------------------------------------------------------ Wiese

void ground(const Scene& s) {
  bool snowy = s.sky == SNOW || (s.weather && s.temp <= -2 && s.sky != CLEAR);
  uint16_t hill = snowy ? rgb(0xd5dfeb) : rgb(0x3f9a4b), grass = snowy ? rgb(0xf3f7fb) : rgb(0x5cc15e),
           dark = snowy ? rgb(0xc9d6e4) : rgb(0x4aa94e), tuft = snowy ? WHITE : rgb(0x8be07a);
  if (!s.day) {
    hill = mix(hill, rgb(0x0a1030), 150);
    grass = mix(grass, rgb(0x0a1030), 150);
    dark = mix(dark, rgb(0x0a1030), 160);
    tuft = mix(tuft, rgb(0x0a1030), 150);
  }
  for (int x = 0; x < S; x++) {  // Hügel am Horizont
    int h = 4 + (int)(3 * sinf(x * 0.085f) + 2 * sinf(x * 0.21f + 1));
    for (int y = HZ - h; y < HZ; y++) put(x, y, hill);
  }
  for (int y = HZ; y < S; y++)
    for (int x = 0; x < S; x++) put(x, y, ((y - HZ) / 3 + (x + y) / 7) % 4 == 0 ? dark : grass);
  for (int i = 0; i < 18; i++) {  // Grasbüschel bzw. Schneehaufen
    uint32_t h = hash(i * 23 + 1);
    int x = h % S, y = HZ + 3 + (h >> 8) % (S - HZ - 6);
    put(x, y, tuft);
    put(x - 1, y + 1, tuft);
    put(x + 1, y + 1, tuft);
  }
  if (s.day && !snowy && (s.sky == CLEAR || s.sky == PARTLY) && s.temp >= 8) {  // Blumen
    static const uint32_t COL[] = {0xff5f7a, 0xffd23f, 0xffffff, 0xb592ff};
    for (int i = 0; i < 7; i++) {
      uint32_t h = hash(i * 41 + 7);
      int x = 44 + h % 70, y = HZ + 6 + (h >> 8) % 20;
      if (x > 56 && x < 100 && y < HZ + 22) continue;  // nicht unter die Temperatur
      put(x, y + 1, rgb(0x2e7d32));
      put(x, y, rgb(COL[i % 4]));
      put(x - 1, y, rgb(0xffe08a));
    }
  }
  if (s.sky == RAIN || s.sky == DRIZZLE || s.sky == STORM) {  // Pfützen mit Ringen
    static const int P[][3] = {{88, 104, 9}, {50, 96, 6}};
    for (auto& p : P) {
      for (int y = -2; y <= 2; y++)
        for (int x = -p[2]; x <= p[2]; x++)
          if (x * x * 4 / (p[2] * p[2]) + y * y <= 4) put(p[0] + x, p[1] + y, s.day ? rgb(0x6f9fd6) : rgb(0x30476e));
      int r = (s.frame + p[0]) % 5;
      if (r < 4) {
        put(p[0] - r - 1, p[1], rgb(0xcfe6ff));
        put(p[0] + r + 1, p[1], rgb(0xcfe6ff));
      }
    }
  }
}

// ------------------------------------------------------------------ Kind

// H Haare, S Haut, E Auge, M Mund, T Oberteil, A Ärmel, K Hand, B Hose oben, L Bein unten, F Schuh
const char* const KID[] = {
    "...HHHHH...", "..HHHHHHH..", "..HSSSSSH..", "..SESSSES..", "..SSSSSSS..", "..SSMMMSS..", "...SSSSS...",
    "....SSS....", "..TTTTTTT..", ".ATTTTTTTA.", ".ATTTTTTTA.", ".ATTTTTTTA.", ".KTTTTTTTK.", "..BBBBBBB..",
    "..BBB.BBB..", "..LLL.LLL..", "..LLL.LLL..", "..LLL.LLL..", "..FFF.FFF..", ".FFFF.FFFF.",
};
constexpr int KW = 11, KH = 20;

struct Look {
  uint32_t top, sleeve, hand, pants, legs, shoes;
  bool shortSleeves;
};

Look lookOf(Outfit o) {
  constexpr uint32_t SKIN = 0xf2c29b;
  switch (o) {
    case HOT: return {0xff5a5a, 0xff5a5a, SKIN, 0x3d7bd9, SKIN, 0xffffff, true};
    case WARM: return {0xffa53c, 0xffa53c, SKIN, 0x3d5a99, 0x3d5a99, 0xffffff, true};
    case MILD: return {0x47c45b, 0x47c45b, SKIN, 0x3d5a99, 0x3d5a99, 0x8b5a2b, false};
    case COOL: return {0x3d7bd9, 0x3d7bd9, SKIN, 0x2b3a66, 0x2b3a66, 0x6b4423, false};
    case COLD: return {0xe0483e, 0xe0483e, 0xffd23f, 0x2b3a66, 0x2b3a66, 0x4a3020, false};
    case RAINY: return {0xffd23f, 0xffd23f, SKIN, 0x2b3a66, 0x2fbf71, 0x2fbf71, false};
    case SNOWY: return {0x8a5cf0, 0x8a5cf0, 0xff5f7a, 0x2b3a66, 0x2b3a66, 0x4a3020, false};
    default: return {0x47c45b, 0x47c45b, SKIN, 0x3d5a99, 0x3d5a99, 0x8b5a2b, false};
  }
}

// dunkler Rand um alles, was in der Maske steht
void outline(const uint8_t* mask, int ox, int oy, int w, int h) {
  for (int y = -1; y <= h; y++)
    for (int x = -1; x <= w; x++) {
      auto in = [&](int i, int j) { return i >= 0 && j >= 0 && i < w && j < h && mask[j * w + i]; };
      if (in(x, y)) continue;
      if (in(x - 1, y) || in(x + 1, y) || in(x, y - 1) || in(x, y + 1)) put(ox + x, oy + y, INK);
    }
}

void umbrella(int x, int y) {  // Griff in der rechten Hand (x, y), Schirm darüber
  for (int j = y - 13; j <= y; j++) put(x, j, rgb(0x5a3a1e));
  put(x - 1, y, rgb(0x5a3a1e));
  int cy = y - 13;
  for (int j = -6; j <= 0; j++)
    for (int i = -10; i <= 10; i++) {
      if (i * i * 36 / 100 + j * j > 36) continue;
      bool stripe = ((i + 10) / 4) % 2;
      uint16_t c = stripe ? WHITE : rgb(0xe0483e);
      if (j == 0 && (i + 10) % 4 == 2) continue;  // gewellter Rand
      put(x + i, cy + j, c);
    }
  // Rand
  for (int i = -10; i <= 10; i++) {
    int j = -(int)sqrtf(fmaxf(0, 36 - i * i * 36 / 100.0f)) - 1;
    put(x + i, cy + j, INK);
  }
}

void kid(const Scene& s, int ox, int oy) {
  Outfit o = outfit(s);
  Look l = lookOf(o);
  constexpr uint32_t SKIN = 0xf2c29b;
  uint8_t mask[(KW + 2) * (KH + 4)] = {};
  const int MW = KW + 2, MY = 3;  // Maske mit Platz für Mütze und Bommel
  bool blink = s.frame % 17 == 0;
  bool hood = o == RAINY, beanie = o == COLD || o == SNOWY, cap = o == HOT;
  bool shades = o == HOT && s.day && (s.sky == CLEAR || s.sky == PARTLY);
  for (int y = 0; y < KH; y++)
    for (int x = 0; x < KW; x++) {
      char k = KID[y][x];
      if (k == '.') continue;
      uint32_t c;
      switch (k) {
        case 'H': c = 0x6b3e1f; break;
        case 'S': c = SKIN; break;
        case 'E': c = blink ? 0xd9a47e : 0x1e1e28; break;
        case 'M': c = 0xd35d5d; break;
        case 'T': c = l.top; break;
        case 'A': c = l.shortSleeves && y > 9 ? SKIN : l.sleeve; break;
        case 'K': c = l.hand; break;
        case 'B': c = l.pants; break;
        case 'L': c = y >= 15 && (o == RAINY) ? l.shoes : l.legs; break;
        case 'F': c = l.shoes; break;
        default: c = 0xff00ff;
      }
      if (shades && y == 3 && x >= 3 && x <= 7) c = 0x1e1e28;
      if (o == COOL && x == 5 && y >= 8 && y <= 12) c = 0x9fc4ff;  // Reißverschluss
      if (o == RAINY && x == 5 && y >= 8 && y <= 12) c = 0xe0a800;
      put(ox + x, oy + y, rgb(c));
      mask[(y + MY) * MW + x + 1] = 1;
    }
  auto add = [&](int x, int y, uint32_t c) {
    put(ox + x, oy + y, rgb(c));
    if (x + 1 >= 0 && x + 1 < MW && y + MY >= 0 && y + MY < KH + 4) mask[(y + MY) * MW + x + 1] = 1;
  };
  if (hood) {  // Kapuze in Jackenfarbe ums Gesicht
    for (int x = 2; x <= 8; x++) add(x, 0, l.top), add(x, 1, l.top);
    add(1, 1, l.top);
    add(9, 1, l.top);
    for (int y = 2; y <= 6; y++) add(1, y, l.top), add(9, y, l.top);
    add(2, 2, l.top);
    add(8, 2, l.top);
  }
  if (beanie) {  // Mütze mit Bommel, Schal
    uint32_t m = o == SNOWY ? 0xffd23f : 0x47c45b;
    for (int x = 2; x <= 8; x++) add(x, 0, m), add(x, 1, m);
    for (int x = 3; x <= 7; x++) add(x, -1, m);
    add(5, -2, 0xffffff);
    add(4, -2, 0xffffff);
    add(6, -2, 0xffffff);
    add(5, -3, 0xffffff);
    for (int x = 2; x <= 8; x++) add(x, 7, (x % 2) ? 0xffffff : m);
    add(7, 8, m);
    add(7, 9, 0xffffff);
    add(7, 10, m);
  }
  if (cap) {  // Schirmmütze
    for (int x = 2; x <= 8; x++) add(x, 0, 0x3d7bd9), add(x, 1, 0x3d7bd9);
    for (int x = 7; x <= 10; x++) add(x, 2, 0x2b5aa8);
  }
  outline(mask, ox - 1, oy - MY, MW, KH + 4);
  if (o == RAINY && (s.sky == RAIN || s.sky == STORM)) umbrella(ox + 9, oy + 12);
}

// Schlafenszeit: das Kind liegt im Bett, darüber schweben Z
void bed(const Scene& s) {
  const int x = 12, y = 84;
  uint16_t wood = rgb(0x9a6232), woodD = rgb(0x6e4220), blanket = rgb(0x6c8cff), blanketD = rgb(0x4f6be0);
  if (!s.day) { wood = mix(wood, BLACK, 60); blanket = mix(blanket, BLACK, 50); blanketD = mix(blanketD, BLACK, 50); }
  fill(x, y, 3, 14, wood);            // Kopfteil
  fill(x + 28, y + 5, 3, 9, wood);    // Fußteil
  fill(x, y + 10, 31, 2, woodD);      // Rahmen
  fill(x + 1, y + 12, 2, 2, woodD);
  fill(x + 28, y + 12, 2, 2, woodD);
  fill(x + 4, y + 4, 7, 4, WHITE);    // Kissen
  // Kopf mit geschlossenen Augen
  fill(x + 5, y + 1, 5, 2, rgb(0x6b3e1f));
  fill(x + 5, y + 3, 5, 4, rgb(0xf2c29b));
  put(x + 6, y + 5, rgb(0x7a5a4a));
  put(x + 8, y + 5, rgb(0x7a5a4a));
  for (int j = 0; j < 5; j++)          // Decke mit Muster
    for (int i = 0; i < 18; i++) put(x + 10 + i, y + 5 + j, (i + j) % 4 == 0 ? blanketD : blanket);
  uint8_t mask[33 * 16] = {};
  for (int j = 0; j < 16; j++)
    for (int i = 0; i < 33; i++) {
      int px2 = x - 1 + i, py = y - 1 + j;
      uint16_t c = get(px2, py);
      mask[j * 33 + i] = (i >= 1 && i <= 31 && j >= 1 && j <= 14) &&
                         (c == wood || c == woodD || c == WHITE || c == blanket || c == blanketD || c == rgb(0xf2c29b) ||
                          c == rgb(0x6b3e1f) || c == rgb(0x7a5a4a));
    }
  outline(mask, x - 1, y - 1, 33, 16);
  int t = s.frame % 8;  // Z steigen auf
  smallLabel(x + 14, y - 6 - t, "Z", rgb(0xdfe6ff));
  if (t > 3) smallLabel(x + 19, y - 12 - (t - 4), "Z", rgb(0xb8c4f0));
}

// ------------------------------------------------------------------ Akku, Musik

void battery(int cx, int y, const Scene& s) {
  if (s.battery < 0 && !s.charging) return;
  int x = cx - 6;
  fill(x - 1, y - 1, 13, 7, INK);
  fill(x, y, 10, 5, WHITE);
  fill(x + 10, y + 1, 1, 3, WHITE);
  fill(x + 1, y + 1, 8, 3, INK);
  int lvl = s.battery < 0 ? 8 : (s.battery * 8 + 50) / 100;
  if (lvl < 1) lvl = 1;
  uint16_t c = s.charging ? rgb(0x48c9f0) : s.battery < 20 ? rgb(0xff453a) : s.battery < 40 ? rgb(0xffd23f) : rgb(0x4cd060);
  fill(x + 1, y + 1, lvl, 3, c);
}

void note(int x, int y, int frame) {
  static const char* const N[] = {"..###", "..#.#", "..#.#", "###..", "###.."};
  y += frame % 2;
  for (int j = -1; j <= 5; j++)
    for (int i = -1; i <= 5; i++) {
      bool on = j >= 0 && j < 5 && i >= 0 && i < 5 && N[j][i] == '#';
      bool near = false;
      for (int b = -1; b <= 1 && !near; b++)
        for (int a = -1; a <= 1 && !near; a++) {
          int jj = j + b, ii = i + a;
          near = jj >= 0 && jj < 5 && ii >= 0 && ii < 5 && N[jj][ii] == '#';
        }
      if (on) put(x + i, y + j, rgb(0xff5f57));
      else if (near) put(x + i, y + j, INK);
    }
}

}  // namespace

Sky skyOf(int c) {
  if (c == 0) return CLEAR;
  if (c == 1 || c == 2) return PARTLY;
  if (c == 3) return CLOUDY;
  if (c == 45 || c == 48) return FOG;
  if (c >= 51 && c <= 57) return DRIZZLE;
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return RAIN;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return SNOW;
  if (c >= 95) return STORM;
  return PARTLY;
}

bool heavyOf(int c) { return c == 65 || c == 67 || c == 75 || c == 82 || c == 86 || c >= 96; }

Outfit outfit(const Scene& s) {
  if (!s.weather) return MILD;
  if (s.sky == SNOW) return SNOWY;
  if (s.sky == RAIN || s.sky == DRIZZLE || s.sky == STORM) return RAINY;
  if (s.temp >= 24) return HOT;
  if (s.temp >= 17) return WARM;
  if (s.temp >= 10) return MILD;
  if (s.temp >= 3) return COOL;
  return COLD;
}

const char* outfitText(Outfit o) {
  switch (o) {
    case HOT: return "T-Shirt, kurze Hose und Kappe";
    case WARM: return "T-Shirt und lange Hose";
    case MILD: return "Pulli und lange Hose";
    case COOL: return "Jacke";
    case COLD: return "Winterjacke, Mütze, Schal und Handschuhe";
    case RAINY: return "Regenjacke und Gummistiefel";
    case SNOWY: return "Schneesachen, Mütze und Stiefel";
    default: return "";
  }
}

void draw(uint16_t* out, const Scene& s) {
  px = out;
  bool flash = s.sky == STORM && s.frame % 14 == 0;
  sky(s, flash);
  if (!s.day && (s.sky == CLEAR || s.sky == PARTLY)) stars(s);
  if (s.day && (s.sky == CLEAR || s.sky == PARTLY)) sun(s.frame);
  if (!s.day && s.sky != STORM && s.sky != RAIN && s.sky != FOG) moon();
  clouds(s);
  ground(s);
  if (s.sky == STORM && s.frame % 14 <= 1) lightning(s.frame);
  if (s.sleeping) bed(s);
  else kid(s, 22, 78);
  if (s.sky == RAIN || s.sky == DRIZZLE || s.sky == STORM) rain(s);
  if (s.sky == SNOW) snow(s);
  if (s.sky == FOG) fog(s);

  // Uhrzeit groß, darunter Wochentag und Datum
  char b[32];
  if (s.timeValid) snprintf(b, sizeof(b), "%d:%02d", s.hour, s.minute);
  else snprintf(b, sizeof(b), "-:--");
  bigLabel(S / 2, 24, b, 3, WHITE);
  if (s.timeValid) {
    static const char* const DAYS[] = {"SONNTAG", "MONTAG", "DIENSTAG", "MITTWOCH", "DONNERSTAG", "FREITAG", "SAMSTAG"};
    snprintf(b, sizeof(b), "%s %d.%d.", DAYS[s.wday % 7], s.mday, s.month);
    smallLabel(S / 2, 49, b, rgb(0xfff2c8));
  }
  // Temperatur neben dem Kind
  if (s.weather) {
    snprintf(b, sizeof(b), "%d^", s.temp);
    bigLabel(84, 86, b, 2, WHITE);
  }
  battery(S / 2, 106, s);
  if (s.playing) note(S / 2 + 10, 105, s.frame);

  // rund: außerhalb des Zifferblatts schwarz, am Rand ein dunkler Ring
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++) {
      float dx = x + 0.5f - S / 2.0f, dy = y + 0.5f - S / 2.0f, d2 = dx * dx + dy * dy;
      if (d2 > R * R) px[y * S + x] = BLACK;
      else if (d2 > (R - 1.2f) * (R - 1.2f)) px[y * S + x] = mix(px[y * S + x], BLACK, 150);
    }
}

}  // namespace faceart
