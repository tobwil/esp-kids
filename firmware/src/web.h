// HTTP(S) für Radio, Audiothek und Cover. Zertifikate werden mit dem eingebetteten
// Bundle (firmware/x509_crt_bundle) geprüft. Weiterleitungen werden verfolgt, auch von http nach https.
#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

namespace web {

// Offene Verbindung zum stückweisen Lesen (Audio, große Feeds)
struct Conn {
  HTTPClient http;
  NetworkClientSecure tls;
  NetworkClient plain;
  int code = 0;
  int size = -1;            // Content-Length, −1 = unbekannt
  String type;              // Content-Type
  int metaint = 0;          // Internetradio: Abstand der Titel-Einschübe (icy-metaint), 0 = keine
  NetworkClient* stream() { return http.getStreamPtr(); }
  ~Conn() { http.end(); }
};

// headers: "Name: Wert\nName2: Wert2"
bool open(Conn& c, String url, const char* headers = nullptr, uint32_t timeoutMs = 12000);
int get(const String& url, String& body, const char* headers = nullptr, size_t maxLen = 512 * 1024);
int post(const String& url, const String& body, const char* contentType, String& out, const char* headers = nullptr);
String urlencode(const char* s);

}  // namespace web
