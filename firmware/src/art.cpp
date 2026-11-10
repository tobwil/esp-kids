#include "art.h"
#include "power.h"
#include <jpeg_decoder.h>
#include <math.h>
#include "web.h"

namespace art {

static uint16_t* buf[2];
static volatile int front = -1;
static volatile uint32_t ver = 0;
static char want[200];
static volatile bool wanted = false;
static SemaphoreHandle_t mtx;

const uint16_t* image(uint32_t* version) {
  *version = ver;
  return front < 0 ? nullptr : buf[front];
}

void request(const char* url) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  snprintf(want, sizeof(want), "%s", url ? url : "");
  wanted = true;
  xSemaphoreGive(mtx);
}

// JPEG → RGB565 (im PSRAM), dann mittig quadratisch zuschneiden, bilinear auf SIZE skalieren, rund maskieren
static bool decode(const String& jpg, uint16_t* out) {
  esp_jpeg_image_cfg_t cfg = {};
  cfg.indata = (uint8_t*)jpg.c_str();
  cfg.indata_size = jpg.length();
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
  esp_jpeg_image_output_t info;
  if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK) return false;
  // so verkleinern, dass das Bild gerade noch ≥ SIZE bleibt (spart Speicher und Zeit)
  int scale = 0;
  while (scale < 3 && (info.width >> (scale + 1)) >= SIZE && (info.height >> (scale + 1)) >= SIZE) scale++;
  int w = info.width >> scale, h = info.height >> scale;
  size_t sz = (size_t)w * h * 2;
  uint16_t* raw = (uint16_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  if (!raw) return false;
  cfg.outbuf = (uint8_t*)raw;
  cfg.outbuf_size = sz;
  cfg.out_scale = (esp_jpeg_image_scale_t)scale;
  bool ok = esp_jpeg_decode(&cfg, &info) == ESP_OK;
  if (ok) {
    w = info.width;
    h = info.height;
    int side = min(w, h), ox = (w - side) / 2, oy = (h - side) / 2;
    float k = (float)side / SIZE, r = SIZE / 2.0f;
    for (int y = 0; y < SIZE; y++) {
      for (int x = 0; x < SIZE; x++) {
        float dx = x + 0.5f - r, dy = y + 0.5f - r;
        float d = sqrtf(dx * dx + dy * dy);
        uint16_t* o = &out[y * SIZE + x];
        if (d > r) { *o = 0; continue; }
        int sx = ox + (int)(x * k), sy = oy + (int)(y * k);
        uint16_t c = raw[sy * w + sx];
        if (d > r - 1) {  // Rand weich ausblenden
          float a = r - d;
          int rr = ((c >> 11) & 31) * a, gg = ((c >> 5) & 63) * a, bb = (c & 31) * a;
          c = (rr << 11) | (gg << 5) | bb;
        }
        *o = c;
      }
    }
  }
  free(raw);
  return ok;
}

static void task(void*) {
  for (;;) {
    if (!wanted) { vTaskDelay(pdMS_TO_TICKS(pwr::dozing() ? 1000 : 50)); continue; }  // dösend seltener nachsehen
    char url[200];
    xSemaphoreTake(mtx, portMAX_DELAY);
    snprintf(url, sizeof(url), "%s", want);
    wanted = false;
    xSemaphoreGive(mtx);
    if (!url[0]) { front = -1; ver = ver + 1; continue; }
    // Bilder (PNG, riesige Cover, Senderlogos) über den freien Bild-Proxy wsrv.nl als kleines JPEG holen
    String src = String("https://wsrv.nl/?w=300&h=300&fit=cover&output=jpg&url=") + web::urlencode(url);
    String jpg;
    int code = web::get(src, jpg, nullptr, 1024 * 1024);
    if (code != 200) code = web::get(url, jpg, nullptr, 1024 * 1024);  // Proxy nicht erreichbar
    if (code != 200 || wanted) continue;
    int back = front == 0 ? 1 : 0;
    if (decode(jpg, buf[back]) && !wanted) { front = back; ver = ver + 1; }
    else Serial.printf("Cover: nicht lesbar (%s)\n", url);
  }
}

void begin() {
  mtx = xSemaphoreCreateMutex();
  buf[0] = (uint16_t*)heap_caps_malloc(SIZE * SIZE * 2, MALLOC_CAP_SPIRAM);
  buf[1] = (uint16_t*)heap_caps_malloc(SIZE * SIZE * 2, MALLOC_CAP_SPIRAM);
  xTaskCreatePinnedToCore(task, "art", 8192, nullptr, 2, nullptr, 1);
}

}  // namespace art
