#!/usr/bin/env python3
"""
Build Magnolia ELF applets and pack them into LittleFS image for vfs partition.

Usage:
  python tools/applets.py build
  python tools/applets.py flash   (optional, best-effort)

Requires ESP-IDF environment for building applets (idf.py in PATH, IDF_PATH set).
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APPLETS_DIR = ROOT / "applets"
BUILD_DIR = ROOT / "build"
STAGING_DIR = BUILD_DIR / "applets_root"
IMAGE_PATH = BUILD_DIR / "vfs.bin"
MKIMAGE_SRC = ROOT / "cmake" / "littlefs_mkimage.c"
MKIMAGE_BIN = BUILD_DIR / "littlefs_mkimage"


def log(msg: str) -> None:
    print(msg, flush=True)


def run(cmd: list[str], cwd: Path | None = None) -> bool:
    log("+ " + " ".join(cmd))
    try:
        subprocess.check_call(cmd, cwd=str(cwd) if cwd else None)
        return True
    except subprocess.CalledProcessError as exc:
        log(f"Command failed ({exc.returncode}): {' '.join(cmd)}")
        return False


def parse_sdkconfig() -> dict[str, int]:
    cfg_path = ROOT / "sdkconfig"
    values: dict[str, int] = {}
    if not cfg_path.exists():
        return values
    for line in cfg_path.read_text().splitlines():
        m = re.match(r"(CONFIG_MAGNOLIA_LITTLEFS_[A-Z_]+)=(.+)", line)
        if not m:
            continue
        key, raw = m.group(1), m.group(2)
        if raw.startswith('"'):
            continue
        try:
            values[key] = int(raw, 0)
        except ValueError:
            continue
    return values


def parse_partition_size() -> int:
    csv_path = ROOT / "partitions.csv"
    if not csv_path.exists():
        return 0x80000
    for line in csv_path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) >= 5 and parts[0] == "vfs":
            return int(parts[4], 0)
    return 0x80000


def discover_applets() -> list[Path]:
    applets: list[Path] = []
    for d in sorted(APPLETS_DIR.iterdir()):
        if not d.is_dir():
            continue
        if d.name == "common":
            continue
        cmake = d / "CMakeLists.txt"
        if not cmake.exists():
            continue
        txt = cmake.read_text()
        if "project_elf" not in txt:
            continue
        applets.append(d)
    return applets


def find_output_elf(applet_dir: Path) -> Path:
    build_dir = applet_dir / "build"
    if not build_dir.exists():
        raise FileNotFoundError(f"{applet_dir} build dir missing")
    elfs = [p for p in build_dir.glob("*.elf") if "bootloader" not in p.name and "partition" not in p.name]
    if not elfs:
        # IDF sometimes produces *.app.elf
        elfs = [p for p in build_dir.rglob("*.elf") if "bootloader" not in p.name and "partition" not in p.name]
    if not elfs:
        raise FileNotFoundError(f"No ELF produced in {build_dir}")
    name = applet_dir.name
    elfs.sort()
    for p in elfs:
        if name in p.name:
            return p
    return elfs[0]


def build_mkimage() -> None:
    BUILD_DIR.mkdir(exist_ok=True)
    if MKIMAGE_BIN.exists() and MKIMAGE_BIN.stat().st_mtime >= MKIMAGE_SRC.stat().st_mtime:
        return
    include_dir = ROOT / "main" / "kernel" / "vfs" / "fs" / "littlefs"
    if not run([
        "gcc",
        "-O2",
        f"-I{include_dir}",
        str(MKIMAGE_SRC),
        os.path.join(str(include_dir), "lfs.c"),
        os.path.join(str(include_dir), "lfs_util.c"),
        "-o",
        str(MKIMAGE_BIN),
    ]):
        raise SystemExit("Failed to build littlefs_mkimage")


def build_applets() -> None:
    applets = discover_applets()
    if not applets:
        log("No applets found.")
        return

    STAGING_DIR.mkdir(parents=True, exist_ok=True)
    for applet in applets:
        log(f"Building applet {applet.name} ...")
        if not run(["idf.py", "build"], cwd=applet):
            log(f"Skipping failed applet build: {applet.name}")
            continue
        try:
            elf_path = find_output_elf(applet)
        except FileNotFoundError as exc:
            log(str(exc))
            continue
        out_path = STAGING_DIR / applet.name
        shutil.copy2(elf_path, out_path)
        log(f"Applet {applet.name} OK -> {out_path}")


def create_image() -> None:
    build_mkimage()
    cfg = parse_sdkconfig()
    size = parse_partition_size()

    args = [
        str(MKIMAGE_BIN),
        "create",
        str(STAGING_DIR),
        str(IMAGE_PATH),
        f"--size={size}",
        f"--block-size={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_BLOCK_SIZE', 512)}",
        f"--block-count={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_BLOCK_COUNT', size // 512)}",
        f"--read-size={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_READ_SIZE', 128)}",
        f"--prog-size={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_PROG_SIZE', 128)}",
        f"--cache-size={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_CACHE_SIZE', 512)}",
        f"--lookahead-size={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_LOOKAHEAD_SIZE', 64)}",
        f"--block-cycles={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_BLOCK_CYCLES', 128)}",
    ]
    if not run(args):
        raise SystemExit("Failed to create LittleFS image")
    log(f"LittleFS image OK size={IMAGE_PATH.stat().st_size} -> {IMAGE_PATH}")


def flash_image() -> None:
    # Best-effort flashing using esptool.py if available.
    port = os.environ.get("ESPPORT")
    if not port:
        log("ESPPORT not set; skipping flash. Run: ESPPORT=/dev/ttyUSB0 make flash-vfs")
        return
    offset = None
    for line in (ROOT / "partitions.csv").read_text().splitlines():
        line = line.strip()
        if line.startswith("#") or not line:
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) >= 4 and parts[0] == "vfs":
            offset = int(parts[3], 0)
            break
    if offset is None:
        log("Failed to find vfs partition offset; skipping flash.")
        return
    if not run(["esptool.py", "-p", port, "write_flash", hex(offset), str(IMAGE_PATH)]):
        log("esptool.py failed to flash partition")
        return
    log("flash vfs partition OK")


def main() -> int:
    if len(sys.argv) < 2:
        log(__doc__)
        return 2
    cmd = sys.argv[1]
    if cmd == "build":
        build_applets()
        create_image()
        return 0
    if cmd == "flash":
        if not IMAGE_PATH.exists():
            build_applets()
            create_image()
        flash_image()
        return 0
    log(__doc__)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
