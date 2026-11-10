#!/usr/bin/env python3
"""Prüft die Weboberfläche aus firmware/src/webui.cpp: Skript syntaktisch gültig (node --check), jede per $('…')
angesprochene ID steht im HTML, jede Route, die das Skript aufruft, gibt es in der Firmware."""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, "firmware", "src", "webui.cpp"), encoding="utf-8").read()
html = re.search(r'R"HTML\((.*?)\)HTML"', src, re.S).group(1)
script = re.search(r"<script>(.*?)</script>", html, re.S).group(1)
bad = 0

with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
    f.write(script)
r = subprocess.run(["node", "--check", f.name], capture_output=True, text=True)
os.unlink(f.name)
if r.returncode:
    print("Skript ungültig:\n" + r.stderr)
    bad += 1

ids = set(re.findall(r'id="([^"]+)"', html))
for used in sorted(set(re.findall(r"\$\('([A-Za-z0-9_-]+)'\)", script))):
    if used not in ids:
        print(f"ID fehlt im HTML: {used}")
        bad += 1

routes = set(re.findall(r'on\("(/api/[^"]+)"', src))
calls = set(re.findall(r"(?:api|post)\('([a-z/]+)", script))
for c in sorted(calls):
    if "/api/" + c not in routes:
        print(f"Route fehlt in der Firmware: /api/{c}")
        bad += 1

print("Weboberfläche: " + ("ok" if not bad else f"{bad} Fehler") + f" ({len(ids)} IDs, {len(calls)} Routen)")
sys.exit(1 if bad else 0)
