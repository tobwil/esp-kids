#include "net.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include "config.h"
#include "hw.h"
#include "power.h"
#include "settings.h"

// Zuletzt benutztes Netz mit Kanal und Router-Adresse, übersteht den Tiefschlaf: schnelles Neuverbinden
RTC_DATA_ATTR static char rtcSsid[33];
RTC_DATA_ATTR static uint8_t rtcBssid[6];
RTC_DATA_ATTR static int32_t rtcChannel = 0;

enum WifiState : uint8_t { W_IDLE, W_TRYING, W_SCANNING, W_USERSCAN, W_ROAMING };
static WifiState wst = W_IDLE;
static uint32_t wAt = 0;
static bool wasOnline = false, manual = false, ntpStarted = false, mdnsStarted = false;
// Ohne WLAN: in wachsenden Abständen nach bekannten Netzen suchen – 10 s, 20 s, 40 s … bis 1 Minute bei
// eingeschaltetem Display, bis 5 Minuten bei dunklem (Akku).
// Display an → gleich wieder suchen.
static const uint32_t RETRY_MIN = 10000, RETRY_SCREEN = 60000, RETRY_DARK = 300000;
static uint32_t retryEvery = RETRY_MIN;
static uint8_t failedTries = 0;
static bool lastScreen = true;
// Repeater/Mesh: bei schwachem Empfang nach einem besseren Zugangspunkt desselben Netzes suchen
static const int ROAM_WEAK = -72, ROAM_BETTER = 8;       // dBm: ab wann schwach, um wie viel besser der neue sein muss
static const uint32_t ROAM_AFTER = 20000, ROAM_EVERY = 90000, ROAM_NOTHING = 300000;
static uint32_t weakSince = 0, roamAt = 0, roamPause = ROAM_EVERY;  // nichts Besseres gefunden: seltener suchen
// Abbrüche (für die Weboberfläche)
static uint16_t drops = 0;
static uint8_t lastReason = 0;
static uint32_t lastDropAt = 0;


const char* netHost() {
  static char host[24];
  if (!host[0]) {
    uint8_t m[6];
    WiFi.macAddress(m);
    snprintf(host, sizeof(host), "pixeluhr-%02x%02x", m[4], m[5]);
  }
  return host;
}

bool netOnline() { return WiFi.status() == WL_CONNECTED; }
const char* netSsid() { return rtcSsid; }
String netIp() { return WiFi.localIP().toString(); }
int netRssi() { return WiFi.RSSI(); }
void netManual(bool on) { manual = on; }

bool timeValid() { return time(nullptr) > 1700000000; }

// Ohne bssid sucht der WLAN-Treiber alle Kanäle ab und nimmt den stärksten Zugangspunkt (netBegin)
void netConnect(const char* ssid, const char* pass, const uint8_t* bssid, int32_t channel) {
  WiFi.disconnect();
  delay(20);
  WiFi.begin(ssid, strlen(pass) ? pass : nullptr, channel, bssid);
  wst = W_TRYING;
  wAt = millis();
}

static String mac(const uint8_t* b) {
  char t[18];
  snprintf(t, sizeof(t), "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
  return t;
}

String netAp() { return netOnline() ? mac(WiFi.BSSID()) + " · Kanal " + String(WiFi.channel()) : ""; }

// Gründe nach IEEE 802.11 bzw. ESP-IDF (die häufigsten)
static const char* reasonText(uint8_t r) {
  switch (r) {
    case WIFI_REASON_BEACON_TIMEOUT: return "Signal weg";
    case WIFI_REASON_NO_AP_FOUND: return "Netz nicht gefunden";
    case WIFI_REASON_AUTH_EXPIRE: case WIFI_REASON_AUTH_FAIL: case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "Anmeldung fehlgeschlagen";
    case WIFI_REASON_ASSOC_LEAVE: return "selbst getrennt";
    case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY: return "vom Router getrennt (inaktiv)";
    case WIFI_REASON_AUTH_LEAVE: return "vom Router getrennt";
    case WIFI_REASON_CONNECTION_FAIL: return "Verbindung fehlgeschlagen";
    default: return "anderer Grund";
  }
}

void netRoamNow() {  // Test über USB: gleich nach einem besseren Zugangspunkt suchen
  if (!netOnline() || wst != W_IDLE) return;
  roamAt = millis();
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false, false, 120);
  wst = W_ROAMING;
  wAt = millis();
}

String netDrops() {
  if (!drops) return "keine";
  uint32_t ago = (millis() - lastDropAt) / 60000;
  return String(drops) + " · zuletzt vor " + String(ago) + " min: " + reasonText(lastReason) + " (" + String(lastReason) + ")";
}

static void startScan() {
  WiFi.disconnect();
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
  wst = W_SCANNING;
  wAt = millis();
}

void netScanStart() {
  if (WiFi.status() != WL_CONNECTED && wst == W_TRYING) WiFi.disconnect();
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
  wst = W_USERSCAN;
  wAt = millis();
}

int netScanResult(ScanNet* out, int max) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return -1;
  if (n < 0) return 0;
  int k = 0;
  for (int i = 0; i < n && k < max; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int j = 0; j < k; j++) if (s == out[j].ssid) { dup = true; break; }
    if (dup) continue;
    snprintf(out[k].ssid, sizeof(out[k].ssid), "%s", s.c_str());
    out[k].rssi = WiFi.RSSI(i);
    out[k].open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    k++;
  }
  // stärkste zuerst
  for (int i = 0; i < k; i++)
    for (int j = i + 1; j < k; j++)
      if (out[j].rssi > out[i].rssi) { ScanNet t = out[i]; out[i] = out[j]; out[j] = t; }
  WiFi.scanDelete();
  wst = W_IDLE;
  wAt = millis();
  return k;
}

void netPoll() {
  uint32_t now = millis();
  if (netOnline()) {
    if (!wasOnline) {
      wasOnline = true;
      wst = W_IDLE;
      snprintf(rtcSsid, sizeof(rtcSsid), "%s", WiFi.SSID().c_str());
      memcpy(rtcBssid, WiFi.BSSID(), 6);
      rtcChannel = WiFi.channel();
      retryEvery = RETRY_MIN;
      failedTries = 0;
      if (!ntpStarted) { configTzTime(TIMEZONE, "pool.ntp.org", "time.cloudflare.com"); ntpStarted = true; }
      if (!mdnsStarted) mdnsStarted = MDNS.begin(netHost());
      weakSince = 0;
      Serial.printf("WLAN: verbunden mit %s über %s, %d dBm, IP %s\n", rtcSsid, netAp().c_str(), WiFi.RSSI(), netIp().c_str());
    }
    // Roaming: seit 20 s schwacher Empfang → im Hintergrund nach einem besseren Zugangspunkt desselben Netzes suchen
    if (wst == W_ROAMING) {
      int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING && now - wAt < 15000) return;
      int best = -1, bestRssi = WiFi.RSSI() + ROAM_BETTER - 1;
      for (int i = 0; i < n; i++)
        if (WiFi.SSID(i) == rtcSsid && memcmp(WiFi.BSSID(i), WiFi.BSSID(), 6) && WiFi.RSSI(i) > bestRssi) { best = i; bestRssi = WiFi.RSSI(i); }
      if (best >= 0) {
        uint8_t b[6];
        memcpy(b, WiFi.BSSID(best), 6);
        int32_t ch = WiFi.channel(best);
        Serial.printf("WLAN: wechsle von %d dBm zu %s (%d dBm)\n", WiFi.RSSI(), mac(b).c_str(), bestRssi);
        WiFi.scanDelete();
        const char* pw = knownPass(rtcSsid);
        netConnect(rtcSsid, pw ? pw : "", b, ch);
        roamPause = ROAM_EVERY;
        return;
      }
      WiFi.scanDelete();
      wst = W_IDLE;
      roamPause = ROAM_NOTHING;
      return;
    }
    int rssi = WiFi.RSSI();
    if (rssi > ROAM_WEAK || rssi == 0) weakSince = 0;
    else if (!weakSince) { weakSince = now; roamPause = ROAM_EVERY; }
    // nur wenn die Uhr ohnehin wach ist (Display an oder es läuft etwas) – eine Suche kostet Strom
    if (weakSince && now - weakSince > ROAM_AFTER && now - roamAt > roamPause && wst == W_IDLE && !manual && !pwr::dozing()) {
      roamAt = now;
      Serial.printf("WLAN: schwacher Empfang (%d dBm), suche besseren Zugangspunkt\n", rssi);
      WiFi.scanDelete();
      WiFi.scanNetworks(true, false, false, 120);
      wst = W_ROAMING;
      wAt = now;
    }
    return;
  }
  if (wst == W_ROAMING) { WiFi.scanDelete(); wst = W_IDLE; }
  if (wasOnline) {
    wasOnline = false;
    Serial.println("WLAN: Verbindung verloren");
    const char* pw = knownPass(rtcSsid);
    if (pw) { netConnect(rtcSsid, pw); return; }
  }
  if (wst == W_USERSCAN) {
    if (now - wAt > 20000) { WiFi.scanDelete(); wst = W_IDLE; }
    return;
  }
  if (manual || !knownCount) return;
  bool screen = hwScreenOn();
  if (screen && !lastScreen && wst == W_IDLE) { retryEvery = RETRY_MIN; wAt = now - RETRY_MIN + 1500; }  // Display an: bald suchen
  lastScreen = screen;
  if (wst == W_TRYING && now - wAt > 12000) {
    WiFi.disconnect();
    if (++failedTries >= 2) {  // Netz da, aber Verbinden klappt nicht (Router startet neu, Passwort falsch): Pause
      Serial.printf("WLAN: kein Erfolg, nächster Versuch in %u s\n", (unsigned)(retryEvery / 1000));
      wst = W_IDLE;
      wAt = now;
      failedTries = 0;
    } else {
      Serial.println("WLAN: kein Erfolg, suche bekannte Netze");
      startScan();
    }
  } else if (wst == W_IDLE && now - wAt > retryEvery) {
    retryEvery = min(retryEvery * 2, screen ? RETRY_SCREEN : RETRY_DARK);
    startScan();
  } else if (wst == W_SCANNING) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING && now - wAt < 15000) return;
    int best = -1, bestRssi = -1000;
    for (int i = 0; i < n; i++)
      if (knownIndex(WiFi.SSID(i).c_str()) >= 0 && WiFi.RSSI(i) > bestRssi) { best = i; bestRssi = WiFi.RSSI(i); }
    if (best >= 0) {  // genau diesen Zugangspunkt nehmen (bei Repeatern gibt es mehrere mit demselben Namen)
      String ss = WiFi.SSID(best);
      uint8_t b[6];
      memcpy(b, WiFi.BSSID(best), 6);
      int32_t ch = WiFi.channel(best);
      Serial.printf("WLAN: versuche %s über %s (%d dBm)\n", ss.c_str(), mac(b).c_str(), bestRssi);
      WiFi.scanDelete();
      netConnect(ss.c_str(), knownPass(ss.c_str()), b, ch);
    } else {
      WiFi.scanDelete();
      wst = W_IDLE;
      wAt = now;
    }
  }
}

void netBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);  // Modem-Schlaf schaltet power.cpp, sobald nichts gestreamt wird
  // Repeater/Mesh: alle Kanäle absuchen und den stärksten Zugangspunkt nehmen (sonst den erstbesten)
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    drops++;
    lastReason = info.wifi_sta_disconnected.reason;
    lastDropAt = millis();
    Serial.printf("WLAN: getrennt, Grund %u (%s)\n", lastReason, reasonText(lastReason));
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  setenv("TZ", TIMEZONE, 1);
  tzset();
  if (!knownCount) return;
  const char* pw = rtcChannel ? knownPass(rtcSsid) : nullptr;
  if (pw) {
    WiFi.begin(rtcSsid, strlen(pw) ? pw : nullptr, rtcChannel, rtcBssid);
    Serial.printf("WLAN: schnell zurück zu %s (Kanal %d)\n", rtcSsid, (int)rtcChannel);
  } else {
    WiFi.begin(known[0].ssid, strlen(known[0].pass) ? known[0].pass : nullptr);
    Serial.printf("WLAN: versuche %s\n", known[0].ssid);
  }
  wst = W_TRYING;
  wAt = millis();
}
