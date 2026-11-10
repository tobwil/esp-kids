#include "settings.h"
#include <Preferences.h>
#include <esp_mac.h>
#include "config.h"

Settings cfg;
KnownNet known[KNOWN_MAX];
int knownCount = 0;
static Preferences prefs;

int knownIndex(const char* ssid) {
  for (int i = 0; i < knownCount; i++) if (!strcmp(known[i].ssid, ssid)) return i;
  return -1;
}

const char* knownPass(const char* ssid) {
  int i = knownIndex(ssid);
  return i < 0 ? nullptr : known[i].pass;
}

// Netz merken bzw. nach vorn holen; ist die Liste voll, fällt das am längsten unbenutzte heraus
void knownAdd(const char* ssid, const char* pass) {
  int i = knownIndex(ssid);
  if (i < 0) i = knownCount < KNOWN_MAX ? knownCount++ : KNOWN_MAX - 1;
  for (int k = i; k > 0; k--) known[k] = known[k - 1];
  snprintf(known[0].ssid, sizeof(known[0].ssid), "%s", ssid);
  snprintf(known[0].pass, sizeof(known[0].pass), "%s", pass);
}

void knownForget(const char* ssid) {
  int i = knownIndex(ssid);
  if (i < 0) return;
  for (int k = i; k < knownCount - 1; k++) known[k] = known[k + 1];
  knownCount--;
}

void settingsLoad() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char defName[17];
  snprintf(defName, sizeof(defName), "Uhr %02X%02X", mac[4], mac[5]);

  prefs.begin("tf", true);
  cfg.volume = prefs.getUChar("vol", VOLUME_DEFAULT);
  cfg.brightness = prefs.getUChar("bright", BRIGHTNESS_DEFAULT);
  cfg.screenOff = prefs.getUShort("scroff", SCREEN_TIMEOUT_S);
  prefs.getString("name", cfg.name, sizeof(cfg.name));
  if (!cfg.name[0]) snprintf(cfg.name, sizeof(cfg.name), "%s", defName);
  cfg.lat = prefs.getFloat("lat", 0);
  cfg.lon = prefs.getFloat("lon", 0);
  prefs.getString("city", cfg.city, sizeof(cfg.city));
  knownCount = min((int)prefs.getUChar("netn", 0), KNOWN_MAX);
  char key[8];
  for (int i = 0; i < knownCount; i++) {
    snprintf(key, sizeof(key), "s%d", i);
    prefs.getString(key, known[i].ssid, sizeof(known[i].ssid));
    snprintf(key, sizeof(key), "p%d", i);
    prefs.getString(key, known[i].pass, sizeof(known[i].pass));
  }
  prefs.end();
  if (strlen(WIFI_SSID) && knownIndex(WIFI_SSID) < 0) knownAdd(WIFI_SSID, WIFI_PASSWORD);
}

void settingsSave() {
  prefs.begin("tf", false);
  prefs.putUChar("vol", cfg.volume);
  prefs.putUChar("bright", cfg.brightness);
  prefs.putUShort("scroff", cfg.screenOff);
  prefs.putString("name", cfg.name);
  prefs.putFloat("lat", cfg.lat);
  prefs.putFloat("lon", cfg.lon);
  prefs.putString("city", cfg.city);
  prefs.putUChar("netn", knownCount);
  char key[8];
  for (int i = 0; i < knownCount; i++) {
    snprintf(key, sizeof(key), "s%d", i);
    prefs.putString(key, known[i].ssid);
    snprintf(key, sizeof(key), "p%d", i);
    prefs.putString(key, known[i].pass);
  }
  prefs.end();
}
