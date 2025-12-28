## gotest (TinyGo) ELF applet

TinyGo-based self-test for the Magnolia TinyGo SDK. It exercises:

- stdout/stderr writes
- `open/read/write/close`
- basic `argc/argv` plumbing

### Build

```bash
source esp-idf/export.sh
cd applets/gotest
idf.py set-target esp32s3
idf.py elf
```

Result: `applets/gotest/build/gotest.app.elf`

