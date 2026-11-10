#!/usr/bin/env python3
"""Fernbedienung für die Uhr über USB: Gesten auslösen, Befehle schicken, Bildschirmfotos holen.

    ~/.platformio/penv/bin/python tools/remote.py /dev/cu.usbmodem* tap 233 233 wait 1 shot bild.png

Befehle: tap X Y · hold X Y · swipe l|r|u|d · boot · pwr · wait SEK · shot DATEI.png · log SEK
         cmd "beliebige Zeile"   (z. B. cmd "wifi MeinNetz geheim")
Braucht pyserial (z. B. aus PlatformIO: ~/.platformio/penv/bin/python).
"""
import struct
import sys
import time
import zlib

import serial

W = H = 466  # wird beim ersten Bildschirmfoto aus der Kennung „FB<Breite>“ übernommen (eckig: 480)


def png(path, raw, W=466, H=466):
    rows = bytearray()
    for y in range(H):
        rows.append(0)
        for x in range(W):
            v = raw[2 * (y * W + x)] | (raw[2 * (y * W + x) + 1] << 8)
            rows += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b""))


def shot(port, path, tries=3):
    marker = b"\nFB"
    for _ in range(tries):
        port.reset_input_buffer()
        port.write(b"shot\n")
        buf = b""
        end = time.time() + 8
        while marker not in buf and time.time() < end:
            buf += port.read(4096)
        if marker not in buf:
            continue
        buf = buf.split(marker, 1)[1]
        while b"\n" not in buf and time.time() < end:
            buf += port.read(16)
        size, buf = buf.split(b"\n", 1)
        W = H = int(size)
        end = time.time() + 20
        while len(buf) < W * H * 2 and time.time() < end:
            buf += port.read(W * H * 2 - len(buf))
        if len(buf) >= W * H * 2:
            png(path, buf[:W * H * 2], W, H)
            print("gespeichert:", path)
            return
        print(f"unvollständig ({len(buf)} Byte), neuer Versuch")
    sys.exit("kein Bildschirmfoto erhalten")


def log(port, secs):
    end = time.time() + secs
    while time.time() < end:
        line = port.readline()
        if line:
            print(line.decode(errors="replace").rstrip())


def main():
    port = serial.Serial(sys.argv[1], 115200, timeout=0.5)
    args = sys.argv[2:]
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("tap", "hold"):
            port.write(f"{a} {args[i + 1]} {args[i + 2]}\n".encode()); i += 3
        elif a == "swipe":
            port.write(f"swipe {args[i + 1]}\n".encode()); i += 2
        elif a in ("boot", "pwr", "pwrlong"):
            port.write(f"{a}\n".encode()); i += 1
        elif a == "cmd":
            port.write((args[i + 1] + "\n").encode()); i += 2
        elif a == "wait":
            time.sleep(float(args[i + 1])); i += 2
        elif a == "log":
            log(port, float(args[i + 1])); i += 2
        elif a == "shot":
            shot(port, args[i + 1]); i += 2
        else:
            sys.exit(f"unbekannt: {a}")
        time.sleep(0.15)


if __name__ == "__main__":
    main()
