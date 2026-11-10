#include "webui.h"
#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include "config.h"
#include "faceart.h"
#include "hw.h"
#include "kids.h"
#include "music.h"
#include "net.h"
#include "power.h"
#include "quota.h"
#include "settings.h"
#include "weather.h"
#include "ui/ui.h"

extern const char TF_BOARD_TAG[];  // main.cpp

namespace webui {

// ------------------------------------------------------------------ Anfragen
// Die Weboberfläche läuft auf dem HTTPS-Server (server.cpp). Dessen Task nimmt eine Anfrage an und übergibt sie an die
// Hauptschleife (Oberfläche und Musik sind nicht threadsicher); die Antwort schickt wieder der HTTPS-Task.
// Die Handler sprechen „srv->…“ wie beim Arduino-WebServer. Port 80 leitet nur noch auf https um.
struct Http {
  String uri, query, body, pin;  // Anfrage
  int code = 0;                  // Antwort
  String type, out, disp;
  const char* outP = nullptr;
  void (*fn)() = nullptr;
  volatile int state = 0;        // 0 frei, 1 wartet auf die Hauptschleife, 2 läuft dort
  SemaphoreHandle_t ready = nullptr;

  String arg(const char* name) const;
  String header(const char*) const { return pin; }  // nur X-Pin wird gebraucht
  void sendHeader(const char*, const String& v) { disp = v; }  // nur Content-Disposition
  void send(int c, const char* t, const String& b) { code = c; type = t; out = b; outP = nullptr; xSemaphoreGive(ready); }
  void send_P(int c, const char* t, const char* p) { code = c; type = t; out = ""; outP = p; xSemaphoreGive(ready); }
};

static Http req;
static Http* const srv = &req;
static WebServer* web80 = nullptr;

static String urlDecode(const String& v) {
  String o;
  for (unsigned i = 0; i < v.length(); i++) {
    char c = v[i];
    if (c == '+') c = ' ';
    else if (c == '%' && i + 2 < v.length()) { c = strtol(v.substring(i + 1, i + 3).c_str(), nullptr, 16); i += 2; }
    o += c;
  }
  return o;
}

String Http::arg(const char* name) const {
  if (!strcmp(name, "plain")) return body;
  int n = strlen(name);
  for (int i = 0; i < (int)query.length();) {
    int amp = query.indexOf('&', i);
    if (amp < 0) amp = query.length();
    if (!strncmp(query.c_str() + i, name, n) && (i + n == amp || query[i + n] == '='))
      return i + n == amp ? "" : urlDecode(query.substring(i + n + 1, amp));
    i = amp + 1;
  }
  return "";
}

struct Route { const char* path; bool post; void (*fn)(); };
static Route routes[32];
static int nRoutes = 0;

static void on(const char* path, http_method m, void (*fn)()) {
  if (nRoutes < 32) routes[nRoutes++] = {path, m == HTTP_POST, fn};
}

// Für das Handy gebaut: eine Spalte, große Schalter, unten die Reiter. Hell und dunkel nach Systemeinstellung.
static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0d0d0f">
<title>Pixeluhr</title>
<style>
:root{color-scheme:light dark;--bg:#f2f0eb;--card:#fff;--fill:#f0ede7;--fill2:#e4e0d8;--text:#16151a;--dim:#7a766e;--line:#e8e4dc;
--acc:#ec6436;--acc2:#ff9a6a;--ok:#159b67;--bad:#e0483e;--warn:#d98c12;--glass:rgba(250,248,244,.78);
--game:#e0a800;--radio:#1fae74;--story:#ec6436;
--shadow:0 1px 2px rgba(30,24,12,.05),0 12px 32px -14px rgba(30,24,12,.22);--r:24px}
@media (prefers-color-scheme:dark){:root{--bg:#000;--card:#121214;--fill:#1d1d20;--fill2:#2a2a2e;--text:#f6f3ec;--dim:#8f8b84;--line:#242428;
--acc:#ff7a4a;--acc2:#ffa985;--ok:#34d399;--bad:#ff6b5f;--warn:#fbbf24;--glass:rgba(18,18,20,.74);
--game:#ffc83a;--radio:#3ddc97;--story:#ff7a59;--shadow:0 0 0 1px rgba(255,255,255,.06)}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 -apple-system,BlinkMacSystemFont,"SF Pro Text",Inter,system-ui,sans-serif;-webkit-font-smoothing:antialiased}
header{position:sticky;top:0;z-index:5;padding:calc(14px + env(safe-area-inset-top)) 20px 12px;background:var(--glass);-webkit-backdrop-filter:saturate(180%) blur(20px);backdrop-filter:saturate(180%) blur(20px);border-bottom:1px solid var(--line)}
header h1{display:flex;align-items:center;flex-wrap:wrap;gap:8px;margin:0;font-size:28px;line-height:1.15;font-weight:780;letter-spacing:-.025em}
#stat{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 0}
.chip{display:inline-flex;align-items:center;gap:5px;padding:4px 10px;border-radius:99px;background:var(--fill);color:var(--dim);font-size:13px;font-weight:650;white-space:nowrap;max-width:100%;overflow:hidden;text-overflow:ellipsis}
.chip.g{color:var(--ok)}.chip.y{color:var(--warn)}.chip.r{color:var(--bad)}
.badge{padding:4px 11px;border-radius:99px;background:linear-gradient(135deg,#9d86ff,#6b52e0);color:#fff;font-size:12px;font-weight:750;letter-spacing:.02em}
.badge[hidden]{display:none}
main{max-width:600px;margin:0 auto;padding:16px 16px calc(112px + env(safe-area-inset-bottom))}
section{margin:0 0 14px;padding:18px;border-radius:var(--r);background:var(--card);box-shadow:var(--shadow)}
h2{display:flex;align-items:center;gap:8px;margin:0 0 10px;color:var(--dim);font-size:12.5px;font-weight:750;letter-spacing:.07em;text-transform:uppercase}
h2:before{content:"";flex:none;width:8px;height:8px;border-radius:50%;background:linear-gradient(135deg,var(--acc2),var(--acc))}
h3{margin:16px 0 4px;font-size:15px}
p.note,.note{margin:8px 0;color:var(--dim);font-size:14px;line-height:1.45}
.warnbox{padding:12px 14px;border-radius:16px;background:rgba(224,72,62,.12);color:var(--bad);font-weight:650}
label.f{display:block;margin:16px 0 6px;color:var(--dim);font-size:13px;font-weight:650}
input,select{display:block;width:100%;min-width:0;max-width:100%;min-height:48px;margin:0;padding:12px 14px;border:1px solid transparent;border-radius:14px;outline:0;background:var(--fill);color:var(--text);font:inherit;font-size:16px;-webkit-appearance:none;appearance:none;transition:border-color .15s,box-shadow .15s}
input:focus,select:focus{border-color:var(--acc);box-shadow:0 0 0 4px rgba(236,100,54,.16)}
input[type=time]{font-variant-numeric:tabular-nums;text-align:left}
input[type=time]::-webkit-date-and-time-value{text-align:left}
input::placeholder{color:var(--dim);opacity:.8}
select{padding-right:40px;overflow:hidden;white-space:nowrap;text-overflow:ellipsis;background-image:url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%238f8b84' stroke-width='2.5' stroke-linecap='round' stroke-linejoin='round'><path d='M6 9l6 6 6-6'/></svg>");background-repeat:no-repeat;background-position:right 14px center;background-size:16px}
input[type=file]{padding:12px;font-size:14px}
button{min-height:48px;padding:12px 18px;border:0;border-radius:14px;background:linear-gradient(135deg,var(--acc2),var(--acc));color:#fff;font:inherit;font-weight:700;cursor:pointer;box-shadow:0 8px 18px -10px rgba(236,100,54,.9);transition:transform .12s,filter .15s}
button:active{transform:scale(.97);filter:brightness(.96)}
button.wide{display:block;width:100%;margin-top:12px}
button.ghost{min-height:42px;padding:8px 14px;background:var(--fill);color:var(--text);font-weight:650;box-shadow:none}
button.on{background:linear-gradient(135deg,#43e3a5,var(--ok));color:#fff;box-shadow:none}
.bt{display:flex;flex:none;gap:6px}
.row{display:flex;align-items:center;gap:12px;min-height:58px;padding:8px 0;border-top:1px solid var(--line)}
.row:first-child,h2+.row{border-top:0}
.row .t{flex:1;min-width:0}.row .t b{display:block;overflow:hidden;font-weight:650;white-space:nowrap;text-overflow:ellipsis}
.row small{display:block;overflow:hidden;color:var(--dim);font-size:13px;white-space:nowrap;text-overflow:ellipsis}
.sw{position:relative;flex:none;width:51px;height:31px}
.sw input{position:absolute;inset:0;z-index:1;min-height:0;margin:0;opacity:0;cursor:pointer}
.sw span{position:absolute;inset:0;border-radius:99px;background:var(--fill2);transition:background .25s}
.sw span:after{content:"";position:absolute;top:2px;left:2px;width:27px;height:27px;border-radius:50%;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.28);transition:transform .3s cubic-bezier(.3,1.5,.5,1)}
.sw input:checked+span{background:linear-gradient(135deg,var(--acc2),var(--acc))}
.sw input:checked+span:after{transform:translateX(20px)}
.two{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:10px}.two>*{min-width:0}
.seg{display:flex;gap:2px;margin-top:12px;padding:4px;border-radius:14px;background:var(--fill)}
.seg button{flex:1;min-width:0;min-height:38px;padding:6px 4px;font-size:14.5px;overflow:hidden;border-radius:10px;background:transparent;color:var(--dim);font-weight:650;white-space:nowrap;text-overflow:ellipsis;box-shadow:none}
.seg button.sel{background:var(--card);color:var(--text);box-shadow:0 1px 2px rgba(0,0,0,.1),0 4px 12px -4px rgba(0,0,0,.18)}
@media (prefers-color-scheme:dark){.seg button.sel{background:var(--fill2)}}
.kv{display:grid;grid-template-columns:auto minmax(0,1fr);gap:9px 16px;font-size:15px}.kv span{min-width:0;overflow-wrap:anywhere}.kv span:nth-child(odd){color:var(--dim)}
.ok{color:var(--ok)}.bad{color:var(--bad)}
input[type=range]{height:30px;min-height:30px;padding:0;border:0;background:transparent;box-shadow:none}
input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:99px;background:var(--fill2)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:26px;height:26px;margin-top:-10px;border-radius:50%;background:#fff;box-shadow:0 2px 8px rgba(0,0,0,.3),0 0 0 4px var(--acc)}
input[type=range]::-moz-range-track{height:6px;border-radius:99px;background:var(--fill2)}
input[type=range]::-moz-range-thumb{width:22px;height:22px;border:4px solid var(--acc);border-radius:50%;background:#fff}
nav{position:fixed;right:12px;bottom:calc(10px + env(safe-area-inset-bottom));left:12px;z-index:6;display:flex;max-width:560px;margin:0 auto;padding:6px;border-radius:28px;background:var(--glass);-webkit-backdrop-filter:saturate(180%) blur(22px);backdrop-filter:saturate(180%) blur(22px);box-shadow:0 0 0 1px var(--line),0 14px 34px -12px rgba(0,0,0,.4)}
nav button{display:flex;flex:1;flex-direction:column;align-items:center;justify-content:center;gap:3px;min-width:0;min-height:56px;padding:6px 2px;border-radius:22px;background:none;color:var(--dim);font-size:11px;font-weight:650;box-shadow:none}
nav svg{width:24px;height:24px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}
nav button.sel{background:rgba(236,100,54,.14);color:var(--acc)}
.tab{display:none}.tab.sel{display:block;animation:tin .28s ease}
@keyframes tin{from{opacity:.6;transform:translateY(8px)}}
#msg{position:fixed;bottom:calc(96px + env(safe-area-inset-bottom));left:50%;z-index:8;display:none;max-width:90vw;padding:12px 18px;border-radius:16px;background:var(--text);color:var(--bg);font-weight:650;transform:translateX(-50%);box-shadow:0 12px 30px -10px rgba(0,0,0,.45)}
#lock{position:fixed;inset:0;z-index:9;display:none;flex-direction:column;justify-content:center;max-width:420px;margin:0 auto;padding:24px;background:var(--bg)}
#lock h1{margin:0 0 4px;font-size:30px;letter-spacing:-.02em}
/* Apps mit Zeitkonto */
.app{border-top:5px solid var(--c)}
.ah{display:flex;align-items:center;gap:14px}
.ah i{display:grid;place-items:center;flex:none;width:52px;height:52px;border-radius:50%;background:var(--fill);font-size:27px;font-style:normal}
.ah .t{flex:1;min-width:0}.ah b{display:block;font-size:20px;font-weight:750;letter-spacing:-.01em}.ah small{display:block;color:var(--dim);font-size:14px}
.ah small.go{color:var(--ok);font-weight:650}
.bar{height:10px;margin:14px 0 4px;border-radius:99px;background:var(--fill2);overflow:hidden}
.bar span{display:block;height:100%;border-radius:99px;background:var(--c)}
.grid4{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;margin-top:12px}
.grid4 button{min-height:46px;padding:8px 4px;border-radius:14px;background:var(--fill);color:var(--text);font-size:14.5px;font-weight:700;box-shadow:none}
.grid4 button:first-child{background:var(--c);color:#fff}
@media (prefers-color-scheme:dark){.grid4 button:first-child{color:#111}}
/* Player */
#np{min-height:44px;color:var(--text);font-size:15px}#np b{display:block;font-size:18px;letter-spacing:-.01em}
.ctl{display:flex;align-items:center;justify-content:center;gap:16px;margin-top:16px}
.ctl button{display:grid;place-items:center;width:52px;height:52px;min-height:52px;padding:0;border-radius:50%;background:var(--fill);color:var(--text);box-shadow:none}
.ctl svg{width:22px;height:22px;fill:currentColor;stroke:currentColor;stroke-width:1.6;stroke-linejoin:round}
.ctl #mtog{width:70px;height:70px;background:linear-gradient(135deg,var(--acc2),var(--acc));color:#fff;box-shadow:0 10px 22px -10px rgba(236,100,54,.9)}
.ctl #mtog svg{width:28px;height:28px}
/* Akku */
.hero{display:flex;align-items:center;gap:18px}
.ring{position:relative;flex:none;width:92px;height:92px}
.ring svg{width:100%;height:100%;transform:rotate(-90deg)}
.ring b{position:absolute;inset:0;display:grid;place-items:center;font-size:23px;font-weight:780;letter-spacing:-.03em}
.hero .t{min-width:0}.hero .t b{display:block;font-size:19px;font-weight:720;letter-spacing:-.01em}.hero .t small{display:block;margin-top:2px;color:var(--dim);font-size:14px}
@media (min-width:760px){main{padding-top:24px}}
</style></head><body>
<header><h1><span id="name">Pixeluhr</span><span class="badge" id="nightb" hidden>Schlafenszeit</span></h1><div id="stat"><span class="chip">verbinde …</span></div></header>
<main>
<div class="tab sel" id="t-home">
 <section id="nopin" hidden><p class="warnbox">Noch keine Eltern-PIN: Jeder kann die Apps auf der Uhr freischalten und diese Seite öffnen. Unter „Regeln“ eine PIN festlegen.</p></section>
 <div id="apps"></div>
 <section><h2>Diese Woche</h2><svg id="week" viewBox="0 0 360 150" style="width:100%;height:auto"></svg><div class="note" id="weekleg"></div></section>
 <section><h2>Heute</h2><div class="kv" id="today"></div><button class="wide ghost" id="findgo">🔔 Uhr klingeln lassen</button></section>
</div>

<div class="tab" id="t-rules">
 <div id="rules"></div>
 <section><h2>Schlafenszeit</h2><p class="note">Von der Bettzeit bis zum Aufstehen ist alles gesperrt. Das Zifferblatt zeigt das Kind im Bett.</p>
  <div class="two"><div><label class="f" for="kw">Aufstehen</label><input type="time" id="kw"></div><div><label class="f" for="kb">Bettzeit</label><input type="time" id="kb"></div></div>
  <label class="f" for="kstory">Einschlafhören nach der Bettzeit</label>
  <select id="kstory"><option value="0">gar nicht – Hören endet zur Bettzeit</option><option value="15">15 Minuten</option><option value="30">30 Minuten</option><option value="45">45 Minuten</option><option value="60">1 Stunde</option></select>
  <p class="note" id="kstst"></p>
  <div class="row"><div class="t"><b>Nachtruhe</b><small>Tiefschlaf nach dem Einschlafhören bis zum Aufstehen – spart viel Akku, die Uhr ist dann nicht erreichbar</small></div><label class="sw"><input type="checkbox" id="pnight"><span></span></label></div></section>
 <section><h2>Lautstärke</h2><div class="row"><div class="t"><b>Höchstens <span id="kvv"></span> %</b></div></div><input type="range" id="kv" min="10" max="100" step="5"></section>
 <section><h2>Eltern-PIN</h2><p class="note" id="pinst"></p><label class="f" for="kpin">Neue PIN (4–8 Ziffern)</label><input id="kpin" inputmode="numeric" maxlength="8" autocomplete="off"><button class="wide" id="kpingo">PIN speichern</button>
  <p class="note">Mit der PIN schaltest du auf der Uhr Apps frei (antippen, PIN eingeben, Dauer wählen) und kommst ins Eltern-Menü (Zifferblatt lange drücken). Die Ziffern stehen jedes Mal woanders.</p></section>
 <section><h2>Balance</h2><p class="note" id="grec"></p><button class="wide ghost" id="greset">Rekorde zurücksetzen</button></section>
</div>

<div class="tab" id="t-music">
 <section><h2>Läuft auf der Uhr</h2><div id="np" class="note">Nichts</div>
  <div class="ctl"><button id="mstop" aria-label="Stopp"><svg viewBox="0 0 24 24"><rect x="7" y="7" width="10" height="10" rx="2"/></svg></button><button id="mprev" aria-label="Zurück"><svg viewBox="0 0 24 24"><path d="M18 6v12l-8.5-6zM6 6v12"/></svg></button><button id="mtog" aria-label="Wiedergabe/Pause"><svg viewBox="0 0 24 24"><path d="M7 5v14l7-7zM16 6v12M20 6v12"/></svg></button><button id="mnext" aria-label="Weiter"><svg viewBox="0 0 24 24"><path d="M6 6v12l8.5-6zM18 6v12"/></svg></button></div>
  <label class="f" for="mvol">Lautstärke <span id="mvolv"></span> %</label><input type="range" id="mvol" min="0" max="100" step="5">
  <label class="f" for="msleep">Sleep-Timer</label><select id="msleep"><option value="0">aus</option><option value="15">15 Min.</option><option value="30">30 Min.</option><option value="45">45 Min.</option><option value="60">60 Min.</option></select>
  <p class="note">Was du hier startest, läuft auch nachts und ohne Freigabe – die Zeit zählt trotzdem mit.</p></section>
 <section><h2>Freigegeben</h2><p class="note">Nur das gibt es auf der Uhr: Sender unter „Radio“, Sendungen und Folgen unter „Hörspiele“.</p><div id="krel"></div></section>
 <section><h2>Freigeben</h2>
  <div class="seg" id="msrc"><button data-v="radio" class="sel">📻 Radio</button><button data-v="ard">🎧 ARD Audiothek</button></div>
  <input id="mq" type="search" placeholder="z. B. Radio Teddy, Ohrenbär, Figarino" enterkeyhint="search" style="margin-top:10px">
  <div class="two"><button class="wide" id="mgo">Suchen</button><button class="wide ghost" id="mbrowse">Stöbern</button></div>
  <p class="note" id="mpath"></p><div id="mlist"></div></section>
 <section><h2>Zuletzt gehört</h2><div id="recent"></div></section>
</div>

<div class="tab" id="t-dev">
 <section><div class="hero" id="hero"></div><svg id="batchart" viewBox="0 0 360 140" style="width:100%;height:auto;margin-top:14px"></svg>
  <div class="note"><span style="color:var(--acc)">━</span> entlädt &nbsp; <span style="color:var(--ok)">━</span> lädt &nbsp; <span style="color:var(--dim)">┄</span> Uhr aus / Tiefschlaf</div>
  <div class="kv" id="batkv"></div>
  <label class="f" for="pmode">Sparmodus</label><select id="pmode"><option value="1">Automatisch ab 20 %</option><option value="2">Immer an</option><option value="0">Aus</option></select>
  <label class="f" for="pdeep">Tiefschlaf tagsüber</label><select id="pdeep"><option value="0">Nie – immer erreichbar</option><option value="10">Nach 10 Min. dunkel</option><option value="30">Nach 30 Min. dunkel</option><option value="60">Nach 1 Std. dunkel</option></select>
  <p class="note">Mit dunklem Display döst die Uhr sparsam und bleibt für diese Seite erreichbar. Im Tiefschlaf hält der Akku am längsten, dann weckt sie nur BOOT.</p></section>
 <section><h2>Name</h2><input id="nm" maxlength="16" aria-label="Name"><button class="wide" id="nmgo">Speichern</button></section>
 <section><h2>Display</h2><label class="f" for="dbr">Helligkeit <span id="dbrv"></span> %</label><input type="range" id="dbr" min="10" max="100" step="10">
  <label class="f" for="doff">Display aus nach</label><select id="doff"><option value="10">10 Sekunden</option><option value="15">15 Sekunden</option><option value="30">30 Sekunden</option><option value="60">1 Minute</option><option value="120">2 Minuten</option></select></section>
 <section><h2>WLAN</h2><div id="wifi"></div>
  <label class="f" for="ss">Netz hinzufügen</label><input id="ss" placeholder="Name (SSID)" autocapitalize="off">
  <label class="f" for="pw">Passwort</label><input id="pw" type="password" placeholder="leer = offenes Netz">
  <button class="wide" id="wgo">Hinzufügen und verbinden</button></section>
 <section><h2>Uhr</h2><div class="kv" id="info"></div></section>
 <section><h2>Firmware</h2><p class="note" id="fwst"></p>
  <label class="f" for="fwfile">Neue Firmware (firmware.bin aus dem Build)</label><input id="fwfile" type="file" accept=".bin">
  <button class="wide" id="fwgo">Aktualisieren</button><p class="note" id="fwprog"></p></section>
 <section><button class="wide ghost" id="rst">Uhr neu starten</button></section>
</div>
</main>
<nav id="nav"><button data-t="home" class="sel"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="8"/><path d="M12 8v4l2.5 1.5"/></svg>Heute</button><button data-t="rules"><svg viewBox="0 0 24 24"><path d="M6 3v18M18 3v18M6 8h4M14 16h4"/><circle cx="12" cy="8" r="2"/><circle cx="12" cy="16" r="2"/></svg>Regeln</button><button data-t="music"><svg viewBox="0 0 24 24"><path d="M9 18V5l11-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="17" cy="16" r="3"/></svg>Hören</button><button data-t="dev"><svg viewBox="0 0 24 24"><path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/></svg>Gerät</button></nav>
<div id="msg"></div>
<div id="lock"><h1>Eltern-PIN</h1><p class="note">Diese Uhr ist geschützt.</p><input id="lpin" type="password" inputmode="numeric" autocomplete="off"><button class="wide" id="lgo">Entsperren</button></div>
<script>
const $=id=>document.getElementById(id),val=id=>$(id).value.trim();
const EMO=['🎱','📻','🎧'],COL=['var(--game)','var(--radio)','var(--story)'],LIM=[0,10,15,20,30,45,60,90,120,180];
let S=null,pin='',mSrc='radio',mItems=[],mStack=[],mTitle='',mErr='',RC=[],lastTitle=null;
try{pin=localStorage.getItem('pin')||''}catch(e){}
function esc(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function toast(t){const m=$('msg');m.textContent=t;m.style.display='block';clearTimeout(toast.h);toast.h=setTimeout(()=>m.style.display='none',2500)}
async function api(p,b){const o={headers:{'X-Pin':pin}};if(b!==undefined){o.method='POST';o.body=JSON.stringify(b)}
 const r=await fetch('/api/'+p,o);if(r.status==401){$('lock').style.display='flex';throw new Error('pin')}
 $('lock').style.display='none';return r.json().catch(()=>({}))}
async function post(p,b,quiet){try{const j=await api(p,b);if(!quiet)toast(j.msg||'Gespeichert');await load()}catch(e){if(e.message!='pin')toast('Uhr nicht erreichbar')}}
const hm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0'),mins=v=>{const[a,b]=v.split(':');return+a*60+ +b};
const clock=t=>new Date(t*1000).toLocaleTimeString('de',{hour:'2-digit',minute:'2-digit'});
const left=s=>s<0?'':s>=3600?'noch '+Math.floor(s/3600)+' Std. '+(Math.ceil(s/60)%60?Math.ceil(s/60)%60+' Min.':''):'noch '+Math.max(1,Math.ceil(s/60))+' Min.';
function row(t,s,right){return`<div class="row"><div class="t"><b>${esc(t)}</b>${s?`<small>${esc(s)}</small>`:''}</div>${right||''}</div>`}
function btn(label,fn,cls){return`<button class="ghost ${cls||''}" onclick='${fn}'>${label}</button>`}
const J=o=>esc(JSON.stringify(o));
const focused=id=>$(id).contains(document.activeElement);
// ------------------------------------------------ Heute
function statusText(a){const k=S.kids;
 if(a.status==3)return`Schlafenszeit – ab ${hm(k.wake)} wieder`;
 if(a.status==2)return'Für heute aufgebraucht';
 if(a.status==1)return'Gesperrt – auf der Uhr mit PIN oder hier freigeben';
 if(a.rule.free)return'Frei ohne Freigabe'+(a.left>=0?' · '+left(a.left):'');
 return`Frei bis ${clock(a.until)} · ${left(a.left)}`}
function appCard(a,i){const lim=a.limit,pct=lim?Math.min(100,a.used*100/lim):0;
 return`<section class="app" style="--c:${COL[i]}"><div class="ah"><i>${EMO[i]}</i><div class="t"><b>${esc(a.name)}</b><small class="${a.status==0?'go':''}">${esc(statusText(a))}</small></div></div>
 <div class="bar"><span style="width:${lim?pct:0}%"></span></div><div class="note">${a.used} Min. heute${lim?' von '+lim+(a.extra?' (davon '+a.extra+' extra)':''):' · ohne Tageslimit'}</div>
 <div class="grid4"><button onclick="unlock(${i},15)">15 Min.</button><button onclick="unlock(${i},30)">30 Min.</button><button onclick="unlock(${i},60)">1 Std.</button><button onclick="unlock(${i},-1)">bis Bett</button></div>
 ${a.until?`<button class="wide ghost" onclick="post('lock',{app:${i}})">🔒 Jetzt sperren</button>`:''}</section>`}
function unlock(i,m){post('unlock',{app:i,min:m},true).then(()=>toast(S.apps[i].name+' freigegeben'))}
function week(){const el=$('week'),D=['So','Mo','Di','Mi','Do','Fr','Sa'],now=new Date(),X0=26,X1=356,Y0=10,Y1=118;
 const days=[];for(let k=7;k>=0;k--){const d=new Date(now-k*864e5);days.push({l:k?D[d.getDay()]:'heute',v:S.apps.map(a=>k?a.hist[k-1]:a.used)})}
 const mx=Math.max(30,...days.flatMap(d=>d.v)),gw=(X1-X0)/days.length,bw=(gw-10)/3,Y=v=>Y1-v/mx*(Y1-Y0);let g='';
 for(const p of [0,.5,1]){const v=Math.round(mx*p);g+=`<line x1="${X0}" x2="${X1}" y1="${Y(v)}" y2="${Y(v)}" stroke="var(--line)"/><text x="${X0-4}" y="${Y(v)+3}" font-size="9" text-anchor="end" fill="var(--dim)">${v}</text>`}
 days.forEach((d,i)=>{d.v.forEach((v,a)=>{const x=X0+5+i*gw+a*bw,h=Y1-Y(v);if(v)g+=`<rect x="${x.toFixed(1)}" y="${Y(v).toFixed(1)}" width="${(bw-1).toFixed(1)}" height="${h.toFixed(1)}" rx="2" fill="${COL[a]}"/>`});
  g+=`<text x="${(X0+i*gw+gw/2).toFixed(1)}" y="${Y1+14}" font-size="10" text-anchor="middle" fill="var(--dim)">${d.l}</text>`});
 el.innerHTML=g;$('weekleg').innerHTML=S.apps.map((a,i)=>`<span style="color:${COL[i]}">■</span> ${esc(a.name)}`).join(' &nbsp; ')+' · Minuten pro Tag'}
// ------------------------------------------------ Regeln
function limOpts(v){return LIM.map(m=>`<option value="${m}"${m==v?' selected':''}>${m?m+' Minuten':'ohne Grenze'}</option>`).join('')}
function rules(){if(focused('rules'))return;
 $('rules').innerHTML=S.apps.map((a,i)=>`<section class="app" style="--c:${COL[i]}"><h2>${EMO[i]} ${esc(a.name)}</h2>
 <div class="two"><div><label class="f">Mo–Fr pro Tag</label><select onchange="rule(${i},{weekday:+this.value})">${limOpts(a.rule.weekday)}</select></div>
 <div><label class="f">Sa &amp; So pro Tag</label><select onchange="rule(${i},{weekend:+this.value})">${limOpts(a.rule.weekend)}</select></div></div>
 <div class="row"><div class="t"><b>Ohne Freigabe</b><small>Nur das Tageslimit gilt, keine PIN nötig</small></div><label class="sw"><input type="checkbox" ${a.rule.free?'checked':''} onchange="rule(${i},{free:this.checked})"><span></span></label></div></section>`).join('')+
 '<p class="note" style="margin:-4px 6px 14px">Gezählt wird nur echte Nutzung: Spielen bzw. Hören. Zwei Minuten vorher kommt ein Hinweis, dann wird die Stufe zu Ende gespielt bzw. die Musik sanft ausgeblendet. Freigaben über das Limit hinaus (auf der Uhr oder unter „Heute“) gelten als Extra-Minuten für heute.</p>'}
function rule(i,p){post('rules',{app:i,...p},true).then(()=>toast('Gespeichert'))}
// ------------------------------------------------ Hören
function released(p){return S.released.some(r=>r.path==p)}
const relOk=id=>/^(radio:|ard:set:|ard:a:)/.test(id);
function relBtn(path,title,sub){const on=released(path);return btn(on?'🧸 ✓':'🧸 +',`rel(${J({path,title,sub})},${!on})`,on?'on':'')}
function rel(it,on){post('release',{...it,on},true).then(()=>{toast(on?'Für die Uhr freigegeben':'Freigabe entfernt');mRender()})}
function mOpen(path,title,push){$('mlist').innerHTML='<p class="note">Lade …</p>';
 api('music/list?path='+encodeURIComponent(path)).then(j=>{if(push)mStack.push({path,title});mTitle=title||j.title||'';mErr=j.err||'';mItems=j.items||[];mRender()}).catch(()=>{$('mlist').innerHTML=''})}
function mRender(){if(!S)return;const cur=mStack[mStack.length-1];
 $('mpath').innerHTML=mStack.length?(mStack.length>1?`<a href="#" onclick="mBack();return false">← zurück</a> · `:'')+esc(mTitle)+(cur&&relOk(cur.path)?' '+relBtn(cur.path,mTitle,'Sendung'):''):'';
 $('mlist').innerHTML=!mStack.length?'':mItems.map((it,i)=>row(it.title,it.sub,'<span class="bt">'+(relOk(it.id)?relBtn(it.id,it.title,it.sub):'')+
  (it.kind==1?btn('Öffnen',`mOpen(${J(it.id)},${J(it.title)},true)`):btn('▶',`mPlay(${i})`))+'</span>')).join('')||'<p class="note">'+esc(mErr||'Nichts gefunden')+'</p>'}
function mBack(){mStack.pop();const t=mStack.pop();if(t)mOpen(t.path,t.title,true)}
function mPlay(i){post('music/play',{items:mItems,index:i},true).then(()=>toast('Läuft auf der Uhr'))}
function mSearch(){const q=val('mq');if(!q)return;mStack=[];mOpen((mSrc=='radio'?'rlsearch:':'ardsearch:')+q,'',true)}
function relPlay(i){const r=S.released[i];if(r.path.startsWith('ard:set:'))return mOpen(r.path,r.title,true),document.getElementById('mpath').scrollIntoView({behavior:'smooth'});
 post('music/play',{items:[{id:r.path,title:r.title,sub:r.sub,kind:0}],index:0},true).then(()=>toast('Läuft auf der Uhr'))}
async function loadRecent(){try{RC=(await api('music/recent')).items||[];$('recent').innerHTML=RC.length?RC.map((r,i)=>row(r.title,[r.sub,r.pos?'bei '+Math.floor(r.pos/60)+':'+String(r.pos%60).padStart(2,'0'):''].filter(Boolean).join(' · '),
 '<span class="bt">'+btn('▶',`recentDo(${i},"play")`)+btn('✕',`recentDo(${i},"remove")`)+'</span>')).join(''):'<p class="note">Noch nichts gehört.</p>'}catch(e){}}
async function recentDo(i,what){const r=RC[i];if(!r)return;await post('music/recent',{[what]:r.id},true);toast(what=='play'?'Läuft auf der Uhr':'Entfernt');loadRecent()}
// ------------------------------------------------ Darstellung
function render(){const s=S,k=s.kids;
 $('name').textContent=s.name;$('nightb').hidden=!s.night;$('nopin').hidden=k.pin;
 const bc=s.battery<0||s.charging?'g':s.battery<=20?'r':s.battery<=40?'y':'g';
 $('stat').innerHTML=`<span class="chip ${bc}">${s.battery<0?'USB':s.battery+' %'}${s.charging?' ⚡':''}</span><span class="chip">${s.ssid?esc(s.ssid):'offline'}</span>`+(s.weather?`<span class="chip">${s.weather.temp}° ${esc(s.weather.text)}</span>`:'');
 $('apps').innerHTML=s.apps.map(appCard).join('');week();
 $('today').innerHTML=(s.weather?`<span>Wetter</span><span>${s.weather.temp} °C, ${esc(s.weather.text)}</span><span>Anziehen</span><span>${esc(s.weather.outfit)}</span>`:'')+
  `<span>Bettzeit</span><span>${hm(k.bed)}${k.storyMin?' (+'+k.storyMin+' Min. Einschlafhören)':''}</span><span>Aufstehen</span><span>${hm(k.wake)}</span>`;
 rules();
 if(!focused('kw'))$('kw').value=hm(k.wake);if(!focused('kb'))$('kb').value=hm(k.bed);if(!focused('kstory'))$('kstory').value=k.storyMin;
 $('kstst').textContent=k.storyMin?`Ab ${hm(k.bed)} darf noch ${k.storyMin} Minuten gehört werden, dann wird sanft ausgeblendet.`:`Ab ${hm(k.bed)} wird ausgeblendet.`;
 $('pnight').checked=s.power.night;if(!focused('kv')){$('kv').value=k.maxVol;$('kvv').textContent=k.maxVol}
 $('pinst').innerHTML=k.pin?'<span class="ok">PIN ist gesetzt</span> – diese Seite und die Uhr fragen danach.':'<span class="bad">Noch keine PIN.</span>';
 $('grec').innerHTML=`Rekorde – Ins Ziel: ${s.game.level?'<b>Stufe '+s.game.level+'</b>':'noch keiner'} · Alle Kugeln: ${s.game.ms?'<b>'+(s.game.ms/1000).toFixed(1).replace('.',',')+' s</b>':'noch keiner'}`;
 const mu=s.music;$('np').innerHTML=mu.active?`<b>${mu.playing?'▶':'⏸'} ${esc(mu.title)}</b>${esc(mu.artist)}${mu.dur?' · '+Math.floor(mu.pos/60)+':'+String(Math.floor(mu.pos%60)).padStart(2,'0')+' / '+Math.floor(mu.dur/60)+':'+String(mu.dur%60).padStart(2,'0'):''}${mu.msg?'<br><span class="bad">'+esc(mu.msg)+'</span>':''}`:'Gerade läuft nichts.';
 if(!focused('mvol')){$('mvol').value=mu.volume;$('mvolv').textContent=mu.volume}
 if(!focused('msleep'))$('msleep').value=[0,15,30,45,60].reduce((a,v)=>Math.abs(v-mu.sleepMin)<Math.abs(a-mu.sleepMin)?v:a,0);
 $('krel').innerHTML=s.released.map((r,i)=>row((r.path.startsWith('radio:')?'📻 ':'🎧 ')+r.title,r.sub,'<span class="bt">'+btn(r.path.startsWith('ard:set:')?'Öffnen':'▶',`relPlay(${i})`)+btn('✕',`rel(${J(r)},false)`)+'</span>')).join('')||'<p class="note">Noch nichts – unten suchen oder stöbern.</p>';
 mRender();
 const pw=s.power,rm=pw.remainMin,ring=s.battery<0?100:s.battery,rc=bc=='r'?'var(--bad)':bc=='y'?'var(--warn)':'var(--ok)';
 $('hero').innerHTML=`<div class="ring"><svg viewBox="0 0 100 100"><circle cx="50" cy="50" r="42" fill="none" stroke="var(--fill2)" stroke-width="10"/><circle cx="50" cy="50" r="42" fill="none" stroke="${rc}" stroke-width="10" stroke-linecap="round" stroke-dasharray="${(ring*2.639).toFixed(1)} 264"/></svg><b>${s.battery<0?'USB':s.battery+'%'}</b></div>
  <div class="t"><b>${s.charging?'Lädt':rm>=0?'noch ca. '+(rm>=60?Math.floor(rm/60)+' h '+String(rm%60).padStart(2,'0')+' min':rm+' min'):'Akku'}</b><small>${esc(pw.sleep||'')}</small></div>`;
 $('batkv').innerHTML=`<span>Spannung</span><span>${pw.mv?(pw.mv/1000).toFixed(2)+' V':'–'}</span><span>Verbrauch</span><span>${pw.drain>0?pw.drain.toFixed(1)+' %/h':'–'}</span><span>Sparmodus</span><span>${pw.saving?'an':'aus'}</span>`+
  (pw.tempBat!=null?`<span>Temperatur</span><span>Ladechip ${pw.tempBat.toFixed(0)} °C${pw.tempCpu!=null?' · Prozessor '+pw.tempCpu.toFixed(0)+' °C':''}</span>`:'');
 if(!focused('pmode'))$('pmode').value=pw.mode;if(!focused('pdeep'))$('pdeep').value=pw.deep;batChart(pw.history);
 if(!focused('nm'))$('nm').value=s.name;
 if(!focused('dbr')){$('dbr').value=s.display.brightness;$('dbrv').textContent=s.display.brightness}
 if(!focused('doff'))$('doff').value=[10,15,30,60,120].includes(s.display.screenOff)?s.display.screenOff:30;
 $('wifi').innerHTML=s.known.map(n=>row(n,n===s.ssid?'verbunden':'',btn('Vergessen',`post("wifi/forget",{ssid:${J(n)}})`))).join('')||'<p class="note">Keine Netze gespeichert</p>';
 $('info').innerHTML=`<span>Adresse</span><span>${esc(s.ip)} · ${esc(s.host)}.local</span><span>WLAN</span><span>${esc(s.ssid||'–')}${s.rssi?' ('+s.rssi+' dBm)':''}</span><span>Version</span><span>${esc(s.version)}</span>`;
 $('fwst').textContent=`Version ${s.version} · ${s.board} · gebaut ${s.built}`}
async function load(){try{S=await api('state');render();if(S.music.title!==lastTitle){lastTitle=S.music.title;loadRecent()}}catch(e){if(e.message!='pin')$('stat').innerHTML='<span class="chip r">Uhr nicht erreichbar</span>'}}
// Akku-Verlauf: Prozent über Uhrzeit; Entladen orange, Laden grün, Lücken (Uhr aus, Tiefschlaf) gestrichelt
function batChart(hist){const h=hist.slice().sort((a,b)=>b[0]-a[0]),el=$('batchart');
 if(h.length<2){el.innerHTML='<text x="0" y="60" font-size="12" fill="var(--dim)">Verlauf: alle 2 Minuten ein Messpunkt</text>';return}
 const span=Math.max(60,Math.ceil(h[0][0]/60)*60),X0=30,X1=350,Y0=8,Y1=118,X=m=>(X1-m/span*(X1-X0)).toFixed(1),Y=p=>(Y1-p/100*(Y1-Y0)).toFixed(1);let g='';
 for(const p of [0,25,50,75,100])g+=`<line x1="${X0}" x2="${X1}" y1="${Y(p)}" y2="${Y(p)}" stroke="var(--line)"/><text x="${X0-5}" y="${+Y(p)+3}" font-size="9" text-anchor="end" fill="var(--dim)">${p} %</text>`;
 const now=Date.now(),step=span<=120?30:span<=240?60:120;
 for(let m=0;m<=span;m++){const t=new Date(now-m*60000);if((t.getHours()*60+t.getMinutes())%step)continue;
  g+=`<line x1="${X(m)}" x2="${X(m)}" y1="${Y0}" y2="${Y1}" stroke="var(--line)" stroke-dasharray="2 3"/><text x="${X(m)}" y="${Y1+14}" font-size="9" text-anchor="middle" fill="var(--dim)">${String(t.getHours()).padStart(2,'0')}:${String(t.getMinutes()).padStart(2,'0')}</text>`}
 for(let i=1;i<h.length;i++){const a=h[i-1],b=h[i],gap=a[0]-b[0]>6;
  g+=`<line x1="${X(a[0])}" y1="${Y(a[1])}" x2="${X(b[0])}" y2="${Y(b[1])}" stroke="${gap?'var(--dim)':b[3]?'var(--ok)':'var(--acc)'}" stroke-width="${gap?1.2:2.5}"${gap?' stroke-dasharray="3 3"':''} stroke-linecap="round"/>`}
 el.innerHTML=g}
// ------------------------------------------------ Bedienung
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('sel',x==b));
 document.querySelectorAll('.tab').forEach(t=>t.classList.toggle('sel',t.id=='t-'+b.dataset.t));scrollTo(0,0)});
const kids=p=>post('kids',p,true);
$('kw').onchange=()=>kids({wake:mins($('kw').value)});$('kb').onchange=()=>kids({bed:mins($('kb').value)});
$('kstory').onchange=e=>kids({storyMin:+e.target.value});$('pnight').onchange=e=>post('power',{night:e.target.checked});
$('kv').oninput=e=>$('kvv').textContent=e.target.value;$('kv').onchange=e=>kids({maxVol:+e.target.value});
$('kpingo').onclick=()=>{const p=val('kpin');if(!/^\d{4,8}$/.test(p))return toast('4–8 Ziffern');
 api('kids/pin',{pin:p}).then(j=>{pin=p;try{localStorage.setItem('pin',p)}catch(e){}$('kpin').value='';toast(j.msg);load()})};
$('lgo').onclick=()=>{pin=val('lpin');try{localStorage.setItem('pin',pin)}catch(e){}load()};
$('greset').onclick=()=>confirm('Balance-Rekorde löschen?')&&post('game/reset',{});
$('findgo').onclick=()=>post('find',{on:true});
$('msrc').onclick=e=>{const b=e.target.closest('button');if(!b)return;mSrc=b.dataset.v;[...$('msrc').children].forEach(x=>x.classList.toggle('sel',x==b));
 $('mq').placeholder=mSrc=='radio'?'z. B. Radio Teddy, Die Maus, KiRaKa':'z. B. Ohrenbär, Figarino, Mikado';if(val('mq'))mSearch()};
$('mgo').onclick=mSearch;$('mq').onkeydown=e=>{if(e.key=='Enter')mSearch()};
$('mbrowse').onclick=()=>{mStack=[];mOpen(mSrc=='radio'?'rl:home':'ard:home','',true)};
const ctl=c=>post('music/ctl',c,true);
$('mprev').onclick=()=>ctl({cmd:'prev'});$('mtog').onclick=()=>ctl({cmd:'toggle'});$('mnext').onclick=()=>ctl({cmd:'next'});$('mstop').onclick=()=>ctl({cmd:'stop'});
$('mvol').oninput=e=>$('mvolv').textContent=e.target.value;$('mvol').onchange=e=>ctl({volume:+e.target.value});
$('msleep').onchange=e=>ctl({sleep:+e.target.value});
$('pmode').onchange=e=>post('power',{mode:+e.target.value});$('pdeep').onchange=e=>post('power',{deep:+e.target.value});
$('dbr').oninput=e=>$('dbrv').textContent=e.target.value;$('dbr').onchange=e=>post('display',{brightness:+e.target.value},true);
$('doff').onchange=e=>post('display',{screenOff:+e.target.value});
$('nmgo').onclick=()=>post('name',{name:val('nm')});
$('wgo').onclick=()=>post('wifi',{ssid:val('ss'),pass:val('pw')});
$('fwgo').onclick=()=>{const f=$('fwfile').files[0];if(!f)return toast('Erst eine Datei wählen');
 const fd=new FormData();fd.append('file',f,f.name);const x=new XMLHttpRequest();x.open('POST','/api/update');x.setRequestHeader('X-Pin',pin);
 x.upload.onprogress=e=>{if(e.lengthComputable)$('fwprog').textContent='Übertrage … '+Math.round(e.loaded*100/e.total)+' %'};
 x.onload=()=>{let m='';try{m=JSON.parse(x.responseText).msg}catch(e){}$('fwprog').textContent=m||('Fehler '+x.status);if(x.status==200)setTimeout(load,15000)};
 x.onerror=()=>{$('fwprog').textContent='Verbindung abgebrochen'};x.send(fd)};
$('rst').onclick=()=>confirm('Uhr neu starten?')&&post('restart',{});
// Reiter direkt öffnen: …/#regeln, #hoeren, #geraet
const HASH={heute:'home',regeln:'rules',hoeren:'music',geraet:'dev'};
function fromHash(){const t=HASH[location.hash.slice(1)];const b=t&&document.querySelector(`nav button[data-t=${t}]`);if(b)b.click()}
window.addEventListener('hashchange',fromHash);fromHash();
load();setInterval(()=>{if(!document.hidden)load()},10000);
</script></body></html>)HTML";

static void json(int code, JsonDocument& d) {
  String out;
  serializeJson(d, out);
  srv->send(code, "application/json", out);
}

static void msg(const char* m, int code = 200) {
  JsonDocument d;
  d["msg"] = m;
  json(code, d);
}

static bool body(JsonDocument& d) { return !deserializeJson(d, srv->arg("plain")); }

// Eltern-PIN: ist eine gesetzt, braucht jede Anfrage an /api/ den Kopf „X-Pin“
static bool pinOk() { return !kids::hasPin() || kids::pinOk(srv->header("X-Pin").c_str()); }

static bool authed() {
  if (pinOk()) return true;
  delay(600);  // Raten bremsen
  msg("PIN nötig", 401);
  return false;
}

static bool appArg(JsonDocument& d, quota::App& a) {
  int i = d["app"] | -1;
  if (i < 0 || i >= quota::APPS) return false;
  a = (quota::App)i;
  return true;
}

// ------------------------------------------------------------------ Update per WLAN
// Die hochgeladene Firmware muss die Kennung dieses Boards enthalten (TF_BOARD_TAG, siehe main.cpp) –
// so landet die Firmware der runden Uhr nicht versehentlich auf der eckigen und umgekehrt.
static String otaErr;
static bool otaTag = false;
static char otaTail[48];
static size_t otaTailLen = 0;

static void otaScan(const uint8_t* p, size_t n) {
  size_t tl = strlen(TF_BOARD_TAG);
  if (otaTag || !n) return;
  // über die Grenze zweier Stücke hinweg: Ende des letzten + Anfang des neuen
  uint8_t join[96];
  size_t k = min(n, tl - 1);
  memcpy(join, otaTail, otaTailLen);
  memcpy(join + otaTailLen, p, k);
  if (memmem(join, otaTailLen + k, TF_BOARD_TAG, tl) || memmem(p, n, TF_BOARD_TAG, tl)) { otaTag = true; return; }
  otaTailLen = min(n, tl - 1);
  memcpy(otaTail, p + n - otaTailLen, otaTailLen);
}

static void otaPrep() {  // in der Hauptschleife: Wiedergabe vor dem Schreiben anhalten
  music::stop();
  srv->send(200, "text/plain", "");
}

static esp_err_t respond(httpd_req_t* r);
static bool runInLoop(void (*fn)(), uint32_t ms);

// Firmware kommt als multipart/form-data mit genau einer Datei; sie wird direkt in den HTTPS-Task geschrieben
static esp_err_t otaReceive(httpd_req_t* r) {
  static char* buf = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  otaErr = "";
  otaTag = false;
  otaTailLen = 0;
  char ct[128] = "";
  httpd_req_get_hdr_value_str(r, "Content-Type", ct, sizeof(ct));
  const char* bnd = strstr(ct, "boundary=");
  if (!pinOk()) otaErr = "PIN nötig";
  else if (!bnd) otaErr = "Ungültige Anfrage";
  else runInLoop(otaPrep, 5000);
  size_t left = r->content_len, have = 0, data = 0;
  char* hdrEnd = nullptr;
  while (!otaErr.length() && !hdrEnd && left && have < 1024) {  // Kopf des Datei-Teils
    int n = httpd_req_recv(r, buf + have, min(left, (size_t)1024 - have));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) { otaErr = "Übertragung abgebrochen"; break; }
    have += n;
    left -= n;
    hdrEnd = (char*)memmem(buf, have, "\r\n\r\n", 4);
  }
  if (!otaErr.length()) {
    if (!hdrEnd) otaErr = "Ungültige Anfrage";
    else {
      // hinter der Datei folgt nur noch „\r\n--<boundary>--\r\n“
      size_t head = hdrEnd + 4 - buf, tail = strlen(bnd + 9) + 8;
      data = r->content_len > head + tail ? r->content_len - head - tail : 0;
      Serial.printf("Update: %u Bytes werden empfangen\n", (unsigned)data);
      if (!data || !Update.begin(data)) otaErr = data ? Update.errorString() : "Leere Datei";
      size_t k = min(have - head, data);
      otaScan((uint8_t*)buf + head, k);
      if (!otaErr.length() && Update.write((uint8_t*)buf + head, k) != k) otaErr = Update.errorString();
      data -= k;
    }
  }
  while (!otaErr.length() && left) {
    int n = httpd_req_recv(r, buf, min(left, (size_t)4096));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) { otaErr = "Übertragung abgebrochen"; break; }
    left -= n;
    size_t k = min((size_t)n, data);
    if (!k) continue;
    otaScan((uint8_t*)buf, k);
    if (Update.write((uint8_t*)buf, k) != k) otaErr = Update.errorString();
    data -= k;
  }
  if (!otaErr.length() && !otaTag) otaErr = "Diese Firmware ist für ein anderes Board";
  if (!otaErr.length() && !Update.end(true)) otaErr = Update.errorString();
  if (otaErr.length()) {
    if (Update.isRunning()) Update.abort();
    Serial.printf("Update: abgelehnt (%s)\n", otaErr.c_str());
    msg(otaErr.c_str(), otaErr == "PIN nötig" ? 401 : 400);
    return respond(r);
  }
  Serial.println("Update: installiert, starte neu");
  msg("Update installiert – die Uhr startet neu");
  respond(r);
  delay(600);
  ESP.restart();
  return ESP_OK;
}

// ------------------------------------------------------------------ Zustand

static void state() {
  if (!authed()) return;
  JsonDocument d;
  d["name"] = cfg.name;
  d["host"] = netHost();
  d["ip"] = netIp();
  d["version"] = FW_VERSION;
  d["board"] = TF_BOARD_TAG + 8;  // „TFBOARD=…;“ → Boardname
  d["built"] = __DATE__ " " __TIME__;
  d["battery"] = hwBattery();
  d["charging"] = hwCharging();
  d["ssid"] = netOnline() ? netSsid() : "";
  d["rssi"] = netOnline() ? netRssi() : 0;
  d["night"] = kids::night();
  JsonArray k = d["known"].to<JsonArray>();
  for (int i = 0; i < knownCount; i++) k.add(known[i].ssid);

  const kids::Cfg& kc = kids::cfg();
  JsonObject ko = d["kids"].to<JsonObject>();
  ko["wake"] = kc.wake;
  ko["bed"] = kc.bed;
  ko["storyMin"] = kc.storyMin;
  ko["maxVol"] = kc.maxVol;
  ko["pin"] = kids::hasPin();  // nie die PIN selbst

  JsonArray apps = d["apps"].to<JsonArray>();
  for (int i = 0; i < quota::APPS; i++) {
    quota::App a = (quota::App)i;
    JsonObject o = apps.add<JsonObject>();
    o["name"] = quota::name(a);
    o["status"] = (int)quota::status(a);
    o["until"] = quota::until(a);
    o["left"] = quota::leftS(a);
    o["used"] = quota::usedMin(a);
    o["limit"] = quota::limitMin(a);
    o["extra"] = quota::extraMin(a);
    quota::Rule r = quota::rule(a);
    JsonObject ro = o["rule"].to<JsonObject>();
    ro["weekday"] = r.weekday;
    ro["weekend"] = r.weekend;
    ro["free"] = r.free;
    JsonArray h = o["hist"].to<JsonArray>();
    for (int day = 1; day <= quota::DAYS; day++) h.add(quota::historyMin(a, day));
  }

  weather::Now w = weather::now();
  if (w.ok) {
    JsonObject wo = d["weather"].to<JsonObject>();
    wo["temp"] = (int)lroundf(w.temp);
    wo["text"] = weather::text(w.code);
    faceart::Scene s{};
    s.weather = true;
    s.sky = faceart::skyOf(w.code);
    s.temp = (int)lroundf(w.temp);
    wo["outfit"] = faceart::outfitText(faceart::outfit(s));
  }

  music::State ms = music::state();
  JsonObject mu = d["music"].to<JsonObject>();
  mu["active"] = ms.active;
  mu["playing"] = ms.playing;
  mu["title"] = ms.title;
  mu["artist"] = ms.artist;
  mu["pos"] = (int)ms.pos;
  mu["dur"] = ms.dur;
  mu["msg"] = ms.msg;
  mu["volume"] = cfg.volume;
  mu["sleepMin"] = (music::sleepLeftS() + 59) / 60;
  static music::Item* rel = (music::Item*)heap_caps_malloc(sizeof(music::Item) * kids::RELEASE_MAX, MALLOC_CAP_SPIRAM);
  JsonArray ra = d["released"].to<JsonArray>();
  int n = kids::list(rel, kids::RELEASE_MAX);
  for (int i = 0; i < n; i++) {
    JsonObject f = ra.add<JsonObject>();
    f["path"] = rel[i].id;
    f["title"] = rel[i].title;
    f["sub"] = rel[i].sub;
  }

  int gl;
  uint32_t gms;
  gameRecords(gl, gms);
  JsonObject gm = d["game"].to<JsonObject>();
  gm["level"] = gl;
  gm["ms"] = gms;

  JsonObject dp = d["display"].to<JsonObject>();
  dp["brightness"] = cfg.brightness;
  dp["screenOff"] = cfg.screenOff;
  JsonObject pw = d["power"].to<JsonObject>();
  pw["mode"] = (int)pwr::mode();
  pw["saving"] = pwr::saving();
  pw["mv"] = hwBatteryMv();
  pw["remainMin"] = pwr::remainingMin();
  pw["sleep"] = pwr::sleepText();
  pw["deep"] = pwr::deepSleepSetting();
  pw["night"] = pwr::nightRestOn();
  pw["drain"] = pwr::drainPerHour();
  if (!isnan(hwTempBattery())) pw["tempBat"] = roundf(hwTempBattery() * 10) / 10;
  if (!isnan(hwTempCpu())) pw["tempCpu"] = roundf(hwTempCpu() * 10) / 10;
  JsonDocument hd;
  deserializeJson(hd, pwr::historyJson());
  pw["history"] = hd;
  json(200, d);
}

// ------------------------------------------------------------------ Übergabe HTTPS-Task ↔ Hauptschleife

static const char* statusText(int c) {
  switch (c) {
    case 200: return "200 OK";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 404: return "404 Not Found";
    case 413: return "413 Payload Too Large";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
  }
}

static esp_err_t respond(httpd_req_t* r) {
  httpd_resp_set_status(r, statusText(req.code));
  httpd_resp_set_type(r, req.type.c_str());
  if (req.disp.length()) httpd_resp_set_hdr(r, "Content-Disposition", req.disp.c_str());
  if (req.outP) return httpd_resp_send(r, req.outP, HTTPD_RESP_USE_STRLEN);
  return httpd_resp_send(r, req.out.c_str(), req.out.length());
}

// fn in der Hauptschleife ausführen und auf ihre Antwort warten (die Hauptschleife ruft poll() alle paar ms)
static bool runInLoop(void (*fn)(), uint32_t ms) {
  hwDozeKick();  // auch wenn noch eine Anfrage wartet: die Hauptschleife soll sie abarbeiten
  for (uint32_t t = millis(); req.state && millis() - t < ms;) delay(5);  // die vorige Anfrage läuft noch
  if (req.state) return false;
  xSemaphoreTake(req.ready, 0);
  req.code = 0;
  req.disp = "";
  req.fn = fn;
  req.state = 1;
  hwDozeKick();  // dösende Hauptschleife sofort wecken, sonst wartet die Anfrage bis zu 0,5 s
  return xSemaphoreTake(req.ready, pdMS_TO_TICKS(ms)) == pdTRUE;
}

esp_err_t handle(httpd_req_t* r) {
  const char* q = strchr(r->uri, '?');
  String path = q ? String(r->uri).substring(0, q - r->uri) : String(r->uri);
  bool post = r->method == HTTP_POST;
  const Route* rt = nullptr;
  for (int i = 0; i < nRoutes && !rt; i++)
    if (routes[i].post == post && path == routes[i].path) rt = &routes[i];
  if (!rt) return httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, "Nicht gefunden");
  for (uint32_t t = millis(); req.state && millis() - t < 30000;) delay(5);
  if (req.state) return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Die Uhr ist beschäftigt");
  char pin[24] = "";
  httpd_req_get_hdr_value_str(r, "X-Pin", pin, sizeof(pin));
  req.uri = path;
  req.query = q ? q + 1 : "";
  req.pin = pin;
  req.body = "";
  if (post && path == "/api/update") return otaReceive(r);
  if (r->content_len > 32768) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Zu groß");
  char b[512];
  for (size_t left = r->content_len; left;) {
    int n = httpd_req_recv(r, b, min(left, sizeof(b)));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return ESP_FAIL;
    req.body.concat(b, n);
    left -= n;
  }
  if (!runInLoop(rt->fn, 30000)) return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Keine Antwort");
  return respond(r);
}

// ------------------------------------------------------------------ Routen

void begin() {
  req.ready = xSemaphoreCreateBinary();
  // http:// leitet auf https:// um (der HTTPS-Server startet, sobald das WLAN steht)
  web80 = new WebServer(80);
  web80->onNotFound([]() {
    String host = web80->hostHeader().length() ? web80->hostHeader() : netIp();
    web80->sendHeader("Location", "https://" + host + web80->uri());
    web80->send(301, "text/plain", "Bitte https verwenden");
  });
  web80->begin();
  on("/", HTTP_GET, []() { srv->send_P(200, "text/html; charset=utf-8", PAGE); });
  on("/api/state", HTTP_GET, state);
  // Diagnose Stromsparen: Zeit in den Schlafarten, gehaltene Sperren, Laufzeit, Grund des letzten Starts
  on("/api/pm", HTTP_GET, []() {
    if (!authed()) return;
    char* buf = nullptr;
    size_t len = 0;
    FILE* f = open_memstream(&buf, &len);
    fprintf(f, "uptime %llu s, reset %d, wakeup %d, dozing %d\n", esp_timer_get_time() / 1000000ULL,
            (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause(), (int)pwr::dozing());
    esp_pm_dump_locks(f);
    fclose(f);
    String out(buf, len);
    free(buf);
    srv->send(200, "text/plain; charset=utf-8", out);
  });
  on("/api/update", HTTP_POST, []() {});  // otaReceive

  // Zeitkonto: freigeben, sperren, Regeln
  on("/api/unlock", HTTP_POST, []() {  // {"app":0,"min":30} – min −1 = bis zur Bettzeit
    if (!authed()) return;
    JsonDocument d;
    quota::App a;
    if (!body(d) || !appArg(d, a)) return msg("Ungültige Anfrage", 400);
    if (!timeValid()) return msg("Die Uhr kennt die Uhrzeit noch nicht", 503);
    quota::unlock(a, d["min"] | 30, true);
    msg("Freigegeben");
  });
  on("/api/lock", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    quota::App a;
    if (!body(d) || !appArg(d, a)) return msg("Ungültige Anfrage", 400);
    quota::lock(a);
    msg("Gesperrt");
  });
  on("/api/rules", HTTP_POST, []() {  // {"app":0,"weekday":15,"weekend":30,"free":false} – fehlende bleiben
    if (!authed()) return;
    JsonDocument d;
    quota::App a;
    if (!body(d) || !appArg(d, a)) return msg("Ungültige Anfrage", 400);
    quota::Rule r = quota::rule(a);
    r.weekday = d["weekday"] | r.weekday;
    r.weekend = d["weekend"] | r.weekend;
    r.free = d["free"] | r.free;
    quota::setRule(a, r);
    msg("Gespeichert");
  });
  on("/api/kids", HTTP_POST, []() {  // Aufstehen, Bettzeit, Einschlafhören, Lautstärke-Grenze
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    kids::Cfg c = kids::cfg();
    c.wake = d["wake"] | c.wake;
    c.bed = d["bed"] | c.bed;
    c.storyMin = d["storyMin"] | c.storyMin;
    c.maxVol = d["maxVol"] | c.maxVol;
    kids::set(c);
    ui::volumeSet(cfg.volume);  // Obergrenze sofort anwenden
    msg("Gespeichert");
  });
  on("/api/kids/pin", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    const char* p = d["pin"] | "";
    size_t n = strlen(p);
    for (size_t i = 0; i < n; i++) if (!isdigit((unsigned char)p[i])) n = 99;
    if (n < 4 || n > 8) return msg("4–8 Ziffern", 400);
    kids::Cfg c = kids::cfg();
    snprintf(c.pin, sizeof(c.pin), "%s", p);
    kids::set(c);
    msg("PIN gespeichert");
  });
  on("/api/release", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    const char* p = body(d) ? (d["path"] | "") : "";
    if (strncmp(p, "radio:", 6) && strncmp(p, "ard:set:", 8) && strncmp(p, "ard:a:", 6)) return msg("Ungültige Anfrage", 400);
    kids::release(p, d["title"] | "", d["sub"] | "", d["on"] | true);
    msg("Gespeichert");
  });
  on("/api/game/reset", HTTP_POST, []() {
    if (!authed()) return;
    gameResetRecords();
    msg("Rekorde zurückgesetzt");
  });

  // Hören: Listen holen (Suche, Rubriken), abspielen, steuern – die Eltern dürfen alles, auch nachts
  on("/api/music/list", HTTP_GET, []() {
    if (!authed()) return;
    static music::Item* it = nullptr;
    if (!it) it = (music::Item*)heap_caps_calloc(80, sizeof(music::Item), MALLOC_CAP_SPIRAM);
    char title[72] = "", err[64] = "";
    int n = music::query(srv->arg("path").c_str(), title, sizeof(title), it, 80, err, sizeof(err));
    JsonDocument d;
    d["title"] = title;
    if (err[0]) d["err"] = err;
    JsonArray a = d["items"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject o = a.add<JsonObject>();
      o["id"] = it[i].id;
      o["title"] = it[i].title;
      o["sub"] = it[i].sub;
      o["kind"] = it[i].kind;
      o["dur"] = it[i].dur;
    }
    json(200, d);
  });
  on("/api/music/play", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    static music::Item* it = nullptr;
    if (!it) it = (music::Item*)heap_caps_calloc(80, sizeof(music::Item), MALLOC_CAP_SPIRAM);
    int n = 0;
    for (JsonObject o : d["items"].as<JsonArray>()) {
      if (n >= 80) break;
      music::Item& x = it[n++];
      x.kind = o["kind"] | 0;
      x.dur = o["dur"] | 0;
      snprintf(x.id, sizeof(x.id), "%s", (const char*)(o["id"] | ""));
      snprintf(x.title, sizeof(x.title), "%s", (const char*)(o["title"] | ""));
      snprintf(x.sub, sizeof(x.sub), "%s", (const char*)(o["sub"] | ""));
    }
    music::playList(it, n, d["index"] | 0, true);
    msg("Läuft auf der Uhr");
  });
  on("/api/music/ctl", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    const char* c = d["cmd"] | "";
    if (!strcmp(c, "toggle")) music::toggle();
    else if (!strcmp(c, "next")) music::next();
    else if (!strcmp(c, "prev")) music::prev();
    else if (!strcmp(c, "stop")) music::stop();
    if (d["volume"].is<int>()) { ui::volumeSet(d["volume"].as<int>()); settingsSave(); }
    if (d["sleep"].is<int>()) music::sleepSet(d["sleep"].as<int>());
    msg("OK");
  });
  on("/api/music/recent", HTTP_GET, []() {
    if (!authed()) return;
    static music::Recent* r = nullptr;  // ≈ 4 KB: PSRAM statt Stapel
    if (!r) r = (music::Recent*)heap_caps_calloc(10, sizeof(music::Recent), MALLOC_CAP_SPIRAM);
    int n = r ? music::recent(r, 10) : 0;
    JsonDocument d;
    JsonArray a = d["items"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject o = a.add<JsonObject>();
      o["id"] = r[i].item.id;
      o["title"] = r[i].item.title;
      o["sub"] = r[i].item.sub;
      o["dur"] = r[i].item.dur;
      o["pos"] = r[i].posSec;
    }
    json(200, d);
  });
  on("/api/music/recent", HTTP_POST, []() {  // {"play": id} · {"remove": id}
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    const char* id = d["remove"] | "";
    if (id[0]) return music::recentRemove(id) ? msg("Entfernt") : msg("Nicht gefunden", 404);
    id = d["play"] | "";
    static music::Recent* r = nullptr;
    if (!r) r = (music::Recent*)heap_caps_calloc(10, sizeof(music::Recent), MALLOC_CAP_SPIRAM);
    int n = id[0] && r ? music::recent(r, 10) : 0;
    for (int i = 0; i < n; i++)
      if (!strcmp(r[i].item.id, id)) {
        music::playList(&r[i].item, 1, 0, true);
        return msg("Läuft auf der Uhr");
      }
    msg("Nicht gefunden", 404);
  });

  // Gerät
  on("/api/find", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    body(d);
    ui::findMe(d["on"] | true, d["volume"] | 70);
    msg(d["on"] | true ? "Die Uhr klingelt 30 Sekunden" : "Beendet");
  });
  on("/api/power", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    if (d["mode"].is<int>()) pwr::setMode((pwr::Mode)d["mode"].as<int>());
    if (d["deep"].is<int>()) pwr::setDeepSleep(constrain(d["deep"].as<int>(), 0, 240));
    if (d["night"].is<bool>()) pwr::setNightRest(d["night"]);
    msg("Gespeichert");
  });
  on("/api/display", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return msg("Ungültige Anfrage", 400);
    if (d["brightness"].is<int>()) { cfg.brightness = constrain(d["brightness"].as<int>(), 10, 100); hwBrightness(cfg.brightness); }
    if (d["screenOff"].is<int>()) cfg.screenOff = constrain(d["screenOff"].as<int>(), 5, 600);
    settingsSave();
    msg("Gespeichert");
  });
  on("/api/name", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    const char* n = body(d) ? (d["name"] | "") : "";
    if (!n[0]) return msg("Name fehlt", 400);
    snprintf(cfg.name, sizeof(cfg.name), "%s", n);
    settingsSave();
    msg("Name gespeichert");
  });
  on("/api/wifi", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d) || !(d["ssid"] | "")[0]) return msg("SSID fehlt", 400);
    knownAdd(d["ssid"], d["pass"] | "");
    settingsSave();
    msg("Netz gespeichert");
    netConnect(d["ssid"], d["pass"] | "");
  });
  on("/api/wifi/forget", HTTP_POST, []() {
    if (!authed()) return;
    JsonDocument d;
    if (body(d)) { knownForget(d["ssid"] | ""); settingsSave(); }
    msg("Netz vergessen");
  });
  on("/api/restart", HTTP_POST, []() {
    if (!authed()) return;
    msg("Startet neu …");
    delay(300);
    ESP.restart();
  });
}

void poll() {
  if (web80) web80->handleClient();
  if (req.state != 1) return;
  req.state = 2;
  req.fn();
  if (!req.code) msg("Keine Antwort", 500);
  req.state = 0;
}

}  // namespace webui
