// Vorschau des Zifferblatts am Rechner: alle Wetter, Tag und Nacht, alle Kleidungen und die Schlafenszeit
// als ein Bild (tests/out/zifferblatt.ppm). Prüft dabei, dass nichts außerhalb des Rasters landet.
//   sh tests/run_host.sh
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../firmware/src/faceart.h"

using namespace faceart;

int main() {
  struct Case { Sky sky; bool day; int temp; bool sleeping; int code; };
  const Case cases[] = {
      {CLEAR, true, 27, false, 0},   {PARTLY, true, 19, false, 2},  {CLOUDY, true, 13, false, 3},
      {FOG, true, 6, false, 45},     {DRIZZLE, true, 11, false, 53}, {RAIN, true, 9, false, 63},
      {STORM, true, 16, false, 95},  {SNOW, true, -1, false, 73},   {CLOUDY, true, -4, false, 3},
      {CLEAR, false, 12, false, 0},  {RAIN, false, 8, false, 61},   {PARTLY, false, 4, true, 2},
  };
  const int N = sizeof(cases) / sizeof(cases[0]), COLS = 4, Z = 3, PAD = 6;
  const int ROWS = (N + COLS - 1) / COLS, W = COLS * (S * Z + PAD) + PAD, H = ROWS * (S * Z + PAD) + PAD;
  std::vector<uint8_t> img(W * H * 3, 40);
  // Schutzbereich um das Raster: draw() darf nur die S × S Pixel beschreiben
  const int GUARD = 64;
  std::vector<uint16_t> buf(S * S + 2 * GUARD, 0xBEEF);
  int bad = 0;
  for (int i = 0; i < N; i++) {
    for (int f = 0; f < 30; f++) {  // mehrere Animationsschritte
      Scene s{};
      s.weather = true;
      s.sky = cases[i].sky;
      s.heavy = heavyOf(cases[i].code);
      s.day = cases[i].day;
      s.temp = cases[i].temp;
      s.timeValid = true;
      s.hour = cases[i].day ? 7 + i : 21;
      s.minute = 5 * i;
      s.wday = i % 7;
      s.mday = 10 + i;
      s.month = 11;
      s.sleeping = cases[i].sleeping;
      s.battery = 15 + i * 8;
      s.charging = i == 5;
      s.playing = i % 3 == 0;
      s.frame = f;
      draw(buf.data() + GUARD, s);
      for (int g = 0; g < GUARD; g++)
        if (buf[g] != 0xBEEF || buf[GUARD + S * S + g] != 0xBEEF) bad++;
    }
    int ox = PAD + (i % COLS) * (S * Z + PAD), oy = PAD + (i / COLS) * (S * Z + PAD);
    for (int y = 0; y < S * Z; y++)
      for (int x = 0; x < S * Z; x++) {
        uint16_t c = buf[GUARD + (y / Z) * S + x / Z];
        uint8_t* p = &img[((oy + y) * W + ox + x) * 3];
        p[0] = (c >> 11) * 255 / 31;
        p[1] = ((c >> 5) & 63) * 255 / 63;
        p[2] = (c & 31) * 255 / 31;
      }
    Scene t{};
    t.weather = true;
    t.sky = cases[i].sky;
    t.temp = cases[i].temp;
    printf("%-8s %3d °C  %s\n", cases[i].day ? "Tag" : "Nacht", cases[i].temp, cases[i].sleeping ? "schläft" : outfitText(outfit(t)));
  }
  FILE* f = fopen(OUT, "wb");
  if (!f) { perror(OUT); return 1; }
  fprintf(f, "P6 %d %d 255\n", W, H);
  fwrite(img.data(), 1, img.size(), f);
  fclose(f);
  if (bad) { printf("FEHLER: %d Pixel außerhalb des Rasters beschrieben\n", bad); return 1; }
  printf("ok – %s\n", OUT);
  return 0;
}
