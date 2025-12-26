# Magnolia Rust Applet SDK

This SDK lets you write Magnolia userland applets in Rust and build them as PIC-like `ET_DYN` ELF files loaded by Magnolia's kernel ELF loader.

## Prereqs

- ESP-IDF env: `source esp-idf/export.sh`
- Rust Xtensa toolchain via `espup`:
  - `cargo install espup --locked`
  - `espup install --targets esp32s3`
  - `source ~/export-esp.sh`

## What’s included

- `sdk/rust/magnolia-applet-sys/` — raw ABI bindings to Magnolia-exported symbols (via `m_elf_symbol.c`).
- `sdk/rust/magnolia-applet/` — runtime + safe-ish wrappers:
  - `magnolia_applet::entry!(main_fn)` generates `app_main(argc, argv)` entrypoint
  - `magnolia_applet::{print!, println!, eprint!, eprintln!}`
  - `magnolia_applet::fs::File` (fd-based open/read/write)

## Example applets

- `applets/rusthello/` — “hello world” + argv dump
- `applets/rselftest/` — runtime smoke tests (alloc + VFS RW + errno paths)

Build an applet:

```bash
source esp-idf/export.sh
cd applets/rusthello
idf.py -G 'Unix Makefiles' elf
```

Result: `applets/rusthello/build/rusthello.app.elf`

Pack into Magnolia rootfs:

```bash
make applets
```

## ABI checks (host-side)

Validate that an applet is loadable by the Magnolia kernel ELF loader:

```bash
python3 tools/applet_check.py applets/rusthello/build/rusthello.app.elf
```

## Creating a new Rust applet

Copy `applets/rusthello/` and adjust:

- `applets/<name>/CMakeLists.txt` (project name)
- `applets/<name>/rust/Cargo.toml` (crate name)
- `applets/<name>/rust/src/lib.rs` (your code)

The build glue is `cmake/magnolia_rust_applet.cmake` (builds a Rust `staticlib`, applies Xtensa section-flag fixups, links it into `<name>.app.elf`).

