// Untergründe für das Balance-Spiel: jede Stufe ein eigenes Thema, Pixel für Pixel in einen Puffer gemalt.
#include "game_theme.h"
#include <math.h>
#include <Arduino.h>
#include "pins.h"

namespace {

constexpr int N = LCD_W, C = LCD_W / 2;
uint16_t* buf;
uint32_t rs;

constexpr uint16_t rgb(uint32_t c) { return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F); }

uint16_t mixc(uint32_t a, uint32_t b, float t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  int r = ((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t;
  int g = ((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t;
  int bl = (a & 255) * (1 - t) + (b & 255) * t;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (bl >> 3);
}

uint32_t hash(int x, int y, uint32_t s) {
  uint32_t h = x * 374761393u + y * 668265263u + s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
float h01(int x, int y, uint32_t s) { return (hash(x, y, s) & 0xFFFF) / 65535.0f; }

// weiches Rauschen: Zufallswerte auf einem Gitter, dazwischen geglättet
float vnoise(float x, float y, float cell, uint32_t s) {
  x /= cell; y /= cell;
  int ix = (int)floorf(x), iy = (int)floorf(y);
  float fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  float a = h01(ix, iy, s), b = h01(ix + 1, iy, s), c = h01(ix, iy + 1, s), d = h01(ix + 1, iy + 1, s);
  return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}
float fbm(float x, float y, float cell, uint32_t s) {
  return vnoise(x, y, cell, s) * 0.6f + vnoise(x, y, cell / 2.3f, s + 7) * 0.3f + vnoise(x, y, cell / 5.1f, s + 13) * 0.1f;
}

float rnd() { rs = rs * 1103515245u + 12345u; return ((rs >> 8) & 0xFFFF) / 65535.0f; }

bool inField(int x, int y, int r = gtheme::R) { int dx = x - C, dy = y - C; return dx * dx + dy * dy < r * r; }
void px(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < N && y < N && inField(x, y)) buf[y * N + x] = c; }
void dot(float cx, float cy, float r, uint16_t c) {
  for (int y = (int)(cy - r); y <= (int)(cy + r) + 1; y++)
    for (int x = (int)(cx - r); x <= (int)(cx + r) + 1; x++)
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) px(x, y, c);
}
void ring(float cx, float cy, float r, float w, uint16_t c) {
  for (int y = (int)(cy - r - w); y <= (int)(cy + r + w) + 1; y++)
    for (int x = (int)(cx - r - w); x <= (int)(cx + r + w) + 1; x++) {
      float d = hypotf(x - cx, y - cy);
      if (fabsf(d - r) <= w / 2) px(x, y, c);
    }
}
void line(float x0, float y0, float x1, float y1, float w, uint16_t c) {
  int n = (int)(hypotf(x1 - x0, y1 - y0) + 1);
  for (int i = 0; i <= n; i++) dot(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, w / 2, c);
}
// zufälliger Punkt im Feld (mit Abstand zum Rand)
void spot(float& x, float& y, float margin = 20) {
  do { x = C + (rnd() * 2 - 1) * gtheme::R; y = C + (rnd() * 2 - 1) * gtheme::R; } while (!inField(x, y, gtheme::R - margin));
}

// ------------------------------------------------------------------ Themen

void meadow(uint32_t s) {
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      if (!inField(x, y)) continue;
      float n = fbm(x, y, 70, s), g = h01(x, y / 3, s) > 0.86f ? 0.18f : 0;
      buf[y * N + x] = mixc(0x3b8a35, 0x6cc04f, n - g);
    }
  static const uint32_t PET[] = {0xffffff, 0xffd23f, 0xff8fc6, 0xb69cff};
  for (int i = 0; i < 46; i++) {
    float x, y;
    spot(x, y);
    uint16_t c = rgb(PET[i % 4]);
    for (int k = 0; k < 5; k++) dot(x + cosf(k * 1.2566f) * 3.5f, y + sinf(k * 1.2566f) * 3.5f, 2.6f, c);
    dot(x, y, 2, rgb(i % 4 == 1 ? 0xff8a2a : 0xffd23f));
  }
}

void beach(uint32_t s) {
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      if (!inField(x, y)) continue;
      float n = fbm(x, y, 60, s) + (h01(x, y, s) - 0.5f) * 0.25f;
      // Rippel vom Wind
      float rip = sinf((x * 0.8f + y * 0.6f) * 0.09f + n * 6) * 0.08f;
      buf[y * N + x] = mixc(0xd2ae6e, 0xf3dfae, n + rip);
    }
  for (int i = 0; i < 9; i++) {  // Muscheln
    float x, y;
    spot(x, y);
    uint16_t c = rgb(i % 2 ? 0xfbe3d8 : 0xf4b7a8);
    for (int k = -2; k <= 2; k++) line(x, y + 6, x + k * 3.5f, y - 3, 2.2f, c);
    dot(x, y + 6, 2, rgb(0xd99a86));
  }
  for (int i = 0; i < 4; i++) {  // Seesterne
    float x, y;
    spot(x, y);
    for (int k = 0; k < 5; k++) line(x, y, x + cosf(k * 1.2566f + i) * 11, y + sinf(k * 1.2566f + i) * 11, 5, rgb(0xff8a3c));
    dot(x, y, 4, rgb(0xff9f55));
  }
}

void ice(uint32_t s) {
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      if (!inField(x, y)) continue;
      float n = fbm(x, y, 90, s);
      buf[y * N + x] = mixc(0xbfe3f5, 0xf5fcff, n + (h01(x, y, s) > 0.995f ? 0.4f : 0));
    }
  for (int i = 0; i < 7; i++) {  // Risse
    float x, y, a = rnd() * 6.28f;
    spot(x, y);
    for (int k = 0; k < 9; k++) {
      float nx = x + cosf(a) * 14, ny = y + sinf(a) * 14;
      line(x, y, nx, ny, 1.4f, rgb(0x8fb8d0));
      if (rnd() < 0.3f) line(nx, ny, nx + cosf(a + 1) * 10, ny + sinf(a + 1) * 10, 1, rgb(0xa7c9dc));
      x = nx; y = ny; a += (rnd() - 0.5f) * 1.2f;
    }
  }
}

void space(uint32_t s) {
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      if (!inField(x, y)) continue;
      float n = fbm(x, y, 120, s), m = fbm(x + 300, y, 90, s + 3);
      uint16_t c = mixc(0x04051a, 0x2a1660, n * n * 1.6f);
      c = n > 0.62f ? mixc(0x2a1660, 0x6a2a7a, (m - 0.4f) * 2) : c;
      float st = h01(x, y, s);
      if (st > 0.9975f) c = rgb(0xffffff);
      else if (st > 0.994f) c = rgb(0x8a8fd0);
      buf[y * N + x] = c;
    }
  float x, y;
  spot(x, y, 70);
  dot(x, y, 26, rgb(0xe08a4a));  // Planet mit Ring
  for (int k = -26; k < 26; k += 7) line(x - 25, y + k, x + 25, y + k + 3, 2, rgb(0xc8703a));
  for (float a = 0; a < 6.28f; a += 0.01f) {
    float rx = x + cosf(a) * 44, ry = y + sinf(a) * 11;
    if (sinf(a) > 0 || hypotf(rx - x, ry - y) > 27) dot(rx, ry, 1.6f, rgb(0xf3d39a));
  }
}

void wood(uint32_t s) {
  float ox = C + 380, oy = C - 120;  // Mitte der Jahresringe liegt außerhalb
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      if (!inField(x, y)) continue;
      float d = hypotf(x - ox, (y - oy) * 0.35f) + fbm(x, y, 50, s) * 30;
      float band = 0.5f + 0.5f * sinf(d * 0.32f);
      float grain = h01(x / 2, y, s) * 0.12f;
      buf[y * N + x] = mixc(0x8a5630, 0xc99560, band * 0.75f + grain);
    }
  for (int i = 0; i < 2; i++) {  // Astlöcher
    float x, y;
    spot(x, y, 40);
    for (int r = 10; r > 0; r -= 3) ring(x, y, r, 2, rgb(r % 2 ? 0x6a3c20 : 0x8a5630));
  }
}

void lava(uint32_t s) {
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      if (!inField(x, y)) continue;
      float n = fbm(x, y, 80, s), v = fabsf(vnoise(x, y, 55, s + 9) - 0.5f);
      uint16_t c = mixc(0x1f1512, 0x4a3026, n);
      if (v < 0.045f) c = mixc(0xff3a10, 0xffd04a, 1 - v / 0.045f);
      else if (v < 0.075f) c = mixc(0x5a2010, 0x1f1512, (v - 0.045f) / 0.03f);
      buf[y * N + x] = c;
    }
}

void sea(uint32_t s) {
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      if (!inField(x, y)) continue;
      float t = (float)y / N, n = fbm(x, y, 70, s);
      float caustic = fabsf(sinf(x * 0.05f + n * 7) + sinf(y * 0.045f - n * 6)) < 0.18f ? 0.25f : 0;
      buf[y * N + x] = mixc(0x1477a8, 0x07365a, t + (n - 0.5f) * 0.3f - caustic);
    }
  for (int i = 0; i < 14; i++) {  // Blasen
    float x, y;
    spot(x, y);
    float r = 3 + rnd() * 7;
    ring(x, y, r, 1.6f, rgb(0x9fdcf5));
    dot(x - r * 0.35f, y - r * 0.35f, 1.2f, rgb(0xe0f6ff));
  }
  for (int i = 0; i < 3; i++) {  // Fische
    float x, y;
    spot(x, y, 40);
    uint16_t c = rgb(i % 2 ? 0xffb020 : 0xff6a5a);
    for (int k = -9; k <= 9; k++) dot(x + k, y, 5.5f * sqrtf(1 - (k / 10.0f) * (k / 10.0f)), c);
    line(x + 10, y, x + 17, y - 6, 3, c);
    line(x + 10, y, x + 17, y + 6, 3, c);
    dot(x - 5, y - 1, 1.5f, rgb(0x102030));
  }
}

void candy(uint32_t s) {
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      if (!inField(x, y)) continue;
      float n = fbm(x, y, 90, s);
      buf[y * N + x] = mixc(0xffb3d4, 0xffe1ee, n);
    }
  static const uint32_t SPR[] = {0xff4f8b, 0x4fc3ff, 0xffd23f, 0x7ad35c, 0xffffff, 0xa36bff};
  for (int i = 0; i < 140; i++) {  // Streusel
    float x, y, a = rnd() * 3.14f;
    spot(x, y, 8);
    line(x, y, x + cosf(a) * 7, y + sinf(a) * 7, 3, rgb(SPR[i % 6]));
  }
}

struct T { const char* name; void (*draw)(uint32_t); uint32_t rim, accent; };
const T THEMES[] = {
    {"Wiese", meadow, 0x2f6a2a, 0x5a3a1c},
    {"Strand", beach, 0xb08850, 0x8a6a3a},
    {"Eis", ice, 0x8fb8d0, 0x5a8aa8},
    {"Weltraum", space, 0x4a4a8a, 0x8a8fd0},
    {"Holz", wood, 0x5a3418, 0x3a2410},
    {"Lava", lava, 0xff5a1a, 0x5a2010},
    {"Unterwasser", sea, 0x0a4a70, 0x9fdcf5},
    {"Zuckerwatte", candy, 0xff6fa5, 0xff4f8b},
};

}  // namespace

namespace gtheme {

const int COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

const char* name(int t) { return THEMES[t % COUNT].name; }
uint32_t accent(int t) { return THEMES[t % COUNT].accent; }

void draw(uint16_t* out, int t, uint32_t seed) {
  buf = out;
  rs = seed | 1;
  const T& th = THEMES[t % COUNT];
  // außen schwarz, dann Rand der Schale, dann das Thema
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      int dx = x - C, dy = y - C, d2 = dx * dx + dy * dy;
      buf[y * N + x] = d2 < (R + 8) * (R + 8) ? mixc(th.rim, 0x000000, d2 < (R + 3) * (R + 3) ? 0.45f : 0) : 0;
    }
  th.draw(seed);
  // leichter Schatten am Innenrand: die Schale wirkt vertieft
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float d = hypotf(x - C, y - C);
      if (d < R && d > R - 14) {
        uint16_t c = buf[y * N + x];
        float k = (d - (R - 14)) / 14 * 0.45f;
        int r = (c >> 11) * (1 - k), g = ((c >> 5) & 63) * (1 - k), b = (c & 31) * (1 - k);
        buf[y * N + x] = (r << 11) | (g << 5) | b;
      }
    }
}

}  // namespace gtheme
