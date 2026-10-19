#!/usr/bin/env python3
"""Build the ESP32-S3 chatbot: config -> compile -> single merged .bin.

This is what build.bat / build.ps1 call.

On Windows the toolchain breaks when paths exceed the classic 260-character
limit (symptom: "bits/c++config.h: No such file or directory").  When the
project lives in a deep folder the script transparently maps a free drive
letter to it and builds through that short path.
"""
from __future__ import annotations

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FQBN = "esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=huge_app,FlashSize=4M"
IS_WIN = os.name == "nt"


def log(msg: str) -> None:
    print(msg, flush=True)


def run(cmd, cwd, env=None) -> None:
    log("  $ " + " ".join(cmd))
    if subprocess.call(cmd, cwd=cwd, env=env) != 0:
        raise SystemExit(1)


def pick_drive():
    for letter in "QRSTUVWXYZ":
        if not os.path.exists(letter + ":\\"):
            return letter + ":"
    return None


def short_root():
    """Map a drive letter when the project path is long. Returns (root, drive)."""
    if not IS_WIN or len(ROOT) <= 70:
        return ROOT, None
    drive = pick_drive()
    if drive is None:
        log("  ! no free drive letter available for the long-path workaround")
        return ROOT, None
    if subprocess.call(["subst", drive, ROOT]) != 0:
        log("  ! subst failed - building from the original path")
        return ROOT, None
    return drive + "\\", drive


def main() -> int:
    root, drive = short_root()
    try:
        if drive:
            log(f"  building through short path {drive}\\ (project path is long)")

        tools = os.path.join(root, "tools")
        cli = os.path.join(tools, "arduino-cli.exe")
        if not os.path.exists(cli):
            log("[!] toolchain missing. Run:  python tools\\setup_toolchain.py")
            return 1

        tmp = os.path.join(root, "build-temp")
        os.makedirs(tmp, exist_ok=True)
        env = dict(os.environ)
        env["TEMP"] = tmp
        env["TMP"] = tmp

        log("[1/4] Reading info.txt ...")
        run([sys.executable, os.path.join(tools, "gen_info.py")], cwd=root, env=env)

        log("[2/4] Compiling firmware ...")
        yaml = os.path.join(root, "arduino-cli.yaml")
        if drive:
            yaml = os.path.join(root, "arduino-cli-short.yaml")
            base = root.replace("\\", "/").rstrip("/")
            with open(yaml, "w", encoding="utf-8", newline="\n") as f:
                f.write(
                    "directories:\n"
                    f"  data: {base}/arduino-data\n"
                    f"  downloads: {base}/arduino-data/staging\n"
                    f"  user: {base}/arduino-data/user\n"
                    "logging:\n"
                    "  level: info\n"
                )

        build = os.path.join(root, "build")
        run(
            [
                cli,
                "--config-file",
                yaml,
                "compile",
                "--fqbn",
                FQBN,
                "--build-path",
                build,
                "--output-dir",
                os.path.join(build, "out"),
                "chatbot",
            ],
            cwd=root,
            env=env,
        )

        log("[3/4] Merging into a single 4 MB image ...")
        run(
            [
                sys.executable,
                os.path.join(tools, "merge_bin.py"),
                "--bootloader",
                os.path.join(build, "out", "chatbot.ino.bootloader.bin"),
                "--partitions",
                os.path.join(build, "out", "chatbot.ino.partitions.bin"),
                "--app",
                os.path.join(build, "out", "chatbot.ino.bin"),
                "--out",
                os.path.join(root, "dist", "DeepSeekChat_ESP32S3_4MB.bin"),
                "--size",
                "4MB",
            ],
            cwd=root,
            env=env,
        )

        log("[4/4] Done.")
        log("")
        log("  Firmware: dist\\DeepSeekChat_ESP32S3_4MB.bin")
        return 0
    finally:
        if drive:
            subprocess.call(["subst", drive, "/d"])


if __name__ == "__main__":
    sys.exit(main())
