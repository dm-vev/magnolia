## uname (TinyGo) ELF applet

TinyGo Magnolia applet scaffolded from `tools/new_tinygo_applet.py`.

### Build

```bash
source esp-idf/export.sh
cd applets/go/uname
idf.py set-target esp32s3
idf.py elf
```

Result: `applets/go/uname/build/uname.app.elf`
