#include "hw.h"
#include <Arduino_GFX_Library.h>
#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>
#include <functional>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include "config.h"
#include "pins.h"

// „geteilt“: der Bus wird nur zum Zeichnen belegt – sonst hält er eine Sperre, die jeden Leichtschlaf verhindert
static Arduino_DataBus* bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3, true);
static Arduino_CO5300* gfx = new Arduino_CO5300(bus, LCD_RESET, 0, PANEL_W, PANEL_H, LCD_COL_OFS, 0, 0, 0);
static TouchDrvCST92xx touch;
static bool touchOk = false;
static lv_display_t* disp;
static lv_indev_t* indev;

static bool screenOn = true;
static uint8_t brightPct = 80;
static uint32_t lastActivity = 0;
static volatile bool wakeReq = false;

// ------------------------------------------------------------------ AXP2101

static bool pmuOk = false;

static int pmuRead(uint8_t reg) {
  Wire.beginTransmission(0x34);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((uint8_t)0x34, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

static void pmuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(0x34);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static void pmuBegin() {
  int r = pmuRead(0x18);
  pmuOk = r >= 0;
  if (!pmuOk) { Serial.println("AXP2101: nicht gefunden"); return; }
  if (!(r & 0x08)) pmuWrite(0x18, r | 0x08);  // Akkustands-Messung an
  int a = pmuRead(0x30);
  if (a >= 0 && (a & 0x11) != 0x11) pmuWrite(0x30, a | 0x11);  // ADC: Akkuspannung (Bit 0) und Chiptemperatur (Bit 4) an
  int e = pmuRead(0x41);
  pmuWrite(0x41, (e < 0 ? 0 : e) | 0x0C);     // IRQ-Status für PWR kurz/lang freigeben
  pmuWrite(0x49, 0xFF);                       // alte Meldungen löschen
  // PWR lang = 1,5 s (IRQ), Ausschalten erst nach 6 s Halten
  int k = pmuRead(0x27);
  if (k >= 0) pmuWrite(0x27, (k & 0xF0) | 0x01 | (0x01 << 2));
  Serial.println("AXP2101: ok");
}

static int pmuPct = -1, pmuMv = 0;
static float pmuTemp = NAN, cpuTemp = NAN;
static bool pmuUsb = false, pmuChg = false;
static uint32_t pmuAt = 0;

static void pmuUpdate() {
  if (!pmuOk || (pmuAt && millis() - pmuAt < 2000)) return;
  pmuAt = millis();
  int s0 = pmuRead(0x00), s1 = pmuRead(0x01), p = pmuRead(0xA4);
  pmuUsb = s0 >= 0 && (s0 & 0x20);
  pmuChg = s1 >= 0 && ((s1 >> 5) & 3) == 1;
  int pct = s0 >= 0 && (s0 & 0x08) && p >= 0 && p <= 100 ? p : -1;
  int h = pmuRead(0x34), l = pmuRead(0x35);
  if (h >= 0 && l >= 0) pmuMv = ((h & 0x3F) << 8) | l;
  // Temperatur des Ladechips (sitzt beim Akku): 14-Bit-Wert, 22 °C bei 7274, 20 Schritte je Grad (Datenblatt AXP2101)
  int th = pmuRead(0x3C), tl = pmuRead(0x3D);
  if (th >= 0 && tl >= 0) {
    int raw = ((th & 0x3F) << 8) | tl;
    if (raw) pmuTemp = 22.0f + (7274 - raw) / 20.0f;
  }
  static uint32_t cpuAt = 0;
  if (!cpuAt || millis() - cpuAt > 10000) {  // Prozessor (eingebauter Fühler), seltener
    cpuAt = millis();
    cpuTemp = temperatureRead();
  }
  // Beim Laden meldet der AXP2101 den Akku manchmal kurz als fehlend: letzten Wert 30 s halten
  static uint32_t validAt = 0;
  if (pct >= 0) { pmuPct = pct; validAt = millis(); }
  else if (millis() - validAt > 30000) pmuPct = -1;
}

// Werte werden nur in hwLoop() (Hauptaufgabe) gelesen, damit sich keine I2C-Zugriffe überschneiden
int hwBattery() { return pmuPct; }
int hwBatteryMv() { return pmuMv; }
bool hwUsb() { return pmuUsb; }
bool hwCharging() { return pmuChg; }
float hwTempBattery() { return pmuTemp; }
float hwTempCpu() { return cpuTemp; }

void hwPowerOff() {
  gfx->displayOff();
  int r = pmuRead(0x10);
  pmuWrite(0x10, (r < 0 ? 0 : r) | 0x01);
}

// 0 = nichts, 1 = kurz, 2 = lang
static int pwrKey() {
  static uint32_t at = 0;
  if (!pmuOk || millis() - at < 80) return 0;
  at = millis();
  int st = pmuRead(0x49);
  if (st <= 0) return 0;
  pmuWrite(0x49, st & 0x0F);
  if (st & 0x08) return 1;
  if (st & 0x04) return 2;
  return 0;
}

// ------------------------------------------------------------------ Tasten

static BtnEvent queue[8];
static uint8_t qHead = 0, qTail = 0;

static void push(BtnEvent e) {
  if ((uint8_t)(qHead - qTail) < sizeof(queue)) queue[qHead++ % sizeof(queue)] = e;
}

BtnEvent hwButton() {
  if (qTail == qHead) return BTN_NONE;
  return queue[qTail++ % sizeof(queue)];
}

static void pollBoot() {
  static bool down = false;
  static uint32_t at = 0;
  bool pressed = digitalRead(BTN_BOOT) == LOW;
  uint32_t now = millis();
  if (pressed && !down) { down = true; at = now; }
  else if (!pressed && down) {
    down = false;
    if (now - at < 25) return;  // Prellen
    push(BOOT_SHORT);
  }
}

// ------------------------------------------------------------------ Display

static uint8_t brightCap = 100;
static bool brightRaw = false;

void hwBrightness(uint8_t pct, bool ignoreCap) {
  brightPct = pct;
  brightRaw = ignoreCap;
  uint8_t p = ignoreCap ? pct : min(pct, brightCap);
  if (screenOn) gfx->setBrightness(max(8, p * 255 / 100));
}

void hwBrightnessCap(uint8_t pct) {
  if (pct == brightCap) return;
  brightCap = pct;
  hwBrightness(brightPct, brightRaw);
}

void hwScreen(bool on) {
  if (on == screenOn) return;
  screenOn = on;
  // dunkel: LVGL zeichnet nichts mehr (Zeiger, Animationen) – spart Rechenzeit; Berührungen werden weiter gelesen
  lv_display_enable_invalidation(disp, on);
  if (on) {
    gfx->displayOn();
    lv_obj_invalidate(lv_screen_active());
    hwBrightness(brightPct, brightRaw);
    hwActivity();
  } else {
    gfx->setBrightness(0);
    gfx->displayOff();
  }
}

bool hwScreenOn() { return screenOn; }
void hwActivity() { lastActivity = millis(); }
uint32_t hwIdleMs() { return millis() - lastActivity; }
void hwTouchPoll() { if (indev) lv_indev_read(indev); }

static volatile uint32_t touchSeenAt = 0;
bool hwTouchRecent() { return touchSeenAt && millis() - touchSeenAt < 800; }

// ------------------------------------------------------------------ Dösen: auf Ereignisse warten statt abfragen
// Touch-INT (GPIO 11, low bei Berührung) und BOOT (GPIO 0, low gedrückt) wecken den Prozessor aus dem Leichtschlaf
// (GPIO-Weckquelle) und die Hauptschleife (Task-Benachrichtigung aus der Unterbrechung). Der Pegel-Interrupt wird
// in der Unterbrechung sofort abgeschaltet und erst vor dem nächsten Warten wieder an – sonst feuert er, solange
// der Finger liegt. Gemessen: vorher ≈ 60 Aufwach-Vorgänge je Sekunde beim Dösen (Touch alle 20 ms).
static TaskHandle_t dozeTask = nullptr;
static bool dozeIrq = false;

static void IRAM_ATTR dozeIsr(void* pin) {
  gpio_intr_disable((gpio_num_t)(intptr_t)pin);
  BaseType_t woke = pdFALSE;
  if (dozeTask) vTaskNotifyGiveFromISR(dozeTask, &woke);
  if (woke) portYIELD_FROM_ISR();
}

void hwDozeKick() {
  if (dozeTask) xTaskNotifyGive(dozeTask);
}

void hwDozeWait(uint32_t ms) {
  static const gpio_num_t PINS[2] = {(gpio_num_t)TP_INT, (gpio_num_t)BTN_BOOT};
  if (!dozeIrq) {
    dozeIrq = true;
    dozeTask = xTaskGetCurrentTaskHandle();
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { dozeTask = nullptr; Serial.println("Dösen: kein GPIO-Interrupt"); }
    for (gpio_num_t p : PINS) {
      gpio_set_intr_type(p, GPIO_INTR_LOW_LEVEL);
      gpio_isr_handler_add(p, dozeIsr, (void*)(intptr_t)p);
      gpio_intr_disable(p);
      gpio_wakeup_enable(p, GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
  }
  if (!dozeTask) { delay(20); return; }  // ohne Interrupt wie früher abfragen
  if (!gpio_get_level(PINS[0]) || !gpio_get_level(PINS[1])) { delay(20); return; }  // gerade berührt/gedrückt
  // Wartende Weckrufe NICHT verwerfen: kam gerade eine Web-Anfrage (hwDozeKick), geht es sofort weiter – sonst wartet
  // sie, und weitere Anfragen wecken nicht mehr (sie warten auf die erste). Nie länger als 1 s schlafen.
  for (gpio_num_t p : PINS) gpio_intr_enable(p);
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(std::min<uint32_t>(ms, 1000)));
  for (gpio_num_t p : PINS) gpio_intr_disable(p);
}
bool hwWakeRequested() { bool w = wakeReq; wakeReq = false; return w; }

static void flushCb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  if (screenOn) gfx->draw16bitRGBBitmap(a->x1 + PANEL_X, a->y1 + PANEL_Y, (uint16_t*)px, lv_area_get_width(a), lv_area_get_height(a));
  lv_display_flush_ready(d);
}

// Der CO5300 braucht Fenster mit gerader Startspalte/-zeile und gerader Breite/Höhe
static void roundCb(lv_event_t* e) {
  lv_area_t* a = (lv_area_t*)lv_event_get_param(e);
  a->x1 &= ~1;
  a->y1 &= ~1;
  a->x2 |= 1;
  a->y2 |= 1;
}

// ------------------------------------------------------------------ Touch (+ eingespielte Gesten über USB)

struct Fake { int16_t x, y; bool down; };
static Fake fake[48];
static uint8_t fHead = 0, fTail = 0;

static void fakePush(int x, int y, bool down) {
  if ((uint8_t)(fHead - fTail) < sizeof(fake) / sizeof(fake[0])) fake[fHead++ % 48] = {(int16_t)x, (int16_t)y, down};
}

static volatile uint32_t strokeId = 0;
static volatile int16_t strokeX0, strokeY0, strokeX1, strokeY1;
static volatile bool strokeDown = false;

static void trackStroke(const lv_indev_data_t* d) {  // für Wischgesten, die LVGL beim Blättern nicht meldet
  bool down = d->state == LV_INDEV_STATE_PRESSED;
  if (down && !strokeDown) { strokeId = strokeId + 1; strokeX0 = d->point.x; strokeY0 = d->point.y; }
  if (down) { strokeX1 = d->point.x; strokeY1 = d->point.y; }
  strokeDown = down;
}

void hwStroke(uint32_t& id, int& dx, int& dy, bool& released) {
  id = strokeId;
  dx = strokeX1 - strokeX0;
  dy = strokeY1 - strokeY0;
  released = !strokeDown;
}

static void touchReadRaw(lv_indev_t*, lv_indev_data_t* data);
static void touchRead(lv_indev_t* in, lv_indev_data_t* data) {
  touchReadRaw(in, data);
  trackStroke(data);
}

static uint32_t holdUntil = 0;  // „hold X Y MS“: so lange gedrückt halten
static int16_t holdX, holdY;

static void touchReadRaw(lv_indev_t*, lv_indev_data_t* data) {
  static int16_t lx = 0, ly = 0;
  static bool swallow = false;
  if (holdUntil) {
    bool down = (int32_t)(millis() - holdUntil) < 0;
    if (!down) holdUntil = 0;
    data->point.x = holdX;
    data->point.y = holdY;
    data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    hwActivity();
    return;
  }
  if (fTail != fHead) {
    Fake f = fake[fTail++ % 48];
    data->point.x = f.x;
    data->point.y = f.y;
    data->state = f.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (!screenOn && f.down) { wakeReq = true; data->state = LV_INDEV_STATE_RELEASED; }
    hwActivity();
    return;
  }
  int16_t x[2], y[2];
  int n = touchOk ? touch.getPoint(x, y, 2) : 0;
  bool pressed = n > 0;
  if (pressed) { lx = constrain(x[0] - PANEL_X, 0, LCD_W - 1); ly = constrain(y[0] - PANEL_Y, 0, LCD_H - 1); }
  if (pressed) touchSeenAt = millis();
  // Bei dunklem Display weckt nur ein Doppeltipp: zwei kurze Tipps kurz nacheinander an etwa derselben Stelle.
  // Stoff in der Hosentasche oder eine Hand am Gehäuse berühren lang, flächig oder wandernd – das zählt nicht,
  // und es hält die Uhr auch nicht vom Tiefschlaf ab. Die Berührung geht nicht an die Oberfläche.
  static bool was = false, spoiled = false;
  static uint32_t downAt = 0, tapAt = 0;
  static int16_t sx, sy, tx, ty;
  if (!screenOn) {
    uint32_t now = millis();
    if (pressed && !was) { downAt = now; sx = lx; sy = ly; spoiled = false; }
    if (pressed && (n > 1 || abs(lx - sx) + abs(ly - sy) > 40)) spoiled = true;
    if (!pressed && was) {
      bool tap = !spoiled && now - downAt < 250;
      if (tap && tapAt && now - tapAt < 500 && abs(sx - tx) + abs(sy - ty) < 80) { wakeReq = true; tapAt = 0; }
      else if (tap) { tapAt = now; tx = sx; ty = sy; }
      else tapAt = 0;
    }
    was = pressed;
    swallow = pressed;
    data->point.x = lx;
    data->point.y = ly;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  was = false;
  if (swallow && !pressed) swallow = false;
  data->point.x = lx;
  data->point.y = ly;
  data->state = pressed && !swallow ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (pressed) hwActivity();
}

// ------------------------------------------------------------------ Bildschirmfoto und Fernsteuerung

static void sendShot() {
  lv_draw_buf_t* snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
  if (!snap) { Serial.println("shot: kein Speicher"); return; }
  // Oberste Ebene (Hinweise, Klingeln) mit einzeichnen
  lv_obj_t* top = lv_layer_top();
  if (lv_obj_get_child_count(top)) {
    lv_draw_buf_t* t = lv_snapshot_take(top, LV_COLOR_FORMAT_ARGB8888);
    if (t) {
      for (int y = 0; y < LCD_H; y++) {
        uint16_t* d = (uint16_t*)(snap->data + y * snap->header.stride);
        const uint8_t* s = t->data + y * t->header.stride;
        for (int x = 0; x < LCD_W; x++) {
          uint8_t b = s[4 * x], g = s[4 * x + 1], r = s[4 * x + 2], a = s[4 * x + 3];
          if (!a) continue;
          uint16_t o = d[x];
          int orr = (o >> 11) << 3, og = ((o >> 5) & 63) << 2, ob = (o & 31) << 3;
          orr = (r * a + orr * (255 - a)) / 255;
          og = (g * a + og * (255 - a)) / 255;
          ob = (b * a + ob * (255 - a)) / 255;
          d[x] = ((orr >> 3) << 11) | ((og >> 2) << 5) | (ob >> 3);
        }
      }
      lv_draw_buf_destroy(t);
    }
  }
  Serial.printf("\nFB%d\n", LCD_W);
  for (int y = 0; y < LCD_H; y++) Serial.write(snap->data + y * snap->header.stride, LCD_W * 2);
  Serial.flush();
  lv_draw_buf_destroy(snap);
}

// Weitere Befehle (WLAN, Musik, Freigaben …) verarbeitet main.cpp über diesen Haken
extern bool appCommand(const char* line);

static void handleLine(char* line) {
  int x, y;
  char d;
  if (sscanf(line, "tap %d %d", &x, &y) == 2) {
    for (int i = 0; i < 3; i++) fakePush(x, y, true);
    fakePush(x, y, false);
  } else if (int ms; sscanf(line, "hold %d %d %d", &x, &y, &ms) == 3) {  // gedrückt halten, ms lang
    holdX = x;
    holdY = y;
    holdUntil = millis() + ms;
  } else if (sscanf(line, "hold %d %d", &x, &y) == 2) {
    for (int i = 0; i < 40; i++) fakePush(x, y, true);
    fakePush(x, y, false);
  } else if (sscanf(line, "swipe %c", &d) == 1) {
    int cx = LCD_W / 2, cy = LCD_H / 2, dx = 0, dy = 0;
    if (d == 'l') dx = -1; else if (d == 'r') dx = 1; else if (d == 'u') dy = -1; else dy = 1;
    for (int i = 0; i <= 8; i++) fakePush(cx - dx * 120 + dx * 30 * i, cy - dy * 120 + dy * 30 * i, true);
    fakePush(cx + dx * 120, cy + dy * 120, false);
  } else if (sscanf(line, "bezel %d", &x) == 1) {  // am Rand drehen: von 12 Uhr um x Grad (negativ = gegen den Uhrzeigersinn)
    int n = constrain(abs(x) / 6, 1, 44);
    for (int i = 0; i <= n; i++) {
      float a = (x * i / (float)n) * 0.0174533f;
      fakePush(LCD_W / 2 + sinf(a) * 205, LCD_H / 2 - cosf(a) * 205, true);
    }
    fakePush(LCD_W / 2 + sinf(x * 0.0174533f) * 205, LCD_H / 2 - cosf(x * 0.0174533f) * 205, false);
  } else if (!strcmp(line, "boot")) push(BOOT_SHORT);
  else if (!strcmp(line, "pwr")) push(PWR_SHORT);
  else if (!strcmp(line, "pwrlong")) push(PWR_LONG);
  else if (!strcmp(line, "shot")) sendShot();
  else if (!strcmp(line, "dump")) {  // alle Labels des aktuellen Bildschirms mit Lage und Sichtbarkeit
    std::function<void(lv_obj_t*, int)> walk = [&](lv_obj_t* o, int d) {
      if (lv_obj_check_type(o, &lv_label_class)) {
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        Serial.printf("%*s[%d,%d %dx%d]%s „%s“\n", d * 2, "", (int)a.x1, (int)a.y1, (int)lv_area_get_width(&a),
                      (int)lv_area_get_height(&a), lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) ? " versteckt" : "",
                      lv_label_get_text(o));
      }
      for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) walk(lv_obj_get_child(o, i), d + 1);
    };
    walk(lv_screen_active(), 0);
    walk(lv_layer_top(), 0);  // Klingeln, Hinweise
  }
  else if (!strcmp(line, "mem")) Serial.printf("Heap intern %u (min %u), PSRAM %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                                                heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
  else if (!appCommand(line)) Serial.printf("? %s\n", line);
}

static void pollSerial() {
  static char line[160];
  static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { line[n] = 0; if (n) handleLine(line); n = 0; }
    else if (n < (int)sizeof(line) - 1) line[n++] = c;
  }
}

// ------------------------------------------------------------------ Start, Schleife, Schlaf

static bool wokeFromSleep = false;
bool hwWokeFromSleep() { return wokeFromSleep; }

void hwBegin() {
  wokeFromSleep = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
  gpio_hold_dis((gpio_num_t)PA_EN);
  gpio_deep_sleep_hold_dis();
  pinMode(BTN_BOOT, INPUT_PULLUP);
  pinMode(PWR_SENSE, INPUT);
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  pmuBegin();
  pmuUpdate();

#ifdef EXTRA_RST
  pinMode(EXTRA_RST, OUTPUT);
  digitalWrite(EXTRA_RST, LOW);
#endif
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(20);
  digitalWrite(TP_RST, HIGH);
#ifdef EXTRA_RST
  digitalWrite(EXTRA_RST, HIGH);
#endif
  delay(50);
  touch.setPins(TP_RST, TP_INT);
  touchOk = touch.begin(Wire, CST92XX_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  if (touchOk) {
    touch.setMaxCoordinates(PANEL_W, PANEL_H);
    touch.setSwapXY(TOUCH_SWAP_XY);
    touch.setMirrorXY(TOUCH_MIRROR_X, TOUCH_MIRROR_Y);
  }
  Serial.printf("Touch: %s\n", touchOk ? "ok" : "nicht gefunden");

  if (!gfx->begin(80000000)) Serial.println("Display: Start fehlgeschlagen");
#ifdef LCD_MADCTL
  bus->beginWrite();
  bus->writeC8D8(0x36, LCD_MADCTL);
  bus->endWrite();
#endif
  gfx->fillScreen(0x0000);
  gfx->setBrightness(0);

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  disp = lv_display_create(LCD_W, LCD_H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  // Ganzer Bildschirm als Puffer im PSRAM: geänderte Bereiche werden in einem Durchgang gezeichnet.
  // (Mit schmalen Streifen im internen RAM liefen aufwendige Zifferblätter 10× pro Bild durch.)
  size_t sz = LCD_W * LCD_H * 2;
  void* b1 = heap_caps_aligned_alloc(64, sz, MALLOC_CAP_SPIRAM);
  void* b2 = heap_caps_aligned_alloc(64, sz, MALLOC_CAP_SPIRAM);
  lv_display_set_buffers(disp, b1, b2, sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_add_event_cb(disp, roundCb, LV_EVENT_INVALIDATE_AREA, nullptr);

  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchRead);
  lv_indev_set_scroll_limit(indev, 12);
  hwActivity();
}

void hwLoop() {
  pollBoot();
  pmuUpdate();
  int k = pwrKey();
  if (k == 1) push(PWR_SHORT);
  else if (k == 2) push(PWR_LONG);
  pollSerial();
}

void hwDeepSleep(uint32_t wakeInS) {
  gfx->setBrightness(0);
  gfx->displayOff();
  if (touchOk) {
    int16_t x, y;
    touch.getPoint(&x, &y, 1);  // anstehende Meldung abholen, damit INT wieder hoch ist
  }
  digitalWrite(PA_EN, LOW);
  gpio_hold_en((gpio_num_t)PA_EN);
  gpio_deep_sleep_hold_en();
  rtc_gpio_pullup_en((gpio_num_t)BTN_BOOT);
  rtc_gpio_pulldown_dis((gpio_num_t)BTN_BOOT);
  // Der automatische Leichtschlaf hinterlässt einen Timer als Weckquelle – der würde den Tiefschlaf sofort beenden
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  // nur die Taste weckt – Berührungen in der Tasche würden die Uhr sonst jedes Mal komplett hochfahren
  esp_sleep_enable_ext1_wakeup(1ULL << BTN_BOOT, ESP_EXT1_WAKEUP_ANY_LOW);
  if (wakeInS) esp_sleep_enable_timer_wakeup((uint64_t)wakeInS * 1000000ULL);
  Serial.println("Tiefschlaf …");
  Serial.flush();
  esp_deep_sleep_start();
}
