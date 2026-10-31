#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Локальный OTA-сервер для прошивок XiaoZhi.

Сервер отдаёт:
  * /ota/                     - JSON с информацией о новой прошивке (проверка версии);
  * /firmware/<имя>.bin       - сам файл прошивки;
  * /                         - простая страница со статусом.

Версия прошивки читается прямо из .bin (esp_app_desc_t), поэтому сервер
всегда предлагает ровно ту версию, которая собрана. Чтобы устройство
увидело обновление, поднимите PROJECT_VER в CMakeLists.txt и пересоберите
прошивку - перезапускать сервер не нужно, файл читается на каждый запрос.

Запуск:
    python ota_server.py
    python ota_server.py --port 8090 --bin ..\\..\\build\\xiaozhi_vn.bin
"""

import argparse
import json
import socket
import sys
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

# esp_app_desc_t начинается с magic_word = 0xABCD5432 (little-endian)
APP_DESC_MAGIC = b"\x32\x54\xcd\xab"
APP_DESC_VERSION_OFFSET = 16  # magic(4) + secure_version(4) + reserv1(8)
APP_DESC_PROJECT_OFFSET = 48
APP_DESC_DATE_OFFSET = 96

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BIN = REPO_ROOT / "build" / "xiaozhi_vn.bin"
DEFAULT_PORT = 8090


def read_app_info(bin_path: Path):
    """Возвращает (version, project, build_date) из ESP-IDF .bin."""
    with open(bin_path, "rb") as f:
        head = f.read(4096)

    idx = head.find(APP_DESC_MAGIC)
    if idx < 0:
        return None, None, None

    def field(offset, size):
        raw = head[idx + offset: idx + offset + size]
        return raw.split(b"\x00", 1)[0].decode("utf-8", "replace").strip()

    version = field(APP_DESC_VERSION_OFFSET, 32)
    project = field(APP_DESC_PROJECT_OFFSET, 32)
    build_date = field(APP_DESC_DATE_OFFSET, 16)
    return (version or None), (project or None), (build_date or None)


def local_ip() -> str:
    """IP-адрес этого компьютера в локальной сети."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def human_size(num: int) -> str:
    for unit in ("Б", "КБ", "МБ", "ГБ"):
        if num < 1024 or unit == "ГБ":
            return f"{num:.0f} {unit}" if unit == "Б" else f"{num:.1f} {unit}"
        num /= 1024.0
    return f"{num:.1f} ГБ"


class OtaHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "XiaoZhiLocalOTA/1.0"

    # ---------------------------------------------------------------- helpers
    @property
    def cfg(self):
        return self.server.cfg

    def _firmware_info(self):
        """Читает .bin на каждый запрос: заменили файл - сразу новая версия."""
        path = self.cfg["bin"]
        if not path.is_file():
            return None
        version, project, build_date = read_app_info(path)
        version = self.cfg["version_override"] or version
        stat = path.stat()
        return {
            "path": path,
            "version": version,
            "project": project,
            "build_date": build_date,
            "size": stat.st_size,
            "mtime": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(stat.st_mtime)),
        }

    def _base_url(self):
        host = self.headers.get("Host")
        if not host:
            host = f"{local_ip()}:{self.cfg['port']}"
        return f"http://{host}"

    def _send(self, status, content_type, body: bytes):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _send_json(self, status, payload: dict):
        body = json.dumps(payload, ensure_ascii=False, indent=2).encode("utf-8")
        self._send(status, "application/json; charset=utf-8", body)

    # ------------------------------------------------------------------ http
    def handle_one_request(self):
        # Устройство обрывает соединение сразу после скачивания прошивки -
        # это нормально, не засоряем консоль трейсбеками.
        try:
            super().handle_one_request()
        except (ConnectionResetError, BrokenPipeError):
            self.close_connection = True

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", "/ota", "/ota/"):
            if "application/json" in (self.headers.get("Accept") or ""):
                self._version_json()
            else:
                self._index_html()
        elif path.startswith("/firmware/"):
            self._firmware_file()
        elif path == "/health":
            self._send_json(200, {"status": "ok"})
        else:
            self._send(404, "text/plain; charset=utf-8", b"not found\n")

    def do_POST(self):
        path = urlparse(self.path).path
        body = b""
        try:
            length = int(self.headers.get("Content-Length") or 0)
            if 0 < length <= 64 * 1024:
                body = self.rfile.read(length)
        except (ValueError, OSError):
            pass

        if path in ("/", "/ota", "/ota/"):
            self._log_check(body)
            self._version_json()
        else:
            self._send(404, "text/plain; charset=utf-8", b"not found\n")

    # ------------------------------------------------------------- endpoints
    def _version_json(self):
        info = self._firmware_info()
        if info is None:
            self._send_json(503, {
                "error": "firmware_not_found",
                "hint": f"Нет файла {self.cfg['bin']}. Сначала выполните idf.py build.",
            })
            return
        if not info["version"]:
            self._send_json(503, {
                "error": "version_not_found",
                "hint": "Не удалось прочитать версию из .bin",
            })
            return

        base = self._base_url()
        payload = {
            "firmware": {
                "version": info["version"],
                "url": f"{base}/firmware/{info['path'].name}",
                "size": info["size"],
                "force": 1 if self.cfg["force"] else 0,
            }
        }
        self._send(200, "application/json; charset=utf-8",
                   json.dumps(payload, ensure_ascii=False, indent=2).encode("utf-8"))

    def _firmware_file(self):
        info = self._firmware_info()
        if info is None:
            self._send_json(404, {"error": "firmware_not_found"})
            return
        path = info["path"]
        size = info["size"]
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.send_header("Content-Disposition", f'attachment; filename="{path.name}"')
        self.end_headers()
        if self.command == "HEAD":
            return

        started = time.time()
        sent = 0
        try:
            with open(path, "rb") as f:
                while True:
                    chunk = f.read(64 * 1024)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    self.wfile.flush()
                    sent += len(chunk)
        except (BrokenPipeError, ConnectionResetError):
            self.log(f"загрузка прервана устройством на {human_size(sent)}")
            self.close_connection = True
            return
        elapsed = max(time.time() - started, 0.001)
        self.log(f"прошивка отдана: {human_size(sent)} за {elapsed:.1f} с "
                 f"({sent / elapsed / 1024:.0f} КБ/с)")
        self.close_connection = True

    def _index_html(self):
        info = self._firmware_info()
        base = self._base_url()
        checks = "".join(f"<li>{line}</li>" for line in reversed(self.server.log_lines)) \
            or "<li>пока никто не обращался</li>"

        if info:
            fw_block = (
                f"<p><b>Версия:</b> {info['version'] or '?'}<br>"
                f"<b>Размер:</b> {human_size(info['size'])} ({info['size']} байт)<br>"
                f"<b>Проект:</b> {info['project'] or '?'}<br>"
                f"<b>Собрано:</b> {info['build_date'] or '?'} (<code>{info['mtime']}</code>)<br>"
                f"<b>Файл:</b> <code>{info['path']}</code></p>"
                f"<p><b>Проверка версии:</b> <code>{base}/ota/</code><br>"
                f"<b>Прошивка:</b> <code>{base}/firmware/{info['path'].name}</code></p>"
            )
            status = 200
            filename = info["path"].name
        else:
            fw_block = (
                f"<p class='err'>Файл прошивки не найден: <code>{self.cfg['bin']}</code></p>"
                "<p>Сначала соберите прошивку: <code>idf.py build</code></p>"
            )
            status = 503
            filename = "xiaozhi_vn.bin"

        html = f"""<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>XiaoZhi OTA</title>
<style>
  body {{ background:#0f1115; color:#e8ecf1; font:15px/1.55 system-ui,Segoe UI,Roboto,sans-serif; margin:0; }}
  main {{ max-width:820px; margin:40px auto; padding:0 20px; }}
  h1 {{ font-size:22px; margin:0 0 4px; }}
  .sub {{ color:#8b96a5; margin-bottom:24px; }}
  .card {{ background:#171b22; border:1px solid #232a34; border-radius:14px; padding:20px 22px; margin-bottom:18px; }}
  code {{ background:#0b0d11; padding:2px 6px; border-radius:6px; color:#7ee2c1; }}
  .ok {{ color:#5ad39a; font-weight:600; }}
  .err {{ color:#ff7b7b; font-weight:600; }}
  ol {{ padding-left:20px; }} li {{ margin:4px 0; }}
  ul.log {{ list-style:none; padding:0; font-size:13px; color:#9aa6b4; max-height:240px; overflow:auto; }}
  ul.log li {{ border-bottom:1px solid #232a34; padding:6px 0; }}
</style></head><body><main>
  <h1>Локальный OTA-сервер XiaoZhi</h1>
  <div class="sub">Обновление устройства по Wi-Fi, без USB и без облака</div>
  <div class="card">{fw_block}</div>
  <div class="card">
    <p class="ok">Сервер работает, порт {self.cfg['port']}.</p>
    <ol>
      <li>Пропишите на устройстве адрес проверки: <code>{base}/ota/</code><br>
          (инструмент <code>self.system.set_ota_url</code>, параметр <code>url</code>)</li>
      <li>Пересоберите прошивку с новым <code>PROJECT_VER</code> в <code>CMakeLists.txt</code> и
      запустите <code>idf.py build</code> — сервер подхватит новый файл автоматически.</li>
      <li>Перезагрузите устройство — оно проверит версию, скачает прошивку и обновится.</li>
    </ol>
    <p>Ручное обновление: <code>http://&lt;ip-устройства&gt;/ota</code><br>
       Принудительно: <code>self.upgrade_firmware</code> с URL
       <code>{base}/firmware/{filename}</code></p>
  </div>
  <div class="card">
    <b>Журнал проверок</b>
    <ul class="log">{checks}</ul>
  </div>
</main></body></html>"""
        self._send(status, "text/html; charset=utf-8", html.encode("utf-8"))

    # ---------------------------------------------------------------- logging
    def _log_check(self, body: bytes):
        device = {}
        try:
            device = json.loads(body.decode("utf-8")) if body else {}
        except (ValueError, UnicodeDecodeError):
            pass

        info = self._firmware_info()
        offered = info["version"] if info else "?"
        application = device.get("application") if isinstance(device.get("application"), dict) else {}
        current = application.get("version") or device.get("version") or "?"
        mac = device.get("mac_address", "?")
        chip = device.get("chip_model_name", "?")
        self.log(f"проверка: {mac} ({chip}) сейчас {current} -> предлагается {offered}")

    def log(self, message: str):
        stamp = time.strftime("%H:%M:%S")
        line = f"[{stamp}] {message}"
        self.server.log_lines.append(line)
        print(f"  {line}", flush=True)

    def log_message(self, fmt, *args):
        # Отключаем стандартный шум BaseHTTPRequestHandler.
        pass


class OtaServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, addr, handler, cfg):
        super().__init__(addr, handler)
        self.cfg = cfg
        self.log_lines = deque(maxlen=30)


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Локальный OTA-сервер XiaoZhi")
    parser.add_argument("--bin", default=str(DEFAULT_BIN),
                        help=f"путь к прошивке (по умолчанию {DEFAULT_BIN})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"порт (по умолчанию {DEFAULT_PORT})")
    parser.add_argument("--bind", default="0.0.0.0",
                        help="адрес прослушивания (по умолчанию 0.0.0.0)")
    parser.add_argument("--force", action="store_true",
                        help="force=1: устройство обновится даже на ту же версию")
    parser.add_argument("--version", default=None,
                        help="подменить версию в JSON (для тестов; обычно не нужно)")
    return parser.parse_args(argv)


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    args = parse_args(argv or sys.argv[1:])
    bin_path = Path(args.bin).expanduser().resolve()

    if not bin_path.is_file():
        print(f"Файл прошивки не найден: {bin_path}", flush=True)
        print("Сначала соберите прошивку: idf.py build", flush=True)
        return 1

    version, project, _ = read_app_info(bin_path)
    if not version:
        print(f"Не удалось прочитать версию из {bin_path}", flush=True)
        print("Похоже, это не образ приложения ESP-IDF.", flush=True)
        return 1

    ip = local_ip()
    cfg = {
        "bin": bin_path,
        "port": args.port,
        "force": args.force,
        "version_override": args.version,
    }

    try:
        server = OtaServer((args.bind, args.port), OtaHandler, cfg)
    except OSError as exc:
        print(f"Не удалось занять порт {args.port}: {exc}", flush=True)
        print(f"Попробуйте другой порт: python ota_server.py --port {args.port + 1}", flush=True)
        return 1

    print("=" * 66, flush=True)
    print("  Локальный OTA-сервер XiaoZhi", flush=True)
    print("=" * 66, flush=True)
    print(f"  Прошивка : {bin_path}", flush=True)
    print(f"  Версия   : {version}  ({project or '?'}, {human_size(bin_path.stat().st_size)})", flush=True)
    print(f"  Адрес    : http://{ip}:{args.port}/ota/", flush=True)
    print(f"  Файл     : http://{ip}:{args.port}/firmware/{bin_path.name}", flush=True)
    print(f"  Страница : http://{ip}:{args.port}/", flush=True)
    print("-" * 66, flush=True)
    print("  На устройстве задайте адрес проверки:", flush=True)
    print(f"    self.system.set_ota_url  url=http://{ip}:{args.port}/ota/", flush=True)
    print("  После этого прошивка обновляется сама при загрузке.", flush=True)
    print("  Остановка: Ctrl+C", flush=True)
    print("=" * 66, flush=True)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nОстановлено.", flush=True)
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
