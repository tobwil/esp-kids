#include "imu.h"
#include <SensorQMI8658.hpp>
#include <Wire.h>
#include "config.h"
#include "pins.h"

namespace imu {

static SensorQMI8658 qmi;
static bool ok = false;

bool begin() {
  ok = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  if (ok) {
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_8G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0);
    qmi.disableAccelerometer();  // erst im Spiel einschalten
  }
  Serial.printf("Bewegungssensor: %s\n", ok ? "ok" : "nicht gefunden");
  return ok;
}

void sleep(bool on) {
  if (!ok) return;
  if (on) qmi.disableAccelerometer();
  else qmi.enableAccelerometer();
}

// Sensorachsen → Bildschirm (x nach rechts, y nach unten). Liegt die Uhr schräg, misst die nach oben zeigende
// Achse +g – die Kugel rollt in die Gegenrichtung.
static void toScreen(float x, float y, float& sx, float& sy) {
#if BOARD_SQUARE
  // 2.16: Sensor gegenüber dem 1.75C um 90° gedreht eingebaut
  sx = -IMU_SIGN_X * x;
  sy = IMU_SIGN_Y * y;
#else
  // 1.75C (siehe Waveshare-Beispiel): +x zeigt zur 12, +y zur 9
  sx = IMU_SIGN_X * y;
  sy = IMU_SIGN_Y * x;
#endif
}

static bool fakeOn = false;
static float fakeX = 0, fakeY = 0;

void fake(bool on, float gx, float gy) {
  fakeOn = on;
  fakeX = gx;
  fakeY = gy;
}

bool gravity(float& gx, float& gy) {
  if (fakeOn) { gx = fakeX; gy = fakeY; return true; }
  float x, y, z;
  if (!ok || !qmi.getAccelerometer(x, y, z)) return false;
  toScreen(x, y, gx, gy);
  return true;
}

}  // namespace imu
