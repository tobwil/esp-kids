// Bewegungssensor QMI8658: Neigung als Schwerkraft in Bildschirm-Richtungen (für das Balance-Spiel).
#pragma once

namespace imu {

bool begin();
void sleep(bool on);
// Schwerkraft in g, umgerechnet auf den Bildschirm: gx > 0 = nach rechts geneigt, gy > 0 = nach unten
bool gravity(float& gx, float& gy);
// Test über USB („tilt X Y“ / „tilt off“): feste Neigung statt des Sensors
void fake(bool on, float gx = 0, float gy = 0);

}  // namespace imu
