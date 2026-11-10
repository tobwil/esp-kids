#include "web.h"

extern const uint8_t crtBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t crtBundleEnd[] asm("_binary_x509_crt_bundle_end");

namespace web {

static void addHeaders(HTTPClient& h, const char* headers) {
  h.setUserAgent("Pixeluhr/1.0 (ESP32)");
  if (!headers) return;
  String all = headers;
  int pos = 0;
  while (pos < (int)all.length()) {
    int nl = all.indexOf('\n', pos);
    if (nl < 0) nl = all.length();
    String line = all.substring(pos, nl);
    int c = line.indexOf(':');
    if (c > 0) {
      String v = line.substring(c + 1);
      v.trim();
      h.addHeader(line.substring(0, c), v);
    }
    pos = nl + 1;
  }
}

bool open(Conn& c, String url, const char* headers, uint32_t timeoutMs) {
  static const char* keep[] = {"Location", "Content-Type", "icy-metaint"};
  for (int hop = 0; hop < 6; hop++) {
    c.http.end();
    bool https = url.startsWith("https://");
    bool ok;
    if (https) {
      c.tls.setCACertBundle(crtBundleStart, crtBundleEnd - crtBundleStart);
      c.tls.setTimeout(timeoutMs / 1000);
      ok = c.http.begin(c.tls, url);
    } else {
      ok = c.http.begin(c.plain, url);
    }
    if (!ok) { c.code = -1; return false; }
    c.http.setTimeout(timeoutMs);
    c.http.setConnectTimeout(timeoutMs);
    c.http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    c.http.collectHeaders(keep, 3);
    c.http.useHTTP10(true);  // kein Chunked-Encoding beim Streamen
    addHeaders(c.http, headers);
    c.code = c.http.GET();
    if (c.code >= 300 && c.code < 400 && c.http.hasHeader("Location")) {
      String loc = c.http.header("Location");
      if (loc.startsWith("/")) {  // relativ zur gleichen Adresse
        int s = url.indexOf('/', url.indexOf("//") + 2);
        loc = (s > 0 ? url.substring(0, s) : url) + loc;
      }
      url = loc;
      continue;
    }
    c.size = c.http.getSize();
    c.type = c.http.header("Content-Type");
    c.metaint = c.http.header("icy-metaint").toInt();
    return c.code == 200 || c.code == 206;
  }
  return false;
}

int get(const String& url, String& body, const char* headers, size_t maxLen) {
  Conn* c = new Conn();
  body = "";
  int code = -1;
  if (open(*c, url, headers)) {
    NetworkClient* s = c->stream();
    uint8_t buf[1024];
    uint32_t idle = millis();
    while (body.length() < maxLen && (s->connected() || s->available())) {
      int n = s->read(buf, sizeof(buf));
      if (n > 0) { body.concat((const char*)buf, n); idle = millis(); }
      else if (millis() - idle > 8000) break;
      else delay(2);
      if (c->size >= 0 && (int)body.length() >= c->size) break;
    }
  }
  code = c->code;
  delete c;
  return code;
}

int post(const String& url, const String& body, const char* contentType, String& out, const char* headers) {
  NetworkClientSecure* tls = new NetworkClientSecure();
  tls->setCACertBundle(crtBundleStart, crtBundleEnd - crtBundleStart);
  HTTPClient* h = new HTTPClient();
  int code = -1;
  if (h->begin(*tls, url)) {
    h->setTimeout(20000);
    h->useHTTP10(true);  // kein Chunked-Encoding: große Antworten sonst abgeschnitten
    addHeaders(*h, headers);
    h->addHeader("Content-Type", contentType);
    code = h->POST((uint8_t*)body.c_str(), body.length());
    if (code > 0) {
      NetworkClient* s = h->getStreamPtr();
      int size = h->getSize();
      out = "";
      uint8_t buf[2048];
      uint32_t idle = millis();
      while ((s->connected() || s->available()) && (size < 0 || (int)out.length() < size)) {
        int n = s->read(buf, sizeof(buf));
        if (n > 0) { out.concat((const char*)buf, n); idle = millis(); }
        else if (millis() - idle > 20000) break;
        else delay(2);
      }
    }
    h->end();
  }
  delete h;
  delete tls;
  return code;
}

String urlencode(const char* s) {
  String o;
  for (; *s; s++) {
    uint8_t c = *s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { char b[4]; snprintf(b, sizeof(b), "%%%02X", c); o += b; }
  }
  return o;
}

}  // namespace web
