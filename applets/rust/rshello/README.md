## rshello (Rust ELF applet)

Builds a Magnolia userland ELF (`ET_DYN`) for `esp32s3` (Xtensa LX7) that prints `Hello world!` to stdout.

### Prereqs

- ESP-IDF environment: `source esp-idf/export.sh`
- Rust Xtensa toolchain via `espup` (once):
  - `cargo install espup --locked`
  - `espup install --targets esp32s3`
  - `source ~/export-esp.sh`

### Build

```bash
source esp-idf/export.sh
cd applets/rshello
idf.py -G 'Unix Makefiles' set-target esp32s3
idf.py -G 'Unix Makefiles' elf
```

Result: `applets/rshello/build/rshello.app.elf`

### Pack into Magnolia rootfs

From repo root:

```bash
make applets
```

This copies the built ELF into `rootfs/bin/rshello` (see `tools/applets.py`).

