// Hardware: Display + LVGL, Touch, Tasten, Akku (AXP2101), Bildschirmfotos und Fernsteuerung über USB.
#pragma once
#include <Arduino.h>
#include <lvgl.h>

enum BtnEvent : uint8_t {
  BTN_NONE,
  BOOT_SHORT,       // BOOT gedrückt (beim Loslassen)
  PWR_SHORT,        // PWR kurz
  PWR_LONG,         // PWR lang (≈ 1,5 s)
};

void hwBegin();
void hwLoop();                 // Tasten, Akku, serielle Befehle
BtnEvent hwButton();           // nächstes Tasten-Ereignis (oder BTN_NONE)

void hwBrightness(uint8_t pct, bool ignoreCap = false);  // ignoreCap: Taschenlampe
void hwBrightnessCap(uint8_t pct);  // Obergrenze (Sparmodus, Schlafenszeit); 100 = keine
void hwScreen(bool on);        // Display an/aus (Touch weckt es wieder)
bool hwScreenOn();
void hwActivity();             // Bedienung: Zeit bis zum Abschalten neu starten
void hwTouchPoll();  // Touch lesen ohne den Rest der Oberfläche (beim Dösen, für den Doppeltipp)
bool hwTouchRecent();  // in den letzten 0,8 s berührt (dann eng abfragen, damit der Doppeltipp erkannt wird)
// Dösen: die Hauptschleife schläft bis zu ms, wacht aber sofort bei Berührung (Touch-INT), BOOT oder hwDozeKick()
// (z. B. eine Anfrage an die Weboberfläche) – statt alle 20 ms den Touch-Chip zu fragen.
void hwDozeWait(uint32_t ms);
void hwDozeKick();
uint32_t hwIdleMs();           // ms seit der letzten Bedienung
bool hwWakeRequested();        // Touch bei dunklem Display
// letzter Wisch/Tipp, wie ihn der Touch-Treiber gesehen hat (jeder Lesevorgang, nicht nur alle 100 ms):
// id zählt je Berührung hoch, dx/dy vom Aufsetzen bis zum letzten Punkt, released = schon losgelassen
void hwStroke(uint32_t& id, int& dx, int& dy, bool& released);

int hwBattery();               // %, −1 = kein Akku
int hwBatteryMv();             // Akkuspannung in mV, 0 = unbekannt
bool hwUsb();
bool hwCharging();
float hwTempBattery();         // °C im Ladechip neben dem Akku (nicht die Zelle selbst; beim Laden wärmer), NAN = unbekannt
float hwTempCpu();             // °C im Prozessor (NAN = unbekannt)
void hwPowerOff();
void hwDeepSleep(uint32_t wakeInS);  // kehrt nicht zurück; 0 = nur Tasten/Touch wecken
bool hwWokeFromSleep();
