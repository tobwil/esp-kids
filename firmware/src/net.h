// WLAN im Hintergrund: verbindet sich mit dem stärksten bekannten Netz, stellt die Uhr per NTP.
#pragma once
#include <Arduino.h>

void netBegin();
void netPoll();                     // einmal pro Sekunde
bool netOnline();
void netConnect(const char* ssid, const char* pass);  // aus dem WLAN-Bildschirm
void netManual(bool on);            // WLAN-Bildschirm offen: Manager hält sich mit Suchen zurück
const char* netSsid();
String netIp();
int netRssi();
const char* netHost();              // mDNS-Name, z. B. „pixeluhr-e008“

// Suche für den WLAN-Bildschirm
struct ScanNet { char ssid[33]; int rssi; bool open; };
void netScanStart();
int netScanResult(ScanNet* out, int max);  // −1 = läuft noch

bool timeValid();                   // Uhrzeit bekannt (NTP)
