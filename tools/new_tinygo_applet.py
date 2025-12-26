#!/usr/bin/env python3
"""
Create a Magnolia TinyGo applet skeleton.

Usage:
  python tools/new_tinygo_applet.py <name> [--add-to-targets]

This generates:
  applets/<name>/
    CMakeLists.txt
    README.md
    main/CMakeLists.txt
    main/idf_component.yml
    tinygo/go.mod
    tinygo/main.go

The output applet uses the shared SDK in `sdk/tinygo/`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APPLETS_DIR = ROOT / "applets"
SDK_REL = "../../sdk/tinygo/cmake/MagnoliaTinyGo.cmake"
SDK_REL_MAIN = "../../../sdk/tinygo/cmake/MagnoliaTinyGo.cmake"


def die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    raise SystemExit(2)


def check_name(name: str) -> None:
    if "/" in name or "\\" in name:
        die("name must be a simple directory name (no slashes)")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", name):
        die("name must match: [A-Za-z0-9][A-Za-z0-9_-]*")


def write(path: Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        die("missing <name>")

    name = argv[1].strip()
    add_targets = "--add-to-targets" in argv[2:]

    check_name(name)

    out_dir = APPLETS_DIR / name
    if out_dir.exists():
        die(f"{out_dir} already exists")

    write(
        out_dir / "CMakeLists.txt",
        f"""cmake_minimum_required(VERSION 3.5)

# Force the applet to build for ESP32-S3 regardless of the user's global IDF target.
set(IDF_TARGET "esp32s3")

include($ENV{{IDF_PATH}}/tools/cmake/project.cmake)
project({name})

include(${{CMAKE_CURRENT_LIST_DIR}}/{SDK_REL})
magnolia_tinygo_enable_text_relocs()

include(${{CMAKE_CURRENT_LIST_DIR}}/../../managed_components/espressif__elf_loader/elf_loader.cmake)
project_elf({name})
""",
    )

    write(
        out_dir / "README.md",
        f"""## {name} (TinyGo) ELF applet

TinyGo Magnolia applet scaffolded from `tools/new_tinygo_applet.py`.

### Build

```bash
source esp-idf/export.sh
cd applets/{name}
idf.py set-target esp32s3
idf.py elf
```

Result: `applets/{name}/build/{name}.app.elf`
""",
    )

    write(
        out_dir / "main" / "CMakeLists.txt",
        f"""include(${{CMAKE_CURRENT_LIST_DIR}}/{SDK_REL_MAIN})

idf_component_register(SRCS ${{MAGNOLIA_TINYGO_RUNTIME_SYMBOLS}})

magnolia_tinygo_add_object(
    TARGET ${{COMPONENT_LIB}}
    NAME   {name}
    GO_DIR ${{CMAKE_CURRENT_LIST_DIR}}/../tinygo
)
""",
    )

    write(
        out_dir / "main" / "idf_component.yml",
        """dependencies:
  elf_loader:
    version: 1.*
""",
    )

    write(
        out_dir / "tinygo" / "go.mod",
        f"""module magnolia/{name}

go 1.22

require magnolia/tinygo v0.0.0

replace magnolia/tinygo => ../../../sdk/tinygo
""",
    )

    write(
        out_dir / "tinygo" / "main.go",
        f"""package main

import "C"

import "magnolia/tinygo"

//export app_main
func app_main(argc C.int, argv **C.char) C.int {{
    _, _ = magnolia.WriteString(magnolia.Stdout, "{name}: hello from TinyGo\\n")
    return 0
}}

func main() {{}}
""",
    )

    if add_targets:
        targets = APPLETS_DIR / "ELF_TARGETS.txt"
        if not targets.exists():
            die(f"missing {targets}")
        lines = targets.read_text().splitlines()
        if name not in {ln.strip() for ln in lines if ln.strip() and not ln.lstrip().startswith("#")}:
            lines.append(name)
            targets.write_text("\n".join(lines) + "\n")

    print(f"OK: created {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

