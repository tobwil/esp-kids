#include "net.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include "config.h"
#include "hw.h"
#include "settings.h"

// Zuletzt benutztes Netz mit Kanal und Router-Adresse, übersteht den Tiefschlaf: schnelles Neuverbinden
RTC_DATA_ATTR static char rtcSsid[33];
RTC_DATA_ATTR static uint8_t rtcBssid[6];
RTC_DATA_ATTR static int32_t rtcChannel = 0;

enum WifiState : uint8_t { W_IDLE, W_TRYING, W_SCANNING, W_USERSCAN };
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

void netConnect(const char* ssid, const char* pass) {
  WiFi.disconnect();
  delay(20);
  WiFi.begin(ssid, strlen(pass) ? pass : nullptr);
  wst = W_TRYING;
  wAt = millis();
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
      Serial.printf("WLAN: verbunden mit %s, IP %s, Kanal %d\n", rtcSsid, netIp().c_str(), (int)rtcChannel);
    }
    return;
  }
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
    if (best >= 0) {
      String ss = WiFi.SSID(best);
      Serial.printf("WLAN: versuche %s (%d dBm)\n", ss.c_str(), bestRssi);
      WiFi.scanDelete();
      netConnect(ss.c_str(), knownPass(ss.c_str()));
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
