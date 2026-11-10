#include "audio.h"
#include "power.h"
#include <Wire.h>
#include <driver/i2s_std.h>
#include "config.h"
#include "pins.h"

namespace audio {

static const uint8_t ES8311 = 0x18, ES7210 = 0x40;
static const int BLOCK = 256;  // Abtastwerte pro Durchlauf (8 ms)
static bool codecOk = false;
static i2s_chan_handle_t tx = nullptr;

// ------------------------------------------------------------------ Ringpuffer (ein Schreiber, ein Leser)

struct Ring {
  int16_t* buf = nullptr;
  uint32_t cap = 0;
  volatile uint32_t head = 0, tail = 0;
  uint32_t used() const { return head - tail; }
  uint32_t space() const { return cap - used(); }
  void write(const int16_t* s, size_t n) {
    for (size_t i = 0; i < n && space(); i++) { buf[head % cap] = s[i]; head = head + 1; }
  }
  int16_t pop() { int16_t v = buf[tail % cap]; tail = tail + 1; return v; }
};

static Ring music;
static volatile bool musicPaused = false, musicFlushReq = false;
static volatile uint32_t played = 0;
static volatile float gainTarget = 0.86f;
static float gain = 0;  // nur im Mischer

size_t musicSpace() { return music.space(); }
size_t musicUsed() { return music.used(); }
static TaskHandle_t mixTask = nullptr;
static inline void kick() { if (mixTask) xTaskNotifyGive(mixTask); }  // Mischer aus der Pause holen

void musicWrite(const int16_t* s, size_t n) { music.write(s, n); kick(); }
void musicFlush() { musicFlushReq = true; while (musicFlushReq) vTaskDelay(1); }
void musicPause(bool p) { musicPaused = p; if (!p) kick(); }
uint32_t musicPlayed() { return played; }

// ------------------------------------------------------------------ Klänge (kleiner Synthesizer)

enum VType : uint8_t { V_OFF, V_SINE, V_TRI };
struct Voice {
  VType type;
  float freq, mult, phase, amp;
  uint32_t delay, pos, len, attack;
};
static Voice voices[12];
static QueueHandle_t fxQueue = nullptr;

static void tone(VType t, float f0, float f1, int ms, float amp, int delayMs = 0, int attackMs = 3) {
  Voice v{};
  v.type = t;
  v.len = max(1, ms * SR / 1000);
  v.freq = f0;
  v.mult = powf(f1 / f0, 1.0f / v.len);
  v.amp = amp;
  v.delay = delayMs * SR / 1000;
  v.attack = max(1, attackMs * SR / 1000);
  if (fxQueue) xQueueSend(fxQueue, &v, 0);
  kick();
}

void play(Fx fx) {
  if (!codecOk) return;
  switch (fx) {
    case FX_TAP: tone(V_SINE, 1400, 1700, 35, 0.22f); break;
    case FX_TICK: tone(V_SINE, 2093, 2093, 15, 0.15f, 0, 1); break;
    case FX_OK:
      tone(V_SINE, 880, 880, 90, 0.25f);
      tone(V_SINE, 1319, 1319, 160, 0.25f, 80);
      break;
    case FX_ERROR:
      tone(V_TRI, 330, 300, 140, 0.28f);
      tone(V_TRI, 262, 220, 220, 0.28f, 150);
      break;
    case FX_CHIME:
      tone(V_SINE, 1047, 1047, 220, 0.25f, 0, 5);
      tone(V_SINE, 1319, 1319, 220, 0.22f, 120, 5);
      tone(V_SINE, 1568, 1568, 400, 0.20f, 240, 5);
      break;
    case FX_ALARM:  // „Uhr finden“: vier schnelle Pieptöne
      for (int i = 0; i < 4; i++) {
        tone(V_TRI, 2093, 2093, 75, 0.45f, i * 120, 2);
        tone(V_SINE, 4186, 4186, 75, 0.10f, i * 120, 2);
      }
      break;
  }
}

static float synth() {
  float s = 0;
  for (auto& v : voices) {
    if (v.type == V_OFF) continue;
    if (v.delay) { v.delay--; continue; }
    float p = (float)v.pos / v.len;
    float env = v.pos < v.attack ? (float)v.pos / v.attack : (1 - p) * (1 - p);
    v.phase += v.freq / SR;
    if (v.phase >= 1) v.phase -= 1;
    float x = v.type == V_SINE ? sinf(v.phase * 6.2832f) : 4 * fabsf(v.phase - 0.5f) - 1;
    v.freq *= v.mult;
    s += x * env * v.amp;
    if (++v.pos >= v.len) v.type = V_OFF;
  }
  return s;
}

static bool fxActive() {
  for (auto& v : voices) if (v.type != V_OFF) return true;
  return false;
}

// ------------------------------------------------------------------ Mischer

static void setPa(bool on) {
  static bool state = false;
  if (on != state) { state = on; digitalWrite(PA_EN, on ? HIGH : LOW); }
}

static void task(void*) {
  static int16_t out[BLOCK * 2];
  uint32_t quietSince = millis(), idleSince = millis();
  bool paused = false;
  for (;;) {
    Voice nv;
    while (xQueueReceive(fxQueue, &nv, 0) == pdTRUE) {
      Voice* slot = nullptr;
      for (auto& c : voices) if (c.type == V_OFF) { slot = &c; break; }
      if (!slot) slot = &voices[0];
      *slot = nv;
    }
    if (musicFlushReq) { music.tail = music.head; played = 0; musicFlushReq = false; }

    // Nichts zu hören: nach 3 s I2S anhalten (der Codec bekommt keinen Takt mehr, der Prozessor hat Ruhe),
    // bis wieder etwas kommt – das dauert nur einen Durchlauf
    bool want = (!musicPaused && music.used()) || fxActive();
    if (want) idleSince = millis();
    if (!paused && !want && millis() - idleSince > 3000) {
      setPa(false);
      i2s_channel_disable(tx);
      paused = true;
    }
    if (paused) {
      // nichts zu tun: schlafen, bis etwas kommt (kick) – beim Dösen bis zu 1 s statt alle 10 ms nachzusehen
      if (!want) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(pwr::dozing() ? 1000 : 10)); continue; }
      i2s_channel_enable(tx);
      paused = false;
    }

    bool musicOn = !musicPaused && music.used() > 0;
    bool fxOn = fxActive();
    for (int n = 0; n < BLOCK; n++) {
      float s = 0;
      if (musicOn && music.used()) { s += music.pop() / 32768.0f * 0.85f; played = played + 1; }
      if (fxOn) s += synth();
      // Lautstärke gleitet zum Ziel (≈ 30 ms), dann weich begrenzen (tanh-Näherung) statt hart zu clippen
      gain += (gainTarget - gain) * 0.001f;
      s *= gain;
      if (s > 3) s = 1;
      else if (s < -3) s = -1;
      else s = s * (27 + s * s) / (27 + 9 * s * s);
      int16_t o = (int16_t)(s * 32000.0f);
      out[2 * n] = o;
      out[2 * n + 1] = o;
    }
    if (musicOn || fxOn) quietSince = millis();
    setPa(codecOk && millis() - quietSince < 1500);
    size_t done;
    i2s_channel_write(tx, out, sizeof(out), &done, portMAX_DELAY);
  }
}

// ------------------------------------------------------------------ Codecs

static void wr(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static uint8_t rd(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

static bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Lautstärke als Software-Verstärkung im Mischer: gehörrichtig (kubisch), 100 % = +12 dB mit weicher
// Begrenzung. Der Codec bleibt fest auf 0 dB, so gibt es keine Sprünge zwischen Registerstufen.
void setVolume(int pct) {
  pct = constrain(pct, 0, 100);
  float v = pct / 100.0f;
  gainTarget = 4.0f * v * v * v;
}

// ES8311 als Slave, MCLK = 256 × fs vom Pin, 16 Bit I2S (Registerfolge nach dem Espressif-Treiber)
static bool es8311Begin() {
  if (!present(ES8311)) { Serial.println("Audio: ES8311 fehlt"); return false; }
  wr(ES8311, 0x00, 0x1F);
  delay(20);
  wr(ES8311, 0x00, 0x00);
  wr(ES8311, 0x00, 0x80);
  wr(ES8311, 0x01, 0x3F);
  wr(ES8311, 0x02, rd(ES8311, 0x02) & 0x07);
  wr(ES8311, 0x03, 0x10);
  wr(ES8311, 0x04, 0x10);
  wr(ES8311, 0x05, 0x00);
  wr(ES8311, 0x06, (rd(ES8311, 0x06) & 0xC0) | 0x03);
  wr(ES8311, 0x07, rd(ES8311, 0x07) & 0xC0);
  wr(ES8311, 0x08, 0xFF);
  wr(ES8311, 0x00, rd(ES8311, 0x00) & 0xBF);
  wr(ES8311, 0x09, 0x0C);
  wr(ES8311, 0x0A, 0x0C);
  wr(ES8311, 0x0D, 0x01);
  wr(ES8311, 0x0E, 0x02);
  wr(ES8311, 0x12, 0x00);
  wr(ES8311, 0x13, 0x10);
  wr(ES8311, 0x1C, 0x6A);
  wr(ES8311, 0x37, 0x08);
  wr(ES8311, 0x31, 0x00);
  wr(ES8311, 0x32, 0xBF);  // DAC-Lautstärke fest 0 dB
  return true;
}

// Das Mikrofon (ES7210) wird nicht gebraucht: abschalten, damit es keinen Strom zieht
static void es7210Off() {
  if (!present(ES7210)) return;
  for (uint8_t reg = 0x47; reg <= 0x4C; reg++) wr(ES7210, reg, 0xFF);
  wr(ES7210, 0x01, 0x7F);
  wr(ES7210, 0x06, 0x07);
}

void shutdown() {
  digitalWrite(PA_EN, LOW);
  if (codecOk) {
    const uint8_t seq[][2] = {{0x32, 0x00}, {0x17, 0x00}, {0x0E, 0xFF}, {0x12, 0x02}, {0x14, 0x00}, {0x0D, 0xFA},
                              {0x15, 0x00}, {0x02, 0x10}, {0x00, 0x00}, {0x00, 0x1F}, {0x01, 0x30}, {0x01, 0x00},
                              {0x45, 0x00}, {0x0D, 0xFC}, {0x02, 0x00}};
    for (auto& r : seq) wr(ES8311, r[0], r[1]);
  }
}

bool begin(int volume) {
  pinMode(PA_EN, OUTPUT);
  digitalWrite(PA_EN, LOW);

  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 6;
  cc.dma_frame_num = BLOCK;
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &tx, nullptr) != ESP_OK) { Serial.println("Audio: I2S fehlgeschlagen"); return false; }
  i2s_std_config_t sc = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SR),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = (gpio_num_t)I2S_MCLK,
                   .bclk = (gpio_num_t)I2S_BCLK,
                   .ws = (gpio_num_t)I2S_WS,
                   .dout = (gpio_num_t)I2S_DOUT,
                   .din = I2S_GPIO_UNUSED,
                   .invert_flags = {false, false, false}},
  };
  sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  i2s_channel_init_std_mode(tx, &sc);
  i2s_channel_enable(tx);
  delay(10);  // MCLK muss laufen, bevor die Codecs konfiguriert werden

  codecOk = es8311Begin();
  es7210Off();
  setVolume(volume);

  music.cap = SR * 6;  // 6 s Musik (Puffer gegen Funklöcher)
  music.buf = (int16_t*)heap_caps_malloc(music.cap * 2, MALLOC_CAP_SPIRAM);
  fxQueue = xQueueCreate(24, sizeof(Voice));
  xTaskCreatePinnedToCore(task, "audio", 4096, nullptr, 19, &mixTask, 0);
  Serial.printf("Audio: Lautsprecher %s\n", codecOk ? "ok" : "–");
  return codecOk;
}

}  // namespace audio
