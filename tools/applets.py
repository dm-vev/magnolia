#!/usr/bin/env python3
"""
Build Magnolia ELF applets and pack them into LittleFS image for vfs partition.

Usage:
  python tools/applets.py build
  python tools/applets.py flash [--port PORT] [--baud BAUD]  (optional, best-effort)
  python tools/applets.py qemu-image   (merge qemu_flash.bin with vfs.bin)
  python tools/applets.py qemu-inject  (patch build/qemu_flash.bin in-place)

Requires ESP-IDF environment for building applets (idf.py in PATH, IDF_PATH set).
"""

from __future__ import annotations

import os
import re
import shutil
import shlex
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APPLETS_DIR = ROOT / "applets"
BUILD_DIR = ROOT / "build"
ROOTFS_DIR = ROOT / "rootfs"
ROOTFS_BIN_DIR = ROOTFS_DIR / "bin"
IMAGE_PATH = BUILD_DIR / "vfs.bin"
QEMU_FLASH_PATH = BUILD_DIR / "qemu_flash.bin"
QEMU_EFUSE_PATH = BUILD_DIR / "qemu_efuse.bin"
ELF_TARGETS_FILE = APPLETS_DIR / "ELF_TARGETS.txt"
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
    def load_from_file() -> list[Path]:
        if not ELF_TARGETS_FILE.exists():
            return []
        targets: list[Path] = []
        for line in ELF_TARGETS_FILE.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            path = APPLETS_DIR / line
            if path.is_dir():
                targets.append(path)
            else:
                log(f"Skipping missing ELF target directory listed in {ELF_TARGETS_FILE}: {line}")
        return targets

    def detect_project_elf_dirs() -> list[Path]:
        dirs: list[Path] = []
        for d in sorted(APPLETS_DIR.iterdir()):
            if not d.is_dir():
                continue
            if d.name == "common":
                continue
            cmake = d / "CMakeLists.txt"
            if not cmake.exists():
                continue
            if "project_elf" not in cmake.read_text():
                continue
            dirs.append(d)
        return dirs

    explicit = load_from_file()
    implicit = detect_project_elf_dirs()
    seen: set[Path] = set()
    result: list[Path] = []
    for path in explicit + implicit:
        if path in seen:
            continue
        seen.add(path)
        result.append(path)
    return result


def find_output_elf(applet_dir: Path) -> Path:
    build_dir = applet_dir / "build"
    if not build_dir.exists():
        raise FileNotFoundError(f"{applet_dir} build dir missing")
    elfs = [p for p in build_dir.glob("*.app.elf") if "bootloader" not in p.name and "partition" not in p.name]
    if not elfs:
        elfs = [p for p in build_dir.glob("*.elf") if "bootloader" not in p.name and "partition" not in p.name]
    if not elfs:
        # Fallback for generators that place ELFs deeper.
        elfs = [p for p in build_dir.rglob("*.app.elf") if "bootloader" not in p.name and "partition" not in p.name]
    if not elfs:
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
    ROOTFS_BIN_DIR.mkdir(parents=True, exist_ok=True)
    if not applets:
        log("No applets found.")
        return
    for applet in applets:
        log(f"Building applet {applet.name} ...")
        if not run(["idf.py", "elf"], cwd=applet):
            log(f"Elf target failed for {applet.name}; running `idf.py build` instead.")
            if not run(["idf.py", "build"], cwd=applet):
                log(f"Skipping failed applet build: {applet.name}")
                continue
        try:
            elf_path = find_output_elf(applet)
        except FileNotFoundError as exc:
            log(str(exc))
            continue
        out_path = ROOTFS_BIN_DIR / applet.name
        shutil.copy2(elf_path, out_path)
        log(f"Applet {applet.name} OK -> {out_path}")


def create_image() -> None:
    build_mkimage()
    cfg = parse_sdkconfig()
    partition_size = parse_partition_size()

    # Mirror the runtime LittleFS config logic from `littlefs_fs.c`:
    # - block_size must be >= erase size (ESP32 flash is 4096)
    # - the effective block_count may be clamped to partition size
    erase_size = 4096
    block_size = cfg.get("CONFIG_MAGNOLIA_LITTLEFS_BLOCK_SIZE", erase_size)
    if block_size < erase_size or (block_size % erase_size) != 0:
        block_size = erase_size
    max_blocks = partition_size // block_size
    cfg_blocks = cfg.get("CONFIG_MAGNOLIA_LITTLEFS_BLOCK_COUNT", max_blocks)
    if cfg_blocks > 0 and cfg_blocks < max_blocks:
        block_count = cfg_blocks
    else:
        block_count = max_blocks
    fs_size = block_count * block_size

    args = [
        str(MKIMAGE_BIN),
        "create",
        str(ROOTFS_DIR),
        str(IMAGE_PATH),
        f"--block-size={block_size}",
        f"--fs-size={fs_size}",
        f"--read-size={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_READ_SIZE', 128)}",
        f"--prog-size={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_PROG_SIZE', 128)}",
        f"--cache-size={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_CACHE_SIZE', 512)}",
        f"--lookahead-size={cfg.get('CONFIG_MAGNOLIA_LITTLEFS_LOOKAHEAD_SIZE', 64)}",
    ]
    if not run(args):
        raise SystemExit("Failed to create LittleFS image")
    log(f"LittleFS image OK size={IMAGE_PATH.stat().st_size} -> {IMAGE_PATH}")


def flash_image() -> None:
    # Best-effort flashing using esptool.py if available.
    port = None
    baud = None

    # argv overrides env
    extra = sys.argv[2:]
    i = 0
    while i < len(extra):
        arg = extra[i]
        if arg == "--port" and i + 1 < len(extra):
            port = extra[i + 1]
            i += 2
            continue
        if arg.startswith("--port="):
            port = arg.split("=", 1)[1]
            i += 1
            continue
        if arg == "--baud" and i + 1 < len(extra):
            baud = extra[i + 1]
            i += 2
            continue
        if arg.startswith("--baud="):
            baud = arg.split("=", 1)[1]
            i += 1
            continue
        i += 1

    if port is None:
        port = os.environ.get("ESPPORT") or os.environ.get("PORT") or os.environ.get("IDF_PORT")
    if not port:
        log("Port not set; skipping flash. Run: make flash-vfs PORT=/dev/ttyUSB0")
        return

    if baud is None:
        baud = os.environ.get("ESPBAUD") or os.environ.get("BAUD")
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
    cmd = ["esptool.py", "-p", port]
    if baud:
        cmd += ["-b", str(baud)]
    cmd += ["write_flash", hex(offset), str(IMAGE_PATH)]
    if not run(cmd):
        log("esptool.py failed to flash partition")
        return
    log("flash vfs partition OK")

def inject_qemu_flash() -> None:
    if not IMAGE_PATH.exists():
        build_applets()
        create_image()

    if not QEMU_FLASH_PATH.exists():
        raise SystemExit(f"Missing {QEMU_FLASH_PATH}; run `idf.py build` first.")

    # Find partition offset and size from partitions.csv.
    offset = None
    size = None
    for line in (ROOT / "partitions.csv").read_text().splitlines():
        line = line.strip()
        if line.startswith("#") or not line:
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) >= 5 and parts[0] == "vfs":
            offset = int(parts[3], 0)
            size = int(parts[4], 0)
            break
    if offset is None or size is None:
        raise SystemExit("Failed to find vfs partition offset/size in partitions.csv")

    image = IMAGE_PATH.read_bytes()
    if len(image) != size:
        raise SystemExit(f"{IMAGE_PATH} size {len(image)} != vfs partition size {size}")

    with QEMU_FLASH_PATH.open("r+b") as fp:
        fp.seek(0, os.SEEK_END)
        flash_size = fp.tell()
        if flash_size < offset + size:
            raise SystemExit(
                f"{QEMU_FLASH_PATH} too small ({flash_size}); needs at least {offset + size}"
            )
        fp.seek(offset)
        fp.write(image)
        fp.flush()
    log(f"Injected {IMAGE_PATH} -> {QEMU_FLASH_PATH} @0x{offset:x}")

def qemu_merge_flash() -> None:
    if not IMAGE_PATH.exists():
        build_applets()
        create_image()

    flash_args_path = BUILD_DIR / "flash_args"
    if not flash_args_path.exists():
        raise SystemExit(f"Missing {flash_args_path}; run `idf.py build` first.")

    # Find partition offset/size from partitions.csv.
    vfs_offset = None
    vfs_size = None
    for line in (ROOT / "partitions.csv").read_text().splitlines():
        line = line.strip()
        if line.startswith("#") or not line:
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) >= 5 and parts[0] == "vfs":
            vfs_offset = int(parts[3], 0)
            vfs_size = int(parts[4], 0)
            break
    if vfs_offset is None or vfs_size is None:
        raise SystemExit("Failed to find vfs partition offset/size in partitions.csv")

    image = IMAGE_PATH.read_bytes()
    if len(image) != vfs_size:
        raise SystemExit(f"{IMAGE_PATH} size {len(image)} != vfs partition size {vfs_size}")

    lines = [ln.strip() for ln in flash_args_path.read_text().splitlines() if ln.strip()]
    if not lines:
        raise SystemExit(f"{flash_args_path} is empty")
    opts = shlex.split(lines[0])
    pairs: list[str] = []
    for ln in lines[1:]:
        toks = ln.split()
        if len(toks) != 2:
            raise SystemExit(f"Invalid line in {flash_args_path}: {ln}")
        pairs += toks

    flash_size = None
    for i, tok in enumerate(opts):
        if tok in ("--flash_size", "--flash-size") and i + 1 < len(opts):
            flash_size = opts[i + 1]
            break
        if tok.startswith("--flash_size=") or tok.startswith("--flash-size="):
            flash_size = tok.split("=", 1)[1]
            break
    if flash_size is None:
        flash_size = "2MB"

    # Determine IDF target from sdkconfig (best-effort); default to esp32s3.
    idf_target = "esp32s3"
    sdk = (ROOT / "sdkconfig")
    if sdk.exists():
        txt = sdk.read_text()
        if "CONFIG_IDF_TARGET_ESP32=" in txt and "CONFIG_IDF_TARGET_ESP32=y" in txt:
            idf_target = "esp32"
        elif "CONFIG_IDF_TARGET_ESP32S2=y" in txt:
            idf_target = "esp32s2"
        elif "CONFIG_IDF_TARGET_ESP32S3=y" in txt:
            idf_target = "esp32s3"
        elif "CONFIG_IDF_TARGET_ESP32C3=y" in txt:
            idf_target = "esp32c3"
        elif "CONFIG_IDF_TARGET_ESP32C6=y" in txt:
            idf_target = "esp32c6"

    # Ensure efuse image exists (ESP-IDF qemu defaults to 1024 bytes).
    if not QEMU_EFUSE_PATH.exists():
        QEMU_EFUSE_PATH.write_bytes(b"\x00" * 1024)

    cmd = [
        sys.executable,
        "-m",
        "esptool",
        f"--chip={idf_target}",
        "merge_bin",
        f"--output={QEMU_FLASH_PATH}",
        f"--fill-flash-size={flash_size}",
        *opts,
        *pairs,
        hex(vfs_offset),
        str(IMAGE_PATH),
    ]
    if not run(cmd, cwd=BUILD_DIR):
        raise SystemExit("Failed to generate qemu_flash.bin with vfs.bin")
    log(f"Generated {QEMU_FLASH_PATH} including {IMAGE_PATH} @0x{vfs_offset:x}")


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
    if cmd == "qemu":
        inject_qemu_flash()
        return 0
    if cmd == "qemu-inject":
        inject_qemu_flash()
        return 0
    if cmd == "qemu-image":
        qemu_merge_flash()
        return 0
    log(__doc__)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
