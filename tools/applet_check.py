#!/usr/bin/env python3
"""
Validate Magnolia ELF applets:
- ET_DYN (shared object)
- entrypoint is non-zero
- relocations use only the kernel-supported types
- all undefined symbols are present in the kernel export table (m_elf_symbol.c)

Usage:
  python3 tools/applet_check.py path/to/foo.app.elf [more.elf ...]
  python3 tools/applet_check.py applets/foo/build
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
KERNEL_SYMS = ROOT / "main" / "kernel" / "core" / "elf" / "m_elf_symbol.c"


def _run(cmd: list[str]) -> str:
    env = dict(os.environ)
    env["LC_ALL"] = "C"
    return subprocess.check_output(cmd, text=True, env=env, stderr=subprocess.STDOUT)


def load_kernel_exports() -> set[str]:
    text = KERNEL_SYMS.read_text(encoding="utf-8", errors="replace")
    start = text.find("static const struct m_elfsym g_kernel_libc_syms[]")
    if start < 0:
        raise RuntimeError(f"Failed to locate g_kernel_libc_syms in {KERNEL_SYMS}")
    block = text[start:]
    end = block.find("M_ELFSYM_END")
    if end < 0:
        raise RuntimeError(f"Failed to locate M_ELFSYM_END in {KERNEL_SYMS}")
    block = block[:end]

    exports: set[str] = set()
    for line in block.splitlines():
        m = re.search(r'M_ELFSYM_EXPORT\(\s*([A-Za-z0-9_]+)\s*\)', line)
        if m:
            exports.add(m.group(1))
        m = re.search(r'\{\s*"([^"]+)"\s*,', line)
        if m:
            exports.add(m.group(1))
    return exports


def expand_inputs(paths: list[str]) -> list[Path]:
    out: list[Path] = []
    for raw in paths:
        p = Path(raw)
        if p.is_dir():
            out.extend(sorted(p.glob("*.app.elf")))
            continue
        out.append(p)
    return out


def parse_header(elf: Path) -> tuple[str, str, int]:
    hdr = _run(["readelf", "-h", str(elf)])
    type_ = ""
    machine = ""
    entry = 0
    for line in hdr.splitlines():
        line = line.strip()
        if line.startswith("Type:"):
            type_ = line.split(":", 1)[1].strip()
        elif line.startswith("Machine:"):
            machine = line.split(":", 1)[1].strip()
        elif line.startswith("Entry point address:"):
            raw = line.split(":", 1)[1].strip()
            try:
                entry = int(raw, 0)
            except ValueError:
                entry = 0
    return type_, machine, entry


def detect_arch(machine: str) -> str:
    if "Xtensa" in machine:
        return "xtensa"
    if "RISC-V" in machine or "RISC V" in machine:
        return "riscv"
    return "unknown"


def parse_reloc_types(elf: Path) -> set[str]:
    out = _run(["readelf", "-r", "-W", str(elf)])
    types: set[str] = set()
    for line in out.splitlines():
        line = line.strip()
        if not line or not re.match(r"^[0-9a-fA-F]+", line):
            continue
        parts = line.split()
        if len(parts) >= 3:
            types.add(parts[2])
    return types


def parse_undefined_symbols(elf: Path) -> set[str]:
    out = _run(["readelf", "-s", "-W", str(elf)])
    undef: set[str] = set()
    for line in out.splitlines():
        line = line.strip()
        m = re.match(
            r"^\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+\S+\s+\S+\s+\S+\s+UND\s+(\S+)\s*$",
            line,
        )
        if not m:
            continue
        name = m.group(1)
        if name:
            undef.add(name)
    return undef


def has_defined_symbol(elf: Path, name: str) -> bool:
    out = _run(["readelf", "-s", "-W", str(elf)])
    for line in out.splitlines():
        line = line.strip()
        if not line.endswith(f" {name}"):
            continue
        if " UND " in f" {line} ":
            continue
        return True
    return False


def check_one(elf: Path, exports: set[str]) -> int:
    errors: list[str] = []
    if not elf.exists():
        return 1

    type_, machine, entry = parse_header(elf)
    arch = detect_arch(machine)

    if "DYN" not in type_:
        errors.append(f"expected ET_DYN; got Type={type_!r}")
    if entry == 0:
        errors.append("entrypoint is 0 (expected -e app_main)")

    reloc = parse_reloc_types(elf)
    if arch == "xtensa":
        allowed = {
            "R_XTENSA_NONE",
            "R_XTENSA_RTLD",
            "R_XTENSA_GLOB_DAT",
            "R_XTENSA_JMP_SLOT",
            "R_XTENSA_RELATIVE",
        }
    elif arch == "riscv":
        allowed = {
            "R_RISCV_NONE",
            "R_RISCV_32",
            "R_RISCV_RELATIVE",
            "R_RISCV_JUMP_SLOT",
        }
    else:
        allowed = set()

    if allowed and not reloc.issubset(allowed):
        bad = sorted(reloc - allowed)
        errors.append(f"unsupported relocation types: {', '.join(bad)}")

    undef = parse_undefined_symbols(elf)
    missing = sorted(s for s in undef if s not in exports)
    if missing:
        errors.append(f"undefined symbols not exported by kernel: {', '.join(missing)}")

    if errors:
        print(f"[FAIL] {elf}", file=sys.stderr)
        print(f"       Machine={machine} Type={type_} Entry=0x{entry:x}", file=sys.stderr)
        for e in errors:
            print(f"       - {e}", file=sys.stderr)
        return 1

    print(f"[OK]   {elf}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+", help="ELF file(s) or build dir(s)")
    args = ap.parse_args()

    exports = load_kernel_exports()
    elfs = expand_inputs(args.paths)
    if not elfs:
        print("No ELF files found.", file=sys.stderr)
        return 2

    rc = 0
    for elf in elfs:
        rc |= check_one(elf, exports)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
