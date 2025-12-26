#!/usr/bin/env python3
"""
Magnolia Zig applet helper.

Commands:
  tools/zig_applet.py new <name> [--force]
  tools/zig_applet.py smoke <zig_file>... [--pic] [--mcpu esp32s3]

This script is intentionally lightweight and does not depend on ESP-IDF being
activated for the `new` command.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APPLETS_DIR = ROOT / "applets"
SDK_ZIG = ROOT / "sdk" / "zig" / "magnolia.zig"
TOOLCHAIN = ROOT / "tools" / "zig_xtensa_toolchain.py"


def _die(msg: str, code: int = 2) -> None:
    print(msg, file=sys.stderr)
    raise SystemExit(code)


def _check_name(name: str) -> None:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", name):
        _die(f"Invalid applet name: {name!r} (allowed: [A-Za-z0-9][A-Za-z0-9_-]*)")


def _write_file(path: Path, content: str, force: bool) -> None:
    if path.exists() and not force:
        _die(f"Refusing to overwrite existing file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def cmd_new(args: argparse.Namespace) -> None:
    name: str = args.name
    force: bool = args.force
    _check_name(name)

    app_dir = APPLETS_DIR / name
    main_dir = app_dir / "main"

    cmake_top = f"""cmake_minimum_required(VERSION 3.5)

# Force the applet to build for ESP32-S3 regardless of the user's global IDF target.
set(IDF_TARGET "esp32s3")

include($ENV{{IDF_PATH}}/tools/cmake/project.cmake)
project({name})

include(${{CMAKE_CURRENT_LIST_DIR}}/../../managed_components/espressif__elf_loader/elf_loader.cmake)
set(ELF_CFLAGS -Wl,-z,notext -Wl,-z,noexecstack)
project_elf({name})
"""

    cmake_main = f"""idf_component_register(SRCS "dummy.c")

get_filename_component(MAGNOLIA_ROOT "${{CMAKE_CURRENT_LIST_DIR}}/../../.." ABSOLUTE)
include(${{MAGNOLIA_ROOT}}/cmake/magnolia_zig.cmake)

magnolia_zig_add_obj_to_component(
    COMPONENT_LIB ${{COMPONENT_LIB}}
    SRC "${{CMAKE_CURRENT_LIST_DIR}}/{name}.zig"
)
"""

    dummy_c = f"void {name}_dummy(void) {{}}\n"

    idf_yml = """dependencies:
  elf_loader:
    version: 1.*
"""

    zig_main = f"""const mg = @import("magnolia");

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {{
    _ = argc;
    _ = argv;
    mg.io.puts("Hello from {name}!");
    return 0;
}}
"""

    _write_file(app_dir / "CMakeLists.txt", cmake_top, force)
    _write_file(main_dir / "CMakeLists.txt", cmake_main, force)
    _write_file(main_dir / "dummy.c", dummy_c, force)
    _write_file(main_dir / "idf_component.yml", idf_yml, force)
    _write_file(main_dir / f"{name}.zig", zig_main, force)

    print(f"Created Zig applet: {app_dir}")
    print(f"Next: (inside {app_dir}) run `idf.py elf` or from repo root run `make applets`.")


def _zig_path() -> Path:
    try:
        out = subprocess.check_output([sys.executable, str(TOOLCHAIN), "--print-zig"], text=True).strip()
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.returncode)
    p = Path(out)
    if not p.exists():
        _die(f"zig not found at {p}; try running: python3 {TOOLCHAIN}")
    return p


def cmd_smoke(args: argparse.Namespace) -> None:
    zig = _zig_path()
    pic = args.pic
    mcpu = args.mcpu
    sources: list[Path] = [Path(s).resolve() for s in args.sources]

    if not SDK_ZIG.exists():
        _die(f"Missing SDK module: {SDK_ZIG}")

    out_dir = ROOT / "build" / "zig-smoke"
    out_dir.mkdir(parents=True, exist_ok=True)

    for src in sources:
        if not src.exists():
            _die(f"Missing Zig source: {src}")
        out = out_dir / (src.stem + ".o")
        cmd = [
            str(zig),
            "build-obj",
            "-target",
            "xtensa-freestanding-none",
            "-mcpu",
            mcpu,
            "-OReleaseSmall",
            "--dep",
            "magnolia",
            f"-Mroot={src}",
            f"-Mmagnolia={SDK_ZIG}",
            f"-femit-bin={out}",
        ]
        if pic:
            cmd.insert(cmd.index("-OReleaseSmall"), "-fPIC")
        print("+ " + " ".join(cmd))
        subprocess.check_call(cmd, cwd=str(ROOT))

    print(f"OK: compiled {len(sources)} file(s) -> {out_dir}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    ap_new = sub.add_parser("new", help="Create a new Zig applet skeleton under applets/<name>/")
    ap_new.add_argument("name")
    ap_new.add_argument("--force", action="store_true", help="Overwrite existing files")
    ap_new.set_defaults(func=cmd_new)

    ap_smoke = sub.add_parser("smoke", help="Compile Zig sources for Magnolia (host-side smoke check)")
    ap_smoke.add_argument("sources", nargs="+")
    ap_smoke.add_argument("--pic", action="store_true", help="Compile with -fPIC (experimental on Xtensa)")
    ap_smoke.add_argument("--mcpu", default="esp32s3")
    ap_smoke.set_defaults(func=cmd_smoke)

    args = ap.parse_args(argv)
    args.func(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

