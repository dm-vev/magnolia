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

This applet uses the shared Magnolia TinyGo SDK (`sdk/tinygo/`).

TinyGo does not currently emit Xtensa PIC, so the build post-processes the TinyGo object to mark the `.literal` section writable (WAX), and links the final applet with text relocations enabled (`-Wl,-z,notext`).

The applet also links a minimal BSS-backed TinyGo heap from `sdk/tinygo/runtime/tinygo_symbols.S`. The heap is intentionally small to stay within Magnolia's job allocator single-allocation limit.
