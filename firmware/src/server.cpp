#include "server.h"
#include <Preferences.h>
#include <esp_https_server.h>
#include <esp_random.h>
#include <mbedtls/ecp.h>
#include <mbedtls/pk.h>
#include <mbedtls/oid.h>
#include <mbedtls/x509_crt.h>
#include <time.h>
#include "net.h"
#include "webui.h"

namespace server {

static httpd_handle_t srv = nullptr;
static String cert, key;

// ------------------------------------------------------------------ Zertifikat (einmal erzeugt, im NVS)

static int rng(void*, unsigned char* out, size_t n) { esp_fill_random(out, n); return 0; }

// Selbst ausgestelltes Zertifikat nach den Regeln der Browser: höchstens 397 Tage gültig (Chrome/Brave lehnen
// längere ab – „Gültigkeitsdauer zu lang“), Name und IP als Subject Alternative Name, Serverzertifikat (EKU).
// Liegt in NVS („tls“, Fassung „v“) und wird 30 Tage vor Ablauf erneuert. Ohne bekannte Uhrzeit zählt das Baudatum.
static const int CERT_VERSION = 3, CERT_DAYS = 397;  // 3: neuer Name „pixeluhr-…“

static time_t buildTime() {  // __DATE__ „Oct 24 2026“
  static const char* M = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = "";
  int d = 1, y = 2026;
  sscanf(__DATE__, "%3s %d %d", mon, &d, &y);
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = (int)((strstr(M, mon) - M) / 3);
  t.tm_mday = d;
  t.tm_hour = 12;
  return mktime(&t);
}

static void stamp(time_t when, char* out) {  // „YYYYMMDDhhmmss“ (UTC)
  struct tm t;
  gmtime_r(&when, &t);
  strftime(out, 15, "%Y%m%d%H%M%S", &t);
}

static bool certFresh() {  // Fassung stimmt und (bei bekannter Uhrzeit) noch mehr als 30 Tage gültig
  mbedtls_x509_crt c;
  mbedtls_x509_crt_init(&c);
  bool ok = !mbedtls_x509_crt_parse(&c, (const unsigned char*)cert.c_str(), cert.length() + 1);
  if (ok && timeValid()) {
    struct tm e = {};
    e.tm_year = c.valid_to.year - 1900;
    e.tm_mon = c.valid_to.mon - 1;
    e.tm_mday = c.valid_to.day;
    ok = mktime(&e) - time(nullptr) > 30L * 86400;
  }
  mbedtls_x509_crt_free(&c);
  return ok;
}

static bool makeCert() {
  Preferences p;
  p.begin("tls", true);
  cert = p.getString("crt", "");
  key = p.getString("key", "");
  int version = p.getInt("v", 1);
  String forIp = p.getString("ip", "");
  p.end();
  bool sameIp = forIp == netIp();  // neue IP vom Router: neues Zertifikat, sonst passt der Name im Zertifikat nicht
  if (cert.length() && key.length() && version == CERT_VERSION && sameIp && certFresh()) return true;
  Serial.println("Weboberfläche: erzeuge Zertifikat …");
  mbedtls_pk_context pk;
  mbedtls_x509write_cert crt;
  mbedtls_pk_init(&pk);
  mbedtls_x509write_crt_init(&crt);
  bool ok = false;
  unsigned char* buf = (unsigned char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  do {
    if (!buf || mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) break;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), rng, nullptr)) break;
    char subj[96];
    snprintf(subj, sizeof(subj), "CN=%s.local,O=Pixeluhr", netHost());
    mbedtls_x509write_crt_set_subject_key(&crt, &pk);
    mbedtls_x509write_crt_set_issuer_key(&crt, &pk);
    mbedtls_x509write_crt_set_subject_name(&crt, subj);
    mbedtls_x509write_crt_set_issuer_name(&crt, subj);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    unsigned char serial[8];
    esp_fill_random(serial, sizeof(serial));
    serial[0] &= 0x7F;
    mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial));
    time_t from = (timeValid() ? time(nullptr) : buildTime()) - 86400;  // ein Tag Puffer für schiefe Uhren
    char nb[16], na[16];
    stamp(from, nb);
    stamp(from + (time_t)CERT_DAYS * 86400, na);
    if (mbedtls_x509write_crt_set_validity(&crt, nb, na)) break;
    mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);  // kein CA, ein Serverzertifikat
    mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_AGREEMENT);
    mbedtls_asn1_sequence eku = {};
    eku.buf.tag = MBEDTLS_ASN1_OID;
    eku.buf.p = (unsigned char*)MBEDTLS_OID_SERVER_AUTH;
    eku.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
    mbedtls_x509write_crt_set_ext_key_usage(&crt, &eku);
    // Subject Alternative Name: Name im Netz und die aktuelle IP
    char dns[48];
    snprintf(dns, sizeof(dns), "%s.local", netHost());
    IPAddress ipa;
    ipa.fromString(netIp());
    unsigned char ip4[4] = {ipa[0], ipa[1], ipa[2], ipa[3]};
    mbedtls_x509_san_list ipSan = {}, dnsSan = {};
    dnsSan.node.type = MBEDTLS_X509_SAN_DNS_NAME;
    dnsSan.node.san.unstructured_name.p = (unsigned char*)dns;
    dnsSan.node.san.unstructured_name.len = strlen(dns);
    ipSan.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
    ipSan.node.san.unstructured_name.p = ip4;
    ipSan.node.san.unstructured_name.len = 4;
    dnsSan.next = ip4[0] ? &ipSan : nullptr;
    if (mbedtls_x509write_crt_set_subject_alternative_name(&crt, &dnsSan)) break;
    if (mbedtls_x509write_crt_pem(&crt, buf, 4096, rng, nullptr)) break;
    cert = (const char*)buf;
    if (mbedtls_pk_write_key_pem(&pk, buf, 4096)) break;
    key = (const char*)buf;
    ok = true;
  } while (0);
  mbedtls_x509write_crt_free(&crt);
  mbedtls_pk_free(&pk);
  if (buf) heap_caps_free(buf);
  if (!ok) { Serial.println("Weboberfläche: Zertifikat fehlgeschlagen"); return false; }
  p.begin("tls", false);
  p.putString("crt", cert);
  p.putString("key", key);
  p.putInt("v", CERT_VERSION);
  p.putString("ip", netIp());
  p.end();
  return true;
}

// ------------------------------------------------------------------ Start, sobald WLAN da ist

void poll() {
  static uint32_t triedAt = 0;
  if (srv || !netOnline() || (triedAt && millis() - triedAt < 60000)) return;
  triedAt = millis();
  if (!makeCert()) return;
  httpd_ssl_config_t c = HTTPD_SSL_CONFIG_DEFAULT();
  c.servercert = (const uint8_t*)cert.c_str();
  c.servercert_len = cert.length() + 1;
  c.prvtkey_pem = (const uint8_t*)key.c_str();
  c.prvtkey_len = key.length() + 1;
  c.httpd.max_open_sockets = 5;
  c.httpd.lru_purge_enable = true;
  c.httpd.uri_match_fn = httpd_uri_match_wildcard;
  c.httpd.stack_size = 8192;
  c.httpd.ctrl_port = 32769;  // der Umleitungs-Server auf Port 80 belegt den Standard
  if (httpd_ssl_start(&srv, &c) != ESP_OK) { srv = nullptr; Serial.println("Weboberfläche: HTTPS-Start fehlgeschlagen"); return; }
  httpd_uri_t pg = {.uri = "/*", .method = HTTP_GET, .handler = webui::handle, .user_ctx = nullptr};
  httpd_uri_t pp = {.uri = "/*", .method = HTTP_POST, .handler = webui::handle, .user_ctx = nullptr};
  httpd_register_uri_handler(srv, &pg);
  httpd_register_uri_handler(srv, &pp);
  Serial.printf("Weboberfläche: https://%s/\n", netIp().c_str());
}

}  // namespace server
