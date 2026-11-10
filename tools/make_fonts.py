#!/usr/bin/env python3
"""Erzeugt die LVGL-Schriften in firmware/src/fonts/ (braucht Node.js für lv_font_conv).

    python3 tools/make_fonts.py

Text: Inter (OFL), Symbole: Material Icons Round (Apache 2.0). Das Zifferblatt zeichnet seine Pixel-Schrift selbst.
Die Symbole werden in die Textschriften eingemischt, damit Labels Text und Icons kombinieren können.
"""
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "fonts")
OUT = os.path.join(ROOT, "firmware", "src", "fonts")

INTER_500 = os.path.join(SRC, "inter-latin-500-normal.woff")
INTER_600 = os.path.join(SRC, "inter-latin-600-normal.woff")
ICONS = os.path.join(SRC, "MaterialIconsRound-Regular.otf")

# Latin-1 plus typografische Zeichen („“ – … •)
LATIN = "0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201A,0x201C,0x201D,0x201E,0x2022,0x2026,0x20AC"

ICON_NAMES = {
    "MUSIC": 0xE405, "WIFI": 0xE63E, "WIFI_OFF": 0xE648, "PLAY": 0xE037, "PAUSE": 0xE034, "NEXT": 0xE044,
    "PREV": 0xE045, "VOL_UP": 0xE050, "VOL_DOWN": 0xE04D, "VOL_OFF": 0xE04F, "RADIO": 0xEBBA, "LIBRARY": 0xE030,
    "BRIGHT": 0xE518, "SLEEP": 0xEF44, "INFO": 0xE88E, "POWER": 0xE8AC, "RESTART": 0xF053, "CHECK": 0xE5CA,
    "CLOSE": 0xE5CD, "PLUS": 0xE145, "MINUS": 0xE15B, "LOCK": 0xE897, "BELL": 0xE7F4, "GROUP": 0xE7EF,
    "BACKSPACE": 0xE14A, "SERVER": 0xE875, "REPLAY": 0xE042, "BATTERY": 0xE1A4, "CHARGING": 0xE1A3, "HISTORY": 0xE889,
    "SHIFT": 0xE318, "ALBUM": 0xE019, "REFRESH": 0xE5D5, "FLAG": 0xE153, "BOLT": 0xEA0B, "SCREEN_OFF": 0xE51C,
    "GAME": 0xEA28,
}
ICON_RANGE = ",".join(hex(c) for c in sorted(set(ICON_NAMES.values())))

FONTS = [
    # Name, Größe, [(Datei, Bereich), ...]
    ("font_text_20", 20, [(INTER_500, LATIN), (ICONS, ICON_RANGE)]),
    ("font_text_24", 24, [(INTER_600, LATIN), (ICONS, ICON_RANGE)]),
    ("font_text_32", 32, [(INTER_600, LATIN), (ICONS, ICON_RANGE)]),
    ("font_num_80", 80, [(INTER_600, "0x20,0x2D,0x2E,0x30-0x3A")]),
    ("font_icon_40", 40, [(ICONS, ICON_RANGE)]),
    ("font_icon_64", 64, [(ICONS, ICON_RANGE)]),
]


def utf8(cp):
    return "".join(f"\\x{b:02X}" for b in chr(cp).encode("utf-8"))


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, size, parts in FONTS:
        cmd = ["npx", "--yes", "lv_font_conv@1.5.3", "--bpp", "4", "--size", str(size), "--format", "lvgl",
               "--no-compress", "--lv-include", "lvgl.h", "--lv-font-name", name, "-o", os.path.join(OUT, name + ".c")]
        for path, rng in parts:
            cmd += ["--font", path, "-r", rng]
        print(name)
        subprocess.run(cmd, check=True)

    with open(os.path.join(OUT, "fonts.h"), "w") as f:
        f.write("// Erzeugt von tools/make_fonts.py – nicht von Hand ändern.\n#pragma once\n#include <lvgl.h>\n\n")
        for name, _, _ in FONTS:
            f.write(f"LV_FONT_DECLARE({name});\n")
        f.write("\n// Material Icons Round als UTF-8 (in allen Text- und Icon-Schriften enthalten)\n")
        for k, cp in ICON_NAMES.items():
            f.write(f'#define I_{k} "{utf8(cp)}"\n')


if __name__ == "__main__":
    main()
