#!/usr/bin/env python3
"""Test auf der Uhr (USB + WLAN), lautlos. Bildschirmfotos landen in tests/out/device/.

    ~/.platformio/penv/bin/python tests/test_device.py [--port /dev/cu.usbmodem…] [--pin 1234]

--pin: die Eltern-PIN, falls eine gesetzt ist (für die Weboberfläche). Der Test ändert die PIN nicht. Genutzte Minuten
und gesperrte Apps stellt er am Ende wieder her; Freigaben, die vorher galten, bleiben bestehen.
"""
import argparse
import glob
import json
import os
import re
import ssl
import sys
import time
import urllib.error
import urllib.request

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import remote  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), "out", "device")
APPS = ["game", "radio", "stories"]
fails = []


def check(ok, what):
    print(("  ok   " if ok else "  FEHLER ") + what)
    if not ok:
        fails.append(what)


class Watch:
    def __init__(self, port):
        self.p = serial.Serial(port, 115200, timeout=0.5)
        time.sleep(0.3)
        self.p.reset_input_buffer()

    def cmd(self, line, wait=0.3):
        self.p.write((line + "\n").encode())
        time.sleep(wait)

    def ask(self, line, until, secs=3):  # Zeile schicken, Antwort mit „until“ zurückgeben
        self.p.reset_input_buffer()
        self.p.write((line + "\n").encode())
        end = time.time() + secs
        while time.time() < end:
            s = self.p.readline().decode(errors="replace")
            if until in s:
                return s.strip()
        return ""

    def status(self):
        s = self.ask("status", "Laufzeit")
        apps = re.findall(r"(\w+) (\d+)/(\d+) min, Status (\d), noch (-?\d+) s", s)
        up = re.search(r"Laufzeit (\d+) s", s)
        ip = re.search(r"WLAN an (\S+)", s)
        vol = re.search(r"Lautstärke (\d+)%", s)
        return {
            "volume": int(vol.group(1)) if vol else 60,
            "apps": [{"used": int(a[1]), "limit": int(a[2]), "status": int(a[3]), "left": int(a[4])} for a in apps],
            "uptime": int(up.group(1)) if up else -1,
            "ip": ip.group(1) if ip else "",
        }

    def labels(self):
        self.p.reset_input_buffer()
        self.p.write(b"dump\n")
        time.sleep(1.2)
        return self.p.read(self.p.in_waiting or 1).decode(errors="replace")

    def shot(self, name):
        os.makedirs(OUT, exist_ok=True)
        remote.shot(self.p, os.path.join(OUT, name + ".png"))


def https(ip, path, pin="", data=None):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    req = urllib.request.Request(f"https://{ip}{path}", data=json.dumps(data).encode() if data is not None else None,
                                 headers={"X-Pin": pin})
    try:
        with urllib.request.urlopen(req, context=ctx, timeout=25) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=(glob.glob("/dev/cu.usbmodem*") or [""])[0])
    ap.add_argument("--pin", default="")
    args = ap.parse_args()
    w = Watch(args.port)
    before = w.status()
    w.cmd("vol 0")  # lautlos
    check(len(before["apps"]) == 3, "Status mit drei Apps")
    if len(before["apps"]) != 3:
        sys.exit(1)

    print("Zifferblatt")
    w.cmd("home", 1)
    for code, temp, day, name in [(0, 26, "day", "sonne"), (2, 15, "day", "heiter"), (63, 9, "day", "regen"),
                                  (95, 17, "day", "gewitter"), (73, -2, "day", "schnee"), (45, 5, "day", "nebel"),
                                  (0, 8, "night", "nacht")]:
        w.cmd(f"weather {code} {temp} {day}", 1.2)
        w.shot("zifferblatt-" + name)
    w.cmd("weather off")

    print("Apps und Freigaben")
    w.cmd("unlock game 5")
    w.cmd("used game 0")
    w.cmd("page apps", 1.2)
    w.shot("apps")
    st = w.status()
    check(st["apps"][0]["status"] == 0 and 0 < st["apps"][0]["left"] <= 300, "Balance frei für 5 Minuten")
    w.cmd("lock game")
    check(w.status()["apps"][0]["status"] == 1, "Balance wieder gesperrt")

    print("Balance")
    w.cmd("unlock game 5")
    w.cmd("home", 1)
    w.cmd("open game", 1.5)
    w.shot("balance-menue")
    w.cmd("tap 233 203", 0.5)  # „Ins Ziel“
    w.cmd("tilt 0.15 0.1", 65)  # gut eine Minute spielen
    w.cmd("tilt off")
    w.shot("balance-spiel")
    st = w.status()
    check(st["apps"][0]["used"] >= 1, f"Spielzeit gezählt ({st['apps'][0]['used']} min)")
    w.cmd(f"used game {st['apps'][0]['limit'] or 15}")
    w.cmd("home", 1)
    w.cmd("page game", 1.5)
    check("genug gekugelt" in w.labels(), "Tageslimit erreicht: „Für heute genug gekugelt!“")
    w.shot("balance-ende")
    w.cmd("home", 1)

    print("Hörspiele")
    w.cmd("unlock stories 5")
    w.cmd("open stories", 4)
    w.shot("hoerspiele")
    check("Hörspiele" in w.labels(), "Liste der Hörspiele")
    w.cmd("home", 1)

    print("Weboberfläche")
    ip = w.status()["ip"]
    check(bool(ip), "WLAN verbunden")
    if ip:
        code, body = https(ip, "/")
        check(code == 200 and b"Pixeluhr" in body, "Seite lädt")
        code, body = https(ip, "/api/state", args.pin)
        check(code == 200, "Zustand mit PIN")
        if code == 200:
            d = json.loads(body)
            check(len(d["apps"]) == 3 and (not args.pin or args.pin.encode() not in body), "Zustand ohne die PIN selbst")
            if d["kids"]["pin"]:
                code, _ = https(ip, "/api/state", "")
                check(code == 401, "ohne PIN gesperrt")
        code, _ = https(ip, "/api/gibtsnicht", args.pin)
        check(code == 404, "unbekannte Adresse: 404")
        code, body = https(ip, "/api/unlock", args.pin, {"app": 1, "min": 1})
        check(code == 200 and w.status()["apps"][1]["status"] == 0, "Radio über die Weboberfläche freigegeben")
        https(ip, "/api/lock", args.pin, {"app": 1})
        check(w.status()["apps"][1]["status"] == 1, "Radio über die Weboberfläche gesperrt")

    print("Aufräumen")
    for i, a in enumerate(APPS):
        w.cmd(f"used {a} {before['apps'][i]['used']}")
        if before["apps"][i]["status"] == 1:
            w.cmd(f"lock {a}")
    w.cmd("home", 1)
    after = w.status()
    check(after["uptime"] > before["uptime"], "kein Neustart während des Tests")
    check([a["used"] for a in after["apps"]] == [a["used"] for a in before["apps"]], "genutzte Minuten wiederhergestellt")
    w.cmd(f"vol {before['volume']}")
    print(f"\n{'Alles ok' if not fails else str(len(fails)) + ' Fehler'} – Bilder in {OUT}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
