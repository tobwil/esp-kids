// Eltern-Menü (Zifferblatt lange drücken, Eltern-PIN): Apps freigeben oder sperren, WLAN, QR-Code zur
// Weboberfläche, Eltern-PIN, Infos, Neustart. Alles Weitere stellt man in der Weboberfläche ein.
#include <WiFi.h>
#include "../audio.h"
#include "../kids.h"
#include "../net.h"
#include "../settings.h"
#include "config.h"
#include "ui.h"

using namespace ui;

extern const ui::Page PAGE_WIFI_, PAGE_WEB_, PAGE_INFO_;

// ================================================================== Übersicht

static lv_obj_t *rApps[quota::APPS], *rWifi, *rWeb, *rPin, *rInfo;

static void parentResume() {
  char b[64];
  for (int a = 0; a < quota::APPS; a++) {
    quota::App app = (quota::App)a;
    switch (quota::status(app)) {
      case quota::NIGHT: snprintf(b, sizeof(b), "Schlafenszeit"); break;
      case quota::USED_UP: snprintf(b, sizeof(b), "aufgebraucht (%d Min.)", quota::usedMin(app)); break;
      case quota::LOCKED: snprintf(b, sizeof(b), "gesperrt"); break;
      default: {
        long left = quota::leftS(app);
        if (left < 0) snprintf(b, sizeof(b), "frei");
        else fmtLeft(b, sizeof(b), left);
      }
    }
    listRowSub(rApps[a], b);
  }
  if (netOnline()) snprintf(b, sizeof(b), "%s", netSsid());
  else snprintf(b, sizeof(b), knownCount ? "Nicht verbunden" : "Nicht eingerichtet");
  listRowSub(rWifi, b);
  listRowSub(rWeb, netOnline() ? "Am Handy einrichten" : "Erst WLAN verbinden");
  listRowSub(rPin, kids::hasPin() ? "Ändern" : "Noch keine – bitte festlegen");
  int bat = hwBattery();
  if (bat >= 0) snprintf(b, sizeof(b), "Akku %d %%%s", bat, hwCharging() ? " · lädt" : "");
  else snprintf(b, sizeof(b), "Version " FW_VERSION);
  listRowSub(rInfo, b);
}

static void parentBuild(lv_obj_t* scr) {
  lv_obj_t* l = roundList(scr);
  listTitle(l, "Eltern", C_SKY());
  for (int a = 0; a < quota::APPS; a++) {
    rApps[a] = listRow(l, appIcon((quota::App)a), appColor((quota::App)a), quota::name((quota::App)a), " ");
    lv_obj_add_event_cb(rApps[a], [](lv_event_t* e) {
      play(0);
      unlockDialog((quota::App)(intptr_t)lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, (void*)(intptr_t)a);
  }
  struct R { lv_obj_t** row; const char* icon; const char* text; const ui::Page* page; };
  static const R rows[] = {
      {&rWifi, I_WIFI, "WLAN", &PAGE_WIFI_},
      {&rWeb, I_SERVER, "Weboberfläche", &PAGE_WEB_},
      {&rPin, I_LOCK, "Eltern-PIN", nullptr},
      {&rInfo, I_INFO, "Über die Uhr", &PAGE_INFO_},
  };
  for (auto& r : rows) {
    *r.row = listRow(l, r.icon, C_SKY(), r.text, " ");
    lv_obj_add_event_cb(*r.row, [](lv_event_t* e) {
      const R* r = (const R*)lv_event_get_user_data(e);
      play(0);
      if (r->page) push(r->page);
      else newPin();
    }, LV_EVENT_CLICKED, (void*)&r);
  }
  parentResume();
  listFit(l);
}

static void parentUpdate() {
  static uint32_t at = 0;
  if (millis() - at < 2000) return;
  at = millis();
  parentResume();
}

const Page PAGE_PARENT = {parentBuild, parentUpdate, nullptr, parentResume, false};

// ================================================================== WLAN

static lv_obj_t *wList, *wStatus;
static bool wScanning = false;
static char wPick[33];
static ScanNet wNets[16];
static int wCount = 0;

static void wifiFill();

static void wifiConnectTo(const char* ssid, const char* pass) {
  knownAdd(ssid, pass);
  settingsSave();
  netConnect(ssid, pass);
  char b[64];
  snprintf(b, sizeof(b), I_WIFI "  Verbinde mit %s …", ssid);
  toast(b);
}

static void wifiScan() {
  wScanning = true;
  netScanStart();
  wifiFill();
}

static void wifiFill() {
  lv_obj_clean(wList);
  listTitle(wList, "WLAN", C_SKY());
  char b[64];
  if (netOnline()) snprintf(b, sizeof(b), "Verbunden · %s", netIp().c_str());
  else snprintf(b, sizeof(b), "Nicht verbunden");
  wStatus = label(wList, &font_text_20, C_DIM(), b);
  if (wScanning) label(wList, &font_text_20, C_DIM(), "Suche Netze …");
  for (int i = 0; i < wCount; i++) {
    bool isKnown = knownIndex(wNets[i].ssid) >= 0;
    bool cur = netOnline() && !strcmp(netSsid(), wNets[i].ssid);
    char sub[48];
    snprintf(sub, sizeof(sub), "%s%s%d dBm", cur ? "Verbunden · " : isKnown ? "Gespeichert · " : "",
             wNets[i].open ? "offen · " : "", wNets[i].rssi);
    lv_obj_t* r = listRow(wList, wNets[i].open ? I_WIFI : I_LOCK, cur ? C_RADIO() : isKnown ? C_SKY() : C_DIM(),
                          wNets[i].ssid, sub);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      int i = (int)(intptr_t)lv_event_get_user_data(e);
      snprintf(wPick, sizeof(wPick), "%s", wNets[i].ssid);
      const char* pw = knownPass(wPick);
      if (pw) wifiConnectTo(wPick, pw);
      else if (wNets[i].open) wifiConnectTo(wPick, "");
      else keyboard(wPick, "", true, [](const char* pass) { wifiConnectTo(wPick, pass); });
    }, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      int i = (int)(intptr_t)lv_event_get_user_data(e);
      if (knownIndex(wNets[i].ssid) < 0) return;
      knownForget(wNets[i].ssid);
      settingsSave();
      toast("Netz vergessen");
      wifiFill();
      lv_indev_wait_release(lv_indev_active());
    }, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
  }
  if (!wScanning) {
    lv_obj_t* p = pill(wList, I_REFRESH "  Neu suchen", C_SURF2(), C_TEXT());
    lv_obj_add_event_cb(p, [](lv_event_t*) { wifiScan(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* h = label(wList, &font_text_20, C_DIM(), "Lange drücken = vergessen");
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  }
  listFit(wList);
}

static void wifiBuild(lv_obj_t* scr) {
  netManual(true);
  wList = roundList(scr);
  wCount = 0;
  wifiScan();
}

static void wifiUpdate() {
  static uint32_t at = 0;
  static bool wasOnline = false;
  if (wScanning) {
    int n = netScanResult(wNets, 16);
    if (n >= 0) { wCount = n; wScanning = false; wifiFill(); }
  } else if (netOnline() != wasOnline || millis() - at > 5000) {
    at = millis();
    char b[64];
    if (netOnline()) snprintf(b, sizeof(b), "Verbunden · %s", netIp().c_str());
    else snprintf(b, sizeof(b), "Nicht verbunden");
    if (wStatus) lv_label_set_text(wStatus, b);
    if (netOnline() != wasOnline) wifiFill();
  }
  wasOnline = netOnline();
}

static void wifiLeave() { netManual(false); wStatus = nullptr; }

const ui::Page PAGE_WIFI_ = {wifiBuild, wifiUpdate, wifiLeave, nullptr, false};

// ================================================================== Über die Uhr

static lv_obj_t* iText;

static void infoUpdate() {
  static uint32_t at = 0;
  if (at && millis() - at < 1000) return;
  at = millis();
  char b[300];
  int bat = hwBattery();
  char batS[32];
  if (bat >= 0) snprintf(batS, sizeof(batS), "%d %%%s", bat, hwCharging() ? ", lädt" : hwUsb() ? ", USB" : "");
  else snprintf(batS, sizeof(batS), "kein Akku");
  snprintf(b, sizeof(b), "%s\nAkku: %s\nWLAN: %s\nIP: %s\nSpeicher: %u KB frei\nVersion " FW_VERSION, cfg.name, batS,
           netOnline() ? netSsid() : "–", netOnline() ? netIp().c_str() : "–",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  lv_label_set_text(iText, b);
}

static void infoBuild(lv_obj_t* scr) {
  lv_obj_t* l = roundList(scr);
  listTitle(l, "Über die Uhr", C_SKY());
  iText = label(l, &font_text_20, C_TEXT(), "");
  lv_obj_set_style_text_align(iText, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(iText, 6, 0);
  lv_obj_t* r = pill(l, I_RESTART "  Neustart", C_SURF2(), C_TEXT());
  lv_obj_add_event_cb(r, [](lv_event_t*) { ESP.restart(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* o = pill(l, I_POWER "  Ausschalten", C_RED(), C_TEXT());
  lv_obj_add_event_cb(o, [](lv_event_t*) { audio::shutdown(); delay(50); hwPowerOff(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* h = label(l, &font_text_20, C_DIM(), "Einschalten: PWR-Taste");
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  infoUpdate();
  listFit(l);
}

const ui::Page PAGE_INFO_ = {infoBuild, infoUpdate, nullptr, nullptr, false};

// ================================================================== Weboberfläche: QR-Code mit der Adresse

static void webBuild(lv_obj_t* scr) {
  title(scr, "Weboberfläche", C_SKY());
  if (!netOnline()) {
    lv_obj_t* t = label(scr, &font_text_24, C_TEXT(), "Erst mit dem WLAN\nverbinden");
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(t);
    return;
  }
  static char url[48];
  snprintf(url, sizeof(url), "https://%s/", netIp().c_str());  // IP statt .local: klappt auch auf Android
  lv_obj_t* qr = lv_qrcode_create(scr);
  lv_qrcode_set_size(qr, 200);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_qrcode_update(qr, url, strlen(url));
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(qr, 12, 0);
  lv_obj_set_style_radius(qr, 12, 0);
  lv_obj_align(qr, LV_ALIGN_CENTER, 0, -6);
  lv_obj_t* a = label(scr, &font_text_20, C_TEXT(), netIp().c_str());
  lv_obj_align(a, LV_ALIGN_BOTTOM_MID, 0, -66);
  char b[48];
  snprintf(b, sizeof(b), "%s.local", netHost());
  lv_obj_t* h = label(scr, &font_text_20, C_DIM(), b);
  lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -40);
}

const ui::Page PAGE_WEB_ = {webBuild, nullptr, nullptr, nullptr, false};
