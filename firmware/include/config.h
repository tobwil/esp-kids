// Voreinstellungen – die meisten Werte lassen sich in der Weboberfläche ändern und bleiben gespeichert.
#pragma once

// WLAN (optional). Leer lassen = an der Uhr einrichten (Zifferblatt lange drücken → Eltern → WLAN).
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Zeitzone Deutschland (inkl. Sommerzeit)
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"

// Display aus nach so vielen Sekunden ohne Bedienung (in der Weboberfläche änderbar)
#define SCREEN_TIMEOUT_S 30
// Tiefschlaf nach so vielen Minuten mit dunklem Display (Standard, in der Weboberfläche änderbar; nur im
// Akkubetrieb, nicht während Musik läuft). 0 = nie: die Uhr döst im Leichtschlaf und bleibt für die Weboberfläche
// erreichbar. Im Tiefschlaf ist das WLAN aus, dann weckt nur BOOT.
#define DEEP_SLEEP_MIN 0

#define VOLUME_DEFAULT 60      // %
#define BRIGHTNESS_DEFAULT 80  // %

// Touch-Ausrichtung (wie in den Waveshare-Beispielen des jeweiligen Boards)
#if defined(BOARD_AMOLED_216)
#define TOUCH_SWAP_XY 1
#define TOUCH_MIRROR_X 1
#define TOUCH_MIRROR_Y 0
#else
#define TOUCH_SWAP_XY 0
#define TOUCH_MIRROR_X 1
#define TOUCH_MIRROR_Y 1
#endif

// Balance-Spiel: Richtung der Neigung. Rollt die Kugel verkehrt herum, das Vorzeichen umdrehen.
#define IMU_SIGN_X 1
#define IMU_SIGN_Y 1

#define FW_VERSION "1.0.0"
