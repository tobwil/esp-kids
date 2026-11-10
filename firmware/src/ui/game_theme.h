// Themen für das Balance-Spiel (Wiese, Strand, Eis, Weltraum, Holz, Lava, Unterwasser, Zuckerwatte).
#pragma once
#include <stdint.h>

namespace gtheme {

static const int R = 214;  // Spielfeld-Radius in Pixeln
extern const int COUNT;

const char* name(int t);
uint32_t accent(int t);  // Randfarbe der Löcher bei „Alle Kugeln“
// ganzen Bildschirm (LCD_W × LCD_H, RGB565) malen: Schale mit Rand, darin das Thema
void draw(uint16_t* buf, int t, uint32_t seed);

}  // namespace gtheme
