## goargs (TinyGo) ELF applet

Small TinyGo applet that prints its `argv[]` to stdout. Useful for validating the TinyGo SDK argument helpers.

### Build

```bash
source esp-idf/export.sh
cd applets/goargs
idf.py set-target esp32s3
idf.py elf
```

Result: `applets/goargs/build/goargs.app.elf`

