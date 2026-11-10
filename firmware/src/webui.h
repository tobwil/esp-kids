// Weboberfläche für die Eltern: https://<IP der Uhr> (http:// leitet um). Freigaben und Minuten pro Tag,
// Schlafenszeit, Eltern-PIN, Freigabeliste für Radio und Audiothek, Fernbedienung, Akku, WLAN, Updates.
// Ist eine Eltern-PIN gesetzt, braucht jede Anfrage sie. Nur im eigenen WLAN erreichbar.
#pragma once
#include <esp_http_server.h>

namespace webui {

void begin();
void poll();                       // aus der Hauptschleife: führt die anstehende Anfrage aus
esp_err_t handle(httpd_req_t* r);  // vom HTTPS-Server (server.cpp)

}  // namespace webui
