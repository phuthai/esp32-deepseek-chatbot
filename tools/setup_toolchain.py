#!/usr/bin/env python3
"""One-time toolchain setup for building the ESP32-S3 chatbot on Windows.

Downloads arduino-cli (single executable), the Arduino ESP32 core (which
bundles the Xtensa GCC toolchain and esptool) and the ArduinoJson library.
Everything is installed inside this folder - nothing is added to PATH.

Usage:  python tools/setup_toolchain.py
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, "arduino-data")
CLI = os.path.join(HERE, "arduino-cli.exe")
YAML = os.path.join(ROOT, "arduino-cli.yaml")

API = "https://api.github.com/repos/arduino/arduino-cli/releases/latest"


def log(msg: str) -> None:
    print(msg, flush=True)


def download(url: str, dest: str) -> None:
    req = urllib.request.Request(url, headers={"User-Agent": "esp32-chatbot-setup"})
    with urllib.request.urlopen(req, timeout=300) as r, open(dest, "wb") as f:
        total = int(r.headers.get("Content-Length", 0))
        done = 0
        while True:
            block = r.read(1 << 20)
            if not block:
                break
            f.write(block)
            done += len(block)
            if total:
                print(f"\r  {done * 100 // total:3d}%  {done / 1e6:6.1f} / {total / 1e6:.1f} MB", end="")
        print()


def main() -> int:
    os.makedirs(DATA, exist_ok=True)

    if not os.path.exists(CLI):
        log("Downloading arduino-cli ...")
        req = urllib.request.Request(API, headers={"User-Agent": "esp32-chatbot-setup"})
        release = json.load(urllib.request.urlopen(req, timeout=60))
        url = next(a["browser_download_url"] for a in release["assets"]
                   if a["name"].endswith("Windows_64bit.zip"))
        zip_path = os.path.join(HERE, "arduino-cli.zip")
        download(url, zip_path)
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(HERE)
        os.remove(zip_path)
        log(f"arduino-cli {release['tag_name']} installed")

    data_posix = DATA.replace("\\", "/")
    with open(YAML, "w", encoding="utf-8") as f:
        f.write(
            "directories:\n"
            f"  data: {data_posix}\n"
            f"  downloads: {data_posix}/staging\n"
            f"  user: {data_posix}/user\n"
            "logging:\n"
            "  level: info\n"
        )

    def run(*args: str) -> None:
        if subprocess.call([CLI, "--config-file", YAML, *args]) != 0:
            raise SystemExit(f"command failed: {' '.join(args)}")

    log("Updating package index ...")
    run("core", "update-index")
    log("Installing esp32:esp32 core (this downloads ~2 GB the first time) ...")
    run("core", "install", "esp32:esp32")
    log("Installing ArduinoJson ...")
    run("lib", "install", "ArduinoJson")
    log("")
    log("Toolchain ready. Next step: edit info.txt, then run build.bat")
    return 0


if __name__ == "__main__":
    sys.exit(main())
