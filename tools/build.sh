#!/bin/sh
# Baut die Firmware (und lädt sie auf Wunsch hoch). ESP-IDF verträgt keine Leerzeichen im Projektpfad –
# liegt das Projekt in so einem Ordner, wird eine Kopie unter /tmp gebaut.
#   sh tools/build.sh                       baut für die runde Uhr (1.75C)
#   sh tools/build.sh amoled-216            baut für die eckige Uhr (2.16)
#   sh tools/build.sh amoled-175c upload    baut und lädt per USB hoch
set -e
ENV=${1:-amoled-175c}
DIR=$(cd "$(dirname "$0")/.." && pwd)
PIO=${PIO:-$HOME/.platformio/penv/bin/pio}
SRC="$DIR/firmware"
case "$SRC" in
  *" "*)
    BUILD=/tmp/pixeluhr-build/firmware
    mkdir -p "$BUILD"
    rsync -a --delete --exclude .pio --exclude .dummy --exclude managed_components "$SRC/" "$BUILD/"
    SRC=$BUILD ;;
esac
cd "$SRC"
if [ "$2" = "upload" ]; then "$PIO" run -e "$ENV" -t upload; else "$PIO" run -e "$ENV"; fi
echo "Firmware: $SRC/.pio/build/$ENV/firmware.bin"
