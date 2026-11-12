// WLAN im Hintergrund: verbindet sich mit dem stärksten bekannten Netz und dort mit dem stärksten Zugangspunkt
// (Repeater, Mesh), wechselt bei schwachem Empfang zu einem besseren, stellt die Uhr per NTP.
#pragma once
#include <Arduino.h>

void netBegin();
void netPoll();                     // einmal pro Sekunde
bool netOnline();
// bssid/channel: genau diesen Zugangspunkt (Repeater), sonst den stärksten mit dem Namen
void netConnect(const char* ssid, const char* pass, const uint8_t* bssid = nullptr, int32_t channel = 0);
void netManual(bool on);            // WLAN-Bildschirm offen: Manager hält sich mit Suchen zurück
const char* netSsid();
String netIp();
int netRssi();
String netAp();                     // verbundener Zugangspunkt: „AA:BB:…  · Kanal 6“ ("" = keiner)
String netDrops();                  // Abbrüche seit dem Start mit dem letzten Grund (Weboberfläche)
void netRoamNow();                  // Test: sofort nach einem besseren Zugangspunkt suchen
const char* netHost();              // mDNS-Name, z. B. „pixeluhr-e008“

// Suche für den WLAN-Bildschirm
struct ScanNet { char ssid[33]; int rssi; bool open; };
void netScanStart();
int netScanResult(ScanNet* out, int max);  // −1 = läuft noch

bool timeValid();                   // Uhrzeit bekannt (NTP)
