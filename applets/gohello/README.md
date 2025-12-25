## gohello (TinyGo) ELF applet

Build an ESP32-S3 (Xtensa LX7) ELF applet that prints `Hello world!` using `write(1, ...)`.

### Build

```bash
source esp-idf/export.sh
cd applets/gohello
idf.py set-target esp32s3
idf.py elf
```

The resulting applet is `build/gohello.app.elf`.

### Notes

TinyGo does not currently emit Xtensa PIC, so the build post-processes the TinyGo object with `xtensa-esp32s3-elf-objcopy` to mark the `.literal` section writable (WAX), and links the final applet with text relocations enabled (`-Wl,-z,notext`).

This applet also provides a minimal BSS-backed TinyGo heap in `applets/gohello/main/tinygo_symbols.S`. The heap is intentionally small to stay within Magnolia's job allocator single-allocation limit.
