// HTTPS-Server der Weboberfläche (Port 443, selbst ausgestelltes Zertifikat – der Browser warnt einmal).
// Alle Anfragen gehen an webui::handle; Port 80 leitet nur um (webui.cpp).
#pragma once
#include <Arduino.h>

namespace server {

void poll();  // aus der Hauptschleife: startet den Server, sobald WLAN da ist

}  // namespace server
