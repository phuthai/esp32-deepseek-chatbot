#!/usr/bin/env python3
"""Merge ESP32-S3 bootloader + partition table + application into one .bin.

The result can be flashed at offset 0x0 with a single command or with the
Espressif Flash Download Tool ("single bin" workflow).
"""
from __future__ import annotations

import argparse
import hashlib
import os
import sys

# ESP32-S3 flash layout used by the "huge_app" Arduino partition scheme.
SECTIONS = (
    ("bootloader", 0x000000),
    ("partitions", 0x008000),
    ("app", 0x010000),
)


def parse_size(text: str) -> int:
    s = text.strip().upper()
    mult = 1
    if s.endswith("MB"):
        mult, s = 1024 ** 2, s[:-2]
    elif s.endswith("KB"):
        mult, s = 1024, s[:-2]
    return int(float(s) * mult)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bootloader", required=True)
    ap.add_argument("--partitions", required=True)
    ap.add_argument("--app", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", default="4MB", help="target flash size, e.g. 4MB, 8MB")
    args = ap.parse_args()

    total = parse_size(args.size)
    paths = {
        "bootloader": args.bootloader,
        "partitions": args.partitions,
        "app": args.app,
    }

    parts = []
    for name, offset in SECTIONS:
        path = paths[name]
        if not os.path.exists(path):
            print(f"ERROR: missing {name} binary: {path}")
            return 1
        data = open(path, "rb").read()
        if len(data) == 0:
            print(f"ERROR: {name} binary is empty: {path}")
            return 1
        parts.append((name, offset, data))

    end = 0
    for name, offset, data in parts:
        if offset + len(data) > total:
            print(f"ERROR: {name} ends at 0x{offset + len(data):X}, "
                  f"beyond the {total // (1024 * 1024)} MB flash size")
            return 1
        if offset < end:
            print(f"ERROR: {name} at 0x{offset:X} overlaps the previous section "
                  f"(ends at 0x{end:X})")
            return 1
        end = offset + len(data)

    part_table = parts[1][2]
    if part_table[:2] != b"\xaa\x50":
        print("WARNING: partition table magic not found - is this really a "
              "partition-table binary?")

    image = bytearray(b"\xff" * total)
    for _, offset, data in parts:
        image[offset:offset + len(data)] = data

    out_dir = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(out_dir, exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(image)

    digest = hashlib.sha256(image).hexdigest()
    print(f"  merged : {args.out}")
    print(f"  size   : {len(image):,} bytes ({len(image) / 1024 / 1024:.2f} MB)")
    for name, offset, data in parts:
        print(f"  {name:<10} 0x{offset:06X} .. 0x{offset + len(data):06X}  ({len(data):,} bytes)")
    print(f"  sha256 : {digest}")
    with open(args.out + ".sha256", "w", encoding="utf-8") as f:
        f.write(f"{digest}  {os.path.basename(args.out)}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
