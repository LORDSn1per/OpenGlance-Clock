"""Local-only mock bridge UI. No clock commands or bridge POSTs are sent.

Usage: python3 chime_ui_preview.py [port]
GETs one baseline status from the real bridge, then all interactions remain in
memory. Field 15 and the comet are SIMULATED: this is not the physical clock.
"""
import copy
import json
import re
import sys
from threading import Timer
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs
from urllib.request import urlopen

header = (Path(__file__).parent.parent / "OpenGlanceBridge/web_ui.h").read_text()
macros = {}
for name in ("OG_COMMON_CSS", "OG_LOGO", "OG_TZ_JS"):
    macros[name] = re.search(r"#define " + name + r' R"(\w+)\((.*?)\)\1"', header, re.S).group(2)
expression = header.split("static const char INDEX_HTML[] PROGMEM = ", 1)[1].rsplit(')HTML";', 1)[0] + ')HTML"'
pieces = re.findall(r'R"(\w+)\((.*?)\)\1"|(OG_COMMON_CSS|OG_LOGO|OG_TZ_JS)', expression, re.S)
html = "".join(text if delimiter else macros[name] for delimiter, text, name in pieces)
html = html.replace("<h1>OpenGlance</h1>", "<h1>OpenGlance · simulated UI preview</h1>")
with urlopen("http://192.168.1.116/api/status", timeout=8) as response:
    state = json.load(response)
state = copy.deepcopy(state)
state["fw"] = "1.22 preview"
# The real bridge can be reconnecting during development. All of these values
# are a local demo fixture, not telemetry or settings written to the clock.
state["clock"].update({"state": "connected", "connected": True, "busy": True})
state["clock"]["hand"] = {"valid": True, "homing": False, "awaiting": False,
    "homingFailure": False, "motorFailure": False, "timeAdjusting": True, "bits": 0x6314}
state["clock"]["settings"] = state["clock"].get("settings") or {
    "brightness": 0, "autoLow": 30, "autoHigh": 220, "nightMode": False,
    "digitalTime": True, "twelveHour": True, "hourPoints": True, "muted": False, "dateFormat": 1}
state["clock"]["settings"]["builtInChimeSound"] = 6
state["clock"]["settings"]["dnd"] = {"known": True, "mode": 1, "from": 22, "till": 8}
state["clock"]["settings"]["silent"] = {"known": True, "mode": 0, "from": 22, "till": 8}
STEPS = [8, 8, 8, 8, 6, 8, 8, 8, 6, 6, 6, 8, 6, 8, 8]   # comet::kSceneSteps, masks 1-15
state["clock"]["comet"] = {"on": True, "color": 18, "rings": 1, "sceneSteps": STEPS, "respectDnd": True,
    "dndActive": True, "stateFresh": True, "running": False, "status": "Paused — clock Do Not Disturb"}
state["time"].update({"source": "manual", "valid": True})
if not state["time"].get("local"):
    state["time"]["local"] = "2026-10-13 19:00:00"
status_reads = 0


class Handler(BaseHTTPRequestHandler):
    def reply(self, value, status=200):
        data = value.encode() if isinstance(value, str) else json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8" if isinstance(value, str) else "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        global status_reads
        if self.path.startswith("/api/status"):
            status_reads += 1
            self.reply(state)
        elif self.path.startswith("/api/preview/counters"):
            self.reply({"statusReads": status_reads})
        elif self.path.startswith("/api/networks"):
            self.reply({"networks": []})
        else:
            self.reply(html)

    def do_POST(self):
        args = parse_qs(self.rfile.read(int(self.headers.get("Content-Length", "0"))).decode())
        if self.path == "/api/time/manual":
            from datetime import datetime
            date = args.get("date", [""])[0]
            clock_time = args.get("time", [""])[0]
            try:
                chosen = datetime.strptime(date + " " + clock_time, "%Y-%m-%d %H:%M")
            except ValueError:
                return self.reply({"ok": False, "error": "Invalid manual time"}, 400)
            state["time"].update({"source": "manual", "valid": True, "local": chosen.strftime("%Y-%m-%d %H:%M:%S")})
            self.reply({"ok": True, "message": "Simulated manual time applied"})
        elif self.path == "/api/time/internet":
            state["time"].update({"source": "internet", "valid": True})
            self.reply({"ok": True, "message": "Simulated internet time selected"})
        elif self.path == "/api/comet":
            on = args.get("on", [""])[0]
            color = args.get("color", [""])[0]
            respect = args.get("respectDnd", [None])[0]
            rings = args.get("rings", [str(state["clock"]["comet"].get("rings", 1))])[0]
            if not rings.isdecimal() or not 1 <= int(rings) <= 15:
                return self.reply({"ok": False, "error": "Choose at least one of the four seconds LEDs"}, 400)
            if on not in ("0", "1") or not color.isdecimal() or not 1 <= int(color) <= 24:
                return self.reply({"ok": False, "error": "Invalid comet options"}, 400)
            if respect is not None and respect not in ("0", "1"):
                return self.reply({"ok": False, "error": "Invalid DND option"}, 400)
            active = on == "1"
            def apply_options():
                state["clock"]["comet"] = {"on": active, "color": int(color), "rings": int(rings), "sceneSteps": STEPS, "respectDnd": True,
                    "dndActive": True, "stateFresh": True, "running": False,
                    "status": "Paused — clock Do Not Disturb" if active else "Off — weather ring available"}
            # Delayed serial-command fixture: status briefly still has old
            # values after HTTP acknowledgement. UI must not bounce switches.
            Timer(2, apply_options).start()
            print("MOCK ONLY: comet ->", on, color, "LEDs", rings, "DND always respected", flush=True)
            self.reply({"ok": True, "message": "Simulated options saved"})
        elif self.path == "/api/setting":
            field = args.get("field", [""])[0]
            value = args.get("value", [""])[0]
            if field != "15" or not value.isdecimal() or not 0 <= int(value) <= 17:
                return self.reply({"ok": False, "error": "Invalid setting"}, 400)
            state["clock"]["settings"]["builtInChimeSound"] = int(value)
            print("MOCK ONLY: field 15 ->", value, flush=True)
            self.reply({"ok": True, "message": "Simulated read-back confirmed"})
        elif self.path == "/api/preview/sound":
            value = args.get("value", [""])[0]
            settings = state["clock"]["settings"]
            if value == "missing":
                settings.pop("builtInChimeSound", None)
            else:
                settings["builtInChimeSound"] = int(value)
            self.reply({"ok": True})
        elif self.path == "/api/night":
            which = args.get("which", [""])[0]
            try:
                mode, start, end = (int(args.get(k, ["x"])[0]) for k in ("mode", "from", "till"))
            except ValueError:
                return self.reply({"ok": False, "error": "Invalid night settings"}, 400)
            if which not in ("dnd", "silent") or mode not in (0, 1, 2) or not (0 <= start <= 23 and 0 <= end <= 23) or (mode == 1 and start == end):
                return self.reply({"ok": False, "error": "Choose Off, Scheduled or Always, and hours 0–23 (start and end must differ)"}, 400)
            def apply_night():
                state["clock"]["settings"][which] = {"known": True, "mode": mode, "from": start, "till": end}
                if which == "silent":
                    state["clock"]["settings"]["muted"] = mode == 2
                if which == "dnd":
                    state["clock"]["comet"]["dndActive"] = mode != 0
            Timer(2, apply_night).start()
            print("MOCK ONLY: night", which, mode, start, end, flush=True)
            self.reply({"ok": True, "message": "Updating the clock's night settings…"})
        elif self.path == "/api/refresh":
            self.reply({"ok": True, "message": "Simulated settings refreshed"})
        else:
            self.reply({"ok": False, "error": "Preview blocks all other actions"}, 403)


ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 8765), Handler).serve_forever()
