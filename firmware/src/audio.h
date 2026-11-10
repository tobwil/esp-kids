// Audio: ES8311 (Lautsprecher) über I2S mit 44,1 kHz. Ein Mischer auf Kern 0 spielt Musik (44,1 kHz mono) und
// kleine Klänge. Das Mikrofon (ES7210) wird nicht gebraucht und bleibt abgeschaltet.
#pragma once
#include <Arduino.h>

namespace audio {

static const int SR = 44100;  // wie die meisten Streams

enum Fx : uint8_t { FX_TAP, FX_OK, FX_ERROR, FX_CHIME, FX_ALARM, FX_TICK };

bool begin(int volume);
void setVolume(int pct);
void play(Fx fx);
void shutdown();            // vor dem Tiefschlaf

// Musik: der Strom-Task schreibt, der Mischer liest
size_t musicSpace();
size_t musicUsed();
void musicWrite(const int16_t* s, size_t n);
void musicFlush();
void musicPause(bool p);    // Mischer hält an (Puffer bleibt)
uint32_t musicPlayed();     // abgespielte Abtastwerte seit dem letzten Flush

}  // namespace audio
