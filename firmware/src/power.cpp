#include "power.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_pm.h>
#include "audio.h"
#include "config.h"
#include "hw.h"
#include "kids.h"
#include "music.h"
#include "net.h"
#include "settings.h"
#include "ui/ui.h"

namespace pwr {

static Mode m = M_AUTO;
static uint8_t deepMin = DEEP_SLEEP_MIN;
static bool nightOn = true;  // Nachtruhe: von Bettzeit + Einschlafzeit bis zum Aufstehen im Tiefschlaf
static bool active = false;

// Verlauf: alle 2 Minuten ein Wert, 6 Stunden. Mit Uhrzeit (Unix-Zeit), damit er Neustarts und Tiefschlaf
// übersteht; gesichert wird alle 10 Minuten und vor Tiefschlaf/Ausschalten (schont den Flash).
static const int HIST = 180;
static const uint32_t STEP_S = 120;
struct Sample { uint32_t t; uint8_t pct; uint8_t chg; uint16_t mv; };
static Sample* hist = nullptr;
static int hN = 0, hHead = 0, unsaved = 0;

void save();

// Automatischer Leichtschlaf (esp_pm): Ohne Sperren schläft der Prozessor zwischen allen Aufgaben (80 MHz, das WLAN
// bleibt über den Modem-Schlaf verbunden). Bei hellem Display, beim Streamen und am Kabel halten zwei Sperren ihn wach
// und auf 240 MHz.
static esp_pm_lock_handle_t lockAwake = nullptr, lockFast = nullptr;
static volatile bool doze = false;
static bool streamingNow = true, sleepOnUsb = false;
bool dozing() { return doze; }
void lightSleepOnUsb(bool on) { sleepOnUsb = on; }

static void pmBegin() {
  esp_pm_config_t c = {.max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = true};
  if (esp_pm_configure(&c) != ESP_OK) { Serial.println("Akku: Leichtschlaf nicht verfügbar"); return; }
  esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "awake", &lockAwake);
  esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "fast", &lockFast);
  esp_pm_lock_acquire(lockAwake);
  esp_pm_lock_acquire(lockFast);
}

static void pmUpdate() {
  bool d = !hwScreenOn() && !streamingNow && (!hwUsb() || sleepOnUsb);
  if (d == doze || !lockAwake) return;
  doze = d;
  // Keine 80-MHz-Grenze beim Dösen: gemessen kaum sparsamer (der Prozessor rechnet länger und schläft weniger),
  // aber HTTPS-Anfragen dauerten 1,5–3 s statt 0,4 s.
  if (d) { esp_pm_lock_release(lockFast); esp_pm_lock_release(lockAwake); }
  else { esp_pm_lock_acquire(lockAwake); esp_pm_lock_acquire(lockFast); }
}

void begin() {
  pmBegin();
  Preferences p;
  p.begin("power", true);
  m = (Mode)p.getUChar("mode", M_AUTO);
  deepMin = p.getUChar("deep", DEEP_SLEEP_MIN);
  nightOn = p.getBool("nkids", true);
  hist = (Sample*)heap_caps_calloc(HIST, sizeof(Sample), MALLOC_CAP_SPIRAM);
  if (hist && p.getBytesLength("hist") == HIST * sizeof(Sample)) {
    p.getBytes("hist", hist, HIST * sizeof(Sample));
    hN = min<int>(p.getUShort("hN", 0), HIST);
    hHead = p.getUShort("hHead", 0) % HIST;
  }
  p.end();
  esp_register_shutdown_handler([]() { save(); });  // auch vor jedem Neustart (Update, Einstellungen)
}

void save() {
  if (!hist || !unsaved) return;
  Preferences p;
  p.begin("power", false);
  p.putBytes("hist", hist, HIST * sizeof(Sample));
  p.putUShort("hN", hN);
  p.putUShort("hHead", hHead);
  p.end();
  unsaved = 0;
}

Mode mode() { return m; }
void setMode(Mode n) {
  m = n;
  Preferences p;
  p.begin("power", false);
  p.putUChar("mode", m);
  p.end();
}
bool saving() { return active; }

uint16_t screenOffS() {
  if (!active) return cfg.screenOff;
  return cfg.screenOff && cfg.screenOff < 10 ? cfg.screenOff : 10;
}
uint16_t deepSleepMin() { return !deepMin ? 0 : active ? min<uint16_t>(deepMin, 3) : deepMin; }
uint8_t deepSleepSetting() { return deepMin; }

bool nightRestOn() { return nightOn; }

void setNightRest(bool on) {
  nightOn = on;
  Preferences p;
  p.begin("power", false);
  p.putBool("nkids", nightOn);
  p.end();
}

bool nightRest(uint32_t* leftS) {
  if (!timeValid()) return false;
  if (!nightOn) return false;
  int from = (kids::cfg().bed + kids::cfg().storyMin) % (24 * 60), to = kids::cfg().wake;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int nowS = (t.tm_hour * 60 + t.tm_min) * 60 + t.tm_sec, fromS = from * 60, toS = to * 60;
  bool in = fromS <= toS ? nowS >= fromS && nowS < toS : nowS >= fromS || nowS < toS;
  if (!in) return false;
  if (leftS) {
    int d = toS - nowS;
    if (d <= 0) d += 86400;
    *leftS = d;
  }
  return true;
}
void setDeepSleep(uint8_t min) {
  deepMin = min;
  Preferences p;
  p.begin("power", false);
  p.putUChar("deep", deepMin);
  p.end();
}

static const Sample& at(int back) { return hist[(hHead - 1 - back + HIST) % HIST]; }

float drainPerHour() {
  // jüngster zusammenhängender Abschnitt ohne Laden, mindestens 20 Minuten; eine Lücke über 3 Stunden
  // (Uhr war aus) beendet ihn – Tiefschlaf zählt mit, er gehört zum normalen Tag
  if (!hist || hN < 2 || at(0).chg) return 0;
  int k = 0;
  while (k + 1 < hN && !at(k + 1).chg && at(k).t - at(k + 1).t < 3 * 3600) k++;
  uint32_t span = at(0).t - at(k).t;
  if (span < 20 * 60) return 0;
  float d = at(k).pct - at(0).pct;
  return d > 0 ? d * 3600.0f / span : 0;
}

int remainingMin() {
  int pct = hwBattery();
  float d = drainPerHour();
  if (pct < 0 || hwUsb() || d <= 0) return -1;
  return (int)(pct / d * 60);
}

static const char* sleepWhy = "";
static long sleepLeft = -1;
void sleepInfo(const char* why, long leftS) { sleepWhy = why ? why : ""; sleepLeft = leftS; }
String sleepText() {
  uint32_t left;
  if (nightRest(&left)) return "Nachtruhe – noch " + String(left / 3600) + " h " + String(left / 60 % 60) + " min";
  if (!strcmp(sleepWhy, "nie")) return doze ? "aus – döst, erreichbar" : "aus – immer erreichbar";
  if (sleepWhy[0]) return String("wach: ") + sleepWhy;
  return "Tiefschlaf in " + String(max(1L, (sleepLeft + 59) / 60)) + " min";
}

String remainingText() {
  if (hwBattery() < 0 || hwUsb()) return "";
  int m = remainingMin();
  if (m < 0) return "Restzeit wird gemessen";
  if (m < 60) return "noch ca. " + String(max(m, 1)) + " min";
  m = (m + 5) / 10 * 10;  // auf 10 Minuten – genauer ist die Schätzung nicht
  return "noch ca. " + String(m / 60) + " h" + (m % 60 ? " " + String(m % 60) + " min" : "");
}

String historyJson() {
  uint32_t now = time(nullptr);
  String s = "[";
  for (int i = hN - 1; i >= 0; i--) {
    const Sample& x = at(i);
    s += "[" + String(x.t < now ? (now - x.t) / 60 : 0) + "," + x.pct + "," + x.mv + "," + x.chg + "]";
    if (i) s += ",";
  }
  return s + "]";
}

void poll() {
  pmUpdate();  // jede Runde: nach einem Doppeltipp sofort wieder volle Geschwindigkeit
  static uint32_t at1s = 0;
  uint32_t now = millis();
  if (now - at1s < 1000) return;
  at1s = now;
  int pct = hwBattery();
  bool usb = hwUsb();

  // Verlauf (erst, wenn die Uhrzeit stimmt)
  uint32_t t = time(nullptr);
  if (hist && pct >= 0 && timeValid() && hN && t < at(0).t) {
    // Uhr nach dem Tiefschlaf zurückgestellt (die interne Uhr läuft dort etwas vor): letzten Wert ersetzen
    hist[(hHead - 1 + HIST) % HIST] = {t, (uint8_t)pct, (uint8_t)(hwCharging() || usb), (uint16_t)hwBatteryMv()};
  } else if (hist && pct >= 0 && timeValid() && (!hN || t - at(0).t >= STEP_S)) {
    hist[hHead] = {t, (uint8_t)pct, (uint8_t)(hwCharging() || usb), (uint16_t)hwBatteryMv()};
    hHead = (hHead + 1) % HIST;
    if (hN < HIST) hN++;
    if (++unsaved >= 5) save();
  }

  // Sparmodus an/aus (Auto: ab 20 %, aus über 25 % oder am Kabel)
  bool want = m == M_ON || (m == M_AUTO && pct >= 0 && !usb && (pct <= 20 || (active && pct <= 25)));
  if (want != active) {
    active = want;
    Serial.printf("Akku: Sparmodus %s\n", active ? "an" : "aus");
    if (active && m == M_AUTO) ui::toast("Akku 20 % – Sparmodus an", 3000);
  }

  // Warnungen und Notabschaltung
  static int warned = 100;
  if (pct >= 0 && !usb) {
    if (pct <= 10 && warned > 10) { warned = 10; ui::toast("Akku 10 % – bitte bald laden", 4000); audio::play(audio::FX_ERROR); }
    if (pct <= 3) {
      static uint32_t lowSince = 0;
      if (!lowSince) lowSince = now;
      if (now - lowSince > 20000) {  // 20 s lang wirklich leer: geordnet ausschalten
        Serial.println("Akku: leer, schalte aus");
        settingsSave();
        save();
        audio::shutdown();
        hwPowerOff();
      }
    }
  } else if (usb) {
    warned = 100;
  }

  // Helligkeit: Obergrenze nachts und im Sparmodus
  hwBrightnessCap(kids::night() ? 20 : active ? 40 : 100);

  // Strom sparen, sobald nichts gestreamt wird: das WLAN-Funkmodul hört dann nur zu den Beacons des Routers hin
  // (Modem-Schlaf, jeder Beacon), bei dunklem Display schläft der Prozessor zwischen den Aufgaben (pmUpdate).
  // Bewusst nicht der tiefe Modem-Schlaf (nur jeder 3. Beacon): mit Repeatern riss die Verbindung dabei öfter ab.
  bool streaming = music::state().playing;
  int psWant = !netOnline() || streaming ? 0 : 1;
  static int ps = -1;
  if (psWant != ps) {
    ps = psWant;
    WiFi.setSleep(ps ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
  }
  streamingNow = streaming;
}

}  // namespace pwr
