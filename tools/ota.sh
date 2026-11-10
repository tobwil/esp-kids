#!/bin/sh
# Firmware per WLAN aufspielen (statt USB):
#   sh tools/ota.sh <Adresse der Uhr> [amoled-175c|amoled-216] [Eltern-PIN]
# z. B.  sh tools/ota.sh pixeluhr-e008.local amoled-175c 1234
# Die Uhr prüft, ob die Firmware zu ihrem Board passt, installiert sie und startet neu.
set -e
HOST=${1:?Adresse der Uhr fehlt}
ENV=${2:-amoled-175c}
PIN=${3:-}
BIN=$(sh "$(dirname "$0")/build.sh" "$ENV" | tail -1 | sed 's/^Firmware: //')
# eine dösende Uhr antwortet manchmal träge – bis zu dreimal versuchen
for i in 1 2 3; do
  R=$(curl -sSk --max-time 180 -H "X-Pin: $PIN" -F "file=@$BIN" "https://$HOST/api/update") && echo "$R" && echo "$R" | grep -q installiert && exit 0
  echo "Versuch $i fehlgeschlagen" >&2
  sleep 3
done
exit 1
