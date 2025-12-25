# Magnolia (ESP32-S3)

Magnolia is a small Unix-like environment for ESP32-S3: a FreeRTOS-based kernel, a LittleFS-backed VFS, and user programs shipped as ELF applets in `/bin`.

## ACKNOWLEDGMENTS

Magnolia is inspired by xv6 (a Unix V6-style teaching kernel) and uses ESP-IDF components, including FreeRTOS and the ESP-IDF VFS layer.

## ERROR REPORTS

This repository is not a product: interfaces and ABI may change without backwards compatibility guarantees.

## BUILDING AND RUNNING

### Flashing (hardware)

```bash
source esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

### Build and flash VFS (/flash) + ELF applets

```bash
make applets
make flash-vfs PORT=/dev/ttyUSB0
```

### QEMU (if configured)

```bash
make qemu
```

## WHAT'S INSIDE

- `main/kernel/` — kernel (VFS, ELF loader, job allocator, core services).
- `applets/sh/` — small shell that runs ELF from `/bin` (supports `>` and `>>` for stdout).
- `rootfs/` — filesystem tree packed into a LittleFS image (see `tools/applets.py`).
- `tools/applets.py` — builds ELF applets and packs `rootfs/` into `build/vfs.bin`.

## QUICK DEMO (inside Magnolia)

```sh
ls /bin
ls /flash
cat /flash/README.txt
echo Hello > /flash/test.txt
echo world >> /flash/test.txt
cat /flash/test.txt
zighello
rshello
gohello
```

## CONFIGURATION

`idf.py menuconfig` → `MagnoliaOS Configuration`:
- LittleFS/VFS settings (`/flash` mount and sizes);
- task/memory limits and debug options.
