#!/usr/bin/env python3
"""Local portal review and real-browser codec/IndexedDB tests.

API responses are fixtures, not an ESP32 emulator. Storage worker behavior is
tested separately by scripts/test/storage.sh. Requires ffmpeg for video/GIF.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import mimetypes
from pathlib import Path
import subprocess
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "build/portal"
VALUES = {"lcd_brightness": "160", "message": "오늘도 한 걸음", "media_loop": "1",
          "dday": "2027-01-01", "core_order": "0,1,2,3,4,5,6,7,8", "label": "수능까지",
          "time_color": "65535", "core_mask": "5", "night_start": "1320", "now_layout": "5"}
STATE = {"token": "local-test-fixture", "mode": 0, "message": "로컬 검증용 응답",
         "ble": "AP 중 대기", "sd": True, "uptime": 3600, "sensor": True, "autoUpdate": False,
         "temperature": 23.4, "humidity": 47.2, "volts": 3.95,
         "syncSession": 0, "syncStale": False,
         "track": {"key": "1234567890abcdef", "title": "테스트 곡", "artist": "NOVA", "album": "검증"},
         "network": {"connected": True, "test": "대기", "scan": [], "saved": ["Test AP"]},
         "diagnostics": "LOCAL FIXTURE — device runtime not connected\nfirmware=0.1.0"}


class Handler(BaseHTTPRequestHandler):
    def reply(self, body, kind="application/json", status=200):
        if not isinstance(body, bytes):
            body = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/api/status":
            return self.reply(STATE)
        if path == "/api/settings":
            return self.reply({"values": VALUES, "specs": [
                {"name": "lcd_brightness", "type": 0, "min": 8, "max": 255},
                {"name": "message", "type": 6, "min": 0, "max": 193},
                {"name": "media_loop", "type": 5, "min": 0, "max": 1},
                {"name": "label", "type": 6, "min": 0, "max": 65},
                {"name": "time_color", "type": 1, "min": 0, "max": 65535},
                {"name": "core_mask", "type": 1, "min": 1, "max": 511},
                {"name": "night_start", "type": 1, "min": 0, "max": 1439},
                {"name": "now_layout", "type": 0, "min": 0, "max": 5}]})
        if path in ("/api/media", "/api/files"):
            return self.reply([])
        if path == "/tests/browser.html":
            file = ROOT / "tests/portal/browser.html"
        elif path in ("/tests/sample.mp4", "/tests/sample.gif"):
            file = OUTPUT / Path(path).name
        elif path == "/":
            file = ROOT / "assets/portal/index.html"
        elif path.count("/") == 1 and Path(path).suffix in (".js", ".css"):
            file = ROOT / "assets/portal" / path[1:]
        else:
            return self.reply({"error": "not found"}, status=404)
        self.reply(file.read_bytes(), mimetypes.guess_type(file)[0] or "application/octet-stream")

    def do_POST(self):
        if self.headers.get("X-NOVA") != STATE["token"]:
            return self.reply({"ok": False, "message": "Missing fixture token"}, status=403)
        data = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
        if self.path == "/api/settings":
            VALUES.update(data)
        elif self.path == "/api/action" and data.get("op") == "mode":
            STATE["mode"] = data["value"]
        elif self.path == "/api/action" and data.get("op") == "updateAuto" and isinstance(data.get("enabled"), bool):
            STATE["autoUpdate"] = data["enabled"]
        else:
            return self.reply({"ok": False, "message": "This action needs the device"}, status=400)
        return self.reply({"ok": True, "message": "Fixture saved"})


if __name__ == "__main__":
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for suffix, options in (("mp4", ["-pix_fmt", "yuv420p"]), ("gif", [])):
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                        "testsrc2=size=160x160:rate=10:duration=1", *options,
                        str(OUTPUT / ("sample." + suffix))], check=True)
    print("Portal: http://127.0.0.1:8765/ — codecs: /tests/browser.html", flush=True)
    ThreadingHTTPServer(("127.0.0.1", 8765), Handler).serve_forever()
