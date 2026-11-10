#!/bin/sh
# Tests am Rechner: Zifferblatt (alle Wetter, Kleidung, Schlafenszeit, nichts außerhalb des Rasters) als Vorschaubild,
# Skript der Weboberfläche, und beide Firmware-Builds.
#   sh tests/run_host.sh            alles
#   sh tests/run_host.sh fast       ohne Firmware-Builds
set -e
cd "$(dirname "$0")/.."
mkdir -p tests/out
c++ -std=c++17 -O1 -Wall -Wextra -Werror -DOUT='"tests/out/zifferblatt.ppm"' tests/face_preview.cpp firmware/src/faceart.cpp -o tests/out/face_preview
./tests/out/face_preview
if command -v sips >/dev/null; then sips -s format png tests/out/zifferblatt.ppm --out tests/out/zifferblatt.png >/dev/null && echo "Vorschau: tests/out/zifferblatt.png"; fi
python3 tests/check_webui.py
[ "$1" = "fast" ] && exit 0
for env in amoled-175c amoled-216; do
  sh tools/build.sh "$env" > tests/out/build-$env.log 2>&1 && echo "Build $env: ok" || { echo "Build $env: FEHLER (tests/out/build-$env.log)"; exit 1; }
done
