#include "weather.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "net.h"
#include "settings.h"

namespace weather {

static Now cur{};
static uint32_t fetchedAt = 0;
static SemaphoreHandle_t mtx;

static bool located() { return cfg.lat != 0 || cfg.lon != 0; }
// ohne Standort: Mitte Deutschlands
static float lat() { return located() ? cfg.lat : 51.0f; }
static float lon() { return located() ? cfg.lon : 10.4f; }

Now now() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  Now n = cur;
  xSemaphoreGive(mtx);
  n.ok = n.ok && millis() - fetchedAt < 3 * 3600 * 1000UL;
  return n;
}

const char* text(int c) {
  if (c == 0) return "klar";
  if (c == 1) return "heiter";
  if (c == 2) return "wolkig";
  if (c == 3) return "bedeckt";
  if (c == 45 || c == 48) return "Nebel";
  if (c >= 51 && c <= 57) return "Niesel";
  if (c >= 61 && c <= 67) return "Regen";
  if (c >= 71 && c <= 77) return "Schnee";
  if (c >= 80 && c <= 82) return "Schauer";
  if (c == 85 || c == 86) return "Schneeschauer";
  if (c >= 95) return "Gewitter";
  return "";
}

const char* city() { return cfg.city; }

static bool getJson(const String& url, JsonDocument& doc) {
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(url)) return false;
  int code = http.GET();
  bool ok = code == 200 && !deserializeJson(doc, http.getString());
  http.end();
  return ok;
}

static void locate() {
  JsonDocument doc;
  if (!getJson("http://ip-api.com/json/?fields=status,lat,lon,city", doc) || strcmp(doc["status"] | "", "success")) return;
  cfg.lat = doc["lat"] | 0.0f;
  cfg.lon = doc["lon"] | 0.0f;
  snprintf(cfg.city, sizeof(cfg.city), "%s", doc["city"] | "");
  settingsSave();
  Serial.printf("Standort: %s (%.2f, %.2f)\n", cfg.city, cfg.lat, cfg.lon);
}

static bool fetch() {
  char url[300];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&current=temperature_2m,weather_code,wind_speed_10m,is_day"
           "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=auto&forecast_days=1",
           lat(), lon());
  JsonDocument doc;
  if (!getJson(url, doc)) return false;
  Now n{};
  n.temp = doc["current"]["temperature_2m"] | 0.0f;
  n.code = doc["current"]["weather_code"] | 0;
  n.wind = doc["current"]["wind_speed_10m"] | 0.0f;
  n.day = (doc["current"]["is_day"] | 1) == 1;
  n.tmax = doc["daily"]["temperature_2m_max"][0] | n.temp;
  n.tmin = doc["daily"]["temperature_2m_min"][0] | n.temp;
  n.rain = doc["daily"]["precipitation_probability_max"][0] | 0;
  n.ok = true;
  xSemaphoreTake(mtx, portMAX_DELAY);
  cur = n;
  fetchedAt = millis();
  xSemaphoreGive(mtx);
  Serial.printf("Wetter: %.1f °C, %s, Regen %d %%\n", n.temp, text(n.code), n.rain);
  return true;
}

static void task(void*) {
  uint32_t last = 0;
  bool first = true;
  for (;;) {
    if (netOnline() && (first || millis() - last > 15 * 60 * 1000UL)) {
      if (!located()) locate();
      if (fetch()) { first = false; last = millis(); }
      else vTaskDelay(pdMS_TO_TICKS(30000));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "weather", 6144, nullptr, 2, nullptr, 1);
}

}  // namespace weather
