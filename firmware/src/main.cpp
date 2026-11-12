// Pixeluhr – eine Kinderuhr für das Waveshare ESP32-S3-Touch-AMOLED-1.75C (rund) und -2.16 (eckig):
// 8-Bit-Zifferblatt mit Uhrzeit und Wetter, das Kugelspiel Balance, Radio und Hörspiele aus der ARD Audiothek.
// Spielen und Hören gibt es nur mit Freigabe der Eltern (PIN an der Uhr oder Weboberfläche) und höchstens so viele
// Minuten pro Tag, wie eingestellt ist.
//
// Bedienung:
//   Zifferblatt: antippen oder hochwischen = Apps · runterwischen = Kontrollzentrum · 1,5 s halten = Eltern-Menü
//   überall: nach rechts wischen = zurück · BOOT kurz = Apps bzw. zurück zum Zifferblatt
//   PWR kurz = Display an/aus · PWR lang = Ausschalten/Neustart
#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <mbedtls/platform.h>
#include "audio.h"
#include "config.h"
#include "hw.h"
#include "imu.h"
#include "kids.h"
#include "music.h"
#include "net.h"
#include "pins.h"
#include "power.h"
#include "quota.h"
#include "server.h"
#include "settings.h"
#include "weather.h"
#include "webui.h"
#include "ui/ui.h"

// Grund des letzten Starts (Status, Weboberfläche): ein „Absturz“ hier heißt, die Uhr ist von selbst neu gestartet
const char* resetText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Eingeschaltet";
    case ESP_RST_SW: return "Neustart";
    case ESP_RST_DEEPSLEEP: return "Aus dem Tiefschlaf";
    case ESP_RST_PANIC: return "Absturz";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "Wächter (hing)";
    case ESP_RST_BROWNOUT: return "Spannung zu niedrig";
    case ESP_RST_USB: return "USB";
    default: return "unbekannt";
  }
}

// Kennung des Boards in der Firmware: Updates per WLAN prüfen sie (webui.cpp)
extern const char TF_BOARD_TAG[];
const char TF_BOARD_TAG[] = "TFBOARD=" BOARD_NAME ";";

static quota::App appOf(const char* name) {
  return !strcmp(name, "game") ? quota::GAME : !strcmp(name, "radio") ? quota::RADIO : !strcmp(name, "stories") ? quota::STORIES : quota::APPS;
}

// Serielle Befehle für Tests über USB (zusätzlich zu tap/swipe/shot in hw.cpp)
bool appCommand(const char* line) {
  char a[100], b[100];
  int n;
  if (sscanf(line, "wifi %99s %99[^\n]", a, b) >= 1) {
    if (sscanf(line, "wifi %99s %99[^\n]", a, b) == 1) b[0] = 0;
    knownAdd(a, b);
    settingsSave();
    netConnect(a, b);
    Serial.printf("WLAN %s gespeichert\n", a);
  } else if (sscanf(line, "browse %99[^\n]", a) == 1) browseOpen(a);
  else if (!strcmp(line, "toggle")) music::toggle();
  else if (!strcmp(line, "next")) music::next();
  else if (!strcmp(line, "stop")) music::stop();
  else if (sscanf(line, "vol %d", &n) == 1) ui::volumeSet(n);
  else if (sscanf(line, "fx %d", &n) == 1) audio::play((audio::Fx)n);
  else if (!strcmp(line, "home")) ui::home();
  else if (sscanf(line, "page %99s", a) == 1) {
    const ui::Page* p = !strcmp(a, "apps") ? &PAGE_APPS : !strcmp(a, "music") ? &PAGE_MUSIC : !strcmp(a, "game") ? &PAGE_GAME
                      : !strcmp(a, "control") ? &PAGE_CONTROL : !strcmp(a, "parent") ? &PAGE_PARENT : nullptr;
    if (p) ui::push(p);
  } else if (sscanf(line, "open %99s", a) == 1 && appOf(a) < quota::APPS) ui::openApp(appOf(a));
  // Freigaben: „unlock game 15“ (Minuten, −1 = bis zur Bettzeit), „lock radio“, „used stories 30“ (heute genutzt)
  else if (sscanf(line, "unlock %99s %d", a, &n) == 2 && appOf(a) < quota::APPS) { quota::unlock(appOf(a), n, false); Serial.println("ok"); }
  else if (sscanf(line, "lock %99s", a) == 1 && appOf(a) < quota::APPS) { quota::lock(appOf(a)); Serial.println("ok"); }
  else if (sscanf(line, "used %99s %d", a, &n) == 2 && appOf(a) < quota::APPS) { quota::testUsed(appOf(a), n); Serial.println("ok"); }
  // Testwetter fürs Zifferblatt: „weather 63 9 day“ (WMO-Code, °C, day/night), „weather off“ = echtes Wetter
  else if (!strcmp(line, "weather off")) faceTest(-1, 0, true);
  else if (int t; sscanf(line, "weather %d %d %99s", &n, &t, a) == 3) faceTest(n, t, strcmp(a, "night"));
  else if (sscanf(line, "level %d", &n) == 1) gameTestLevel(n);
  else if (!strcmp(line, "wlan drop")) { WiFi.disconnect(); Serial.println("WLAN getrennt (Test)"); }  // Aussetzer vortäuschen
  else if (!strcmp(line, "wlan roam")) netRoamNow();
  else if (!strcmp(line, "wlan")) Serial.printf("WLAN: %s, %d dBm, Abbrüche: %s\n", netAp().c_str(), netRssi(), netDrops().c_str());
  // Eltern-PIN setzen bzw. löschen (nur über USB, zum Testen): „pin 1234“, „pin -“
  else if (sscanf(line, "pin %8s", a) == 1) {
    kids::Cfg c = kids::cfg();
    snprintf(c.pin, sizeof(c.pin), "%s", strcmp(a, "-") ? a : "");
    kids::set(c);
    Serial.println("ok");
  }
  else if (!strcmp(line, "wake")) ui::wake();
  else if (!strcmp(line, "tilt off")) imu::fake(false);  // Test: Kipp-Spiel ohne Kippen
  else if (sscanf(line, "tilt %99s %99s", a, b) == 2) imu::fake(true, atof(a), atof(b));
  else if (!strcmp(line, "imu")) { imu::sleep(false); delay(50); float x, y; imu::gravity(x, y); Serial.printf("Neigung: x %.2f y %.2f\n", x, y); }
  else if (!strncmp(line, "lightsleep usb ", 15)) { pwr::lightSleepOnUsb(!strcmp(line + 15, "on")); Serial.println("ok"); }
  else if (!strcmp(line, "poweroff")) {  // ausschalten; hängt USB dran und startet die PMU neu: Tiefschlaf
    pwr::save();
    audio::shutdown();
    hwPowerOff();
    delay(1500);
    hwDeepSleep(0);
  } else if (!strcmp(line, "status")) {
    music::State s = music::state();
    Serial.printf("WLAN %s %s · Musik %s %s%s%s · Lautstärke %d%% · Akku %d%% · Heap %u · Laufzeit %lu s (%s)",
                  netOnline() ? "an" : "aus", netOnline() ? netIp().c_str() : "", s.active ? (s.playing ? "spielt" : "Pause") : "aus",
                  s.title, s.msg[0] ? " – " : "", s.msg, cfg.volume, hwBattery(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)(millis() / 1000), resetText());
    for (int i = 0; i < quota::APPS; i++)
      Serial.printf(" · %s %d/%d min, Status %d, noch %ld s", quota::name((quota::App)i), quota::usedMin((quota::App)i),
                    quota::limitMin((quota::App)i), (int)quota::status((quota::App)i), quota::leftS((quota::App)i));
    Serial.println();
  } else return false;
  return true;
}

// TLS-Puffer (je Verbindung 20–40 KB) ins PSRAM: sonst reicht der interne RAM nicht für Stream und Cover gleichzeitig
static void* tlsCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void setup() {
  Serial.begin(115200);
  mbedtls_platform_set_calloc_free(tlsCalloc, free);
  settingsLoad();
  kids::begin();
  quota::begin();
  cfg.volume = kids::limitVolume(cfg.volume);
  hwBegin();
  audio::begin(cfg.volume);
  imu::begin();
  netBegin();
  music::begin();
  weather::begin();
  pwr::begin();
  webui::begin();
  ui::begin();
  lv_timer_handler();
  // nach einer Etappe der Nachtruhe aufgewacht und es ist noch Nacht: dunkel bleiben (sie schläft gleich weiter)
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && pwr::nightRest(nullptr)) hwScreen(false);
  hwBrightness(cfg.brightness);
  Serial.printf("Pixeluhr %s bereit (%s) – Start: %s\n", FW_VERSION, cfg.name, resetText());
  // Wächter für die Hauptschleife: hängt sie länger als 60 s (z. B. ein Treiber, der nicht zurückkehrt), startet die
  // Uhr neu statt dunkel und unerreichbar zu bleiben. 60 s, weil manche Web-Anfragen die Schleife bis 20 s belegen.
  esp_task_wdt_config_t wdt = {.timeout_ms = 60000, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) == ESP_ERR_INVALID_STATE) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(nullptr);
}

// Hören zählt aufs Zeitkonto. Ist die Zeit um (Freigabe abgelaufen, Tageslimit erreicht, Schlafenszeit), wird
// sanft ausgeblendet; zwei Minuten vorher kommt ein Hinweis. Was die Eltern selbst gestartet haben, läuft weiter.
static void listening() {
  static uint32_t last = 0;
  static bool warned = false;
  uint32_t now = millis();
  uint32_t dt = now - last;
  last = now;
  music::State s = music::state();
  if (!s.active) { warned = false; return; }
  quota::App a = s.live ? quota::RADIO : quota::STORIES;
  if (s.playing && dt < 5000) quota::use(a, dt);
  if (music::parentStarted()) return;
  long left = quota::leftS(a);
  if (left < 0 || left > 120) { warned = false; return; }
  if (!warned && left > 0) {
    warned = true;
    char b[64];
    snprintf(b, sizeof(b), "Noch 2 Minuten %s", quota::name(a));
    ui::toast(b, 4000);
  }
  if (left == 0 && s.playing && !kids::night()) {  // nachts blendet kids::poll() nach der Einschlafzeit aus
    music::fadeOut();
    static uint32_t toldAt = 0;
    if (now - toldAt > 30000) {
      toldAt = now;
      ui::toast(quota::status(a) == quota::USED_UP ? "Für heute ist die Hörzeit um" : "Die Hörzeit ist um", 4000);
    }
  }
}

static void power() {
  // Display aus nach der eingestellten Zeit ohne Bedienung
  uint32_t off = pwr::screenOffS();
  if (hwScreenOn() && off && hwIdleMs() > off * 1000UL) hwScreen(false);
  // Nachtruhe: Tiefschlaf bis zum Aufstehen, in Etappen von höchstens 2 h – dazwischen stellt sich die Uhr per
  // WLAN nach, denn der Zeitgeber im Tiefschlaf geht ungenau.
  uint32_t nightLeft;
  if (millis() > 30000 && !hwScreenOn() && !hwUsb() && hwBattery() >= 0 && pwr::nightRest(&nightLeft) &&
      !music::state().playing) {
    if (music::state().active) music::stop();
    pwr::save();
    audio::shutdown();
    hwDeepSleep(max<uint32_t>(min<uint32_t>(nightLeft, 2 * 3600), 30));
  }
  // Tiefschlaf im Akkubetrieb, wenn nichts mehr läuft (pausierte Musik hält die Uhr nicht wach);
  // der Grund, warum sie (noch) wach ist, steht in der Weboberfläche
  const char* why = nullptr;
  if (hwScreenOn()) why = "Display an";
  else if (!pwr::deepSleepMin()) why = "nie";
  else if (hwUsb() || hwBattery() < 0) why = "am Kabel";
  else if (music::state().playing) why = "Musik läuft";
  long left = why ? -1 : ((long)(off + pwr::deepSleepMin() * 60UL) * 1000L - (long)hwIdleMs()) / 1000;
  pwr::sleepInfo(why, left);
  if (!why && left <= 0) {
    if (music::state().active) music::stop();  // merkt sich die Stelle
    pwr::save();
    audio::shutdown();
    hwDeepSleep(0);
  }
}

void loop() {
  esp_task_wdt_reset();
  hwLoop();
  BtnEvent b;
  while ((b = hwButton()) != BTN_NONE) {
    if (b == PWR_SHORT) {
      hwScreen(!hwScreenOn());
      if (hwScreenOn()) ui::wake();
      continue;
    }
    if (!hwScreenOn() && b == BOOT_SHORT) { hwScreen(true); ui::wake(); continue; }
    hwActivity();
    ui::onButton(b);
  }
  if (hwWakeRequested()) { hwScreen(true); ui::wake(); }

  // Dösen (dunkel, nichts läuft): der Rest alle 500 ms; dazwischen schläft die Hauptschleife, bis der Touch-Chip,
  // BOOT oder eine Web-Anfrage sie weckt (hwDozeWait). Nach einer Berührung 0,8 s lang alle 20 ms den Touch
  // lesen – so wird der Doppeltipp erkannt.
  static uint32_t dozeAt = 0;
  if (pwr::dozing() && millis() - dozeAt < 500) {
    hwTouchPoll();
    pwr::poll();
    uint32_t used = millis() - dozeAt;  // kann schon über 500 liegen – dann nicht warten
    if (hwTouchRecent()) delay(20);
    else if (used < 500) hwDozeWait(500 - used);
    return;
  }
  dozeAt = millis();

  static uint32_t slow = 0;
  if (millis() - slow > 1000) {
    slow = millis();
    netPoll();
    power();
    quota::poll();
  }
  music::poll();
  listening();
  kids::poll();
  pwr::poll();
  server::poll();
  webui::poll();
  ui::tick();
  uint32_t next = lv_timer_handler();
  delay(min<uint32_t>(next, hwScreenOn() ? 5 : 20));  // dunkel: seltener aufwachen
}
