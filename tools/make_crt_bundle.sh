#!/bin/sh
# Erzeugt die Stamm-Zertifikate für HTTPS (Radio, Audiothek, Cover):
#   firmware/x509_crt_bundle   Format von ESP-IDF 5.5 (für den Arduino-Core)
# Quelle: die Zertifikatsliste von ESP-IDF (enthält auch Wurzeln wie „GlobalSign Root CA“, die certifi nicht mehr hat).
#   sh tools/make_crt_bundle.sh
set -e
IDF=v5.5.5
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
python3 -m venv "$TMP/venv" && "$TMP/venv/bin/pip" -q install cryptography
BASE="https://raw.githubusercontent.com/espressif/esp-idf/$IDF/components/mbedtls/esp_crt_bundle"
curl -sfL -o "$TMP/cacert.pem" "$BASE/cacrt_all.pem"
curl -sfL -o "$TMP/gen_crt_bundle.py" "$BASE/gen_crt_bundle.py"
(cd "$TMP" && "$TMP/venv/bin/python" gen_crt_bundle.py -i cacert.pem)
cp "$TMP/x509_crt_bundle" "$ROOT/firmware/x509_crt_bundle"
echo "firmware/x509_crt_bundle aktualisiert"
