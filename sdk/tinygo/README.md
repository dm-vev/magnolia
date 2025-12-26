# Magnolia TinyGo SDK (ELF applets)

This SDK provides:

- a small Go package (`magnolia/tinygo`) with Magnolia-friendly wrappers around the exported POSIX-ish kernel symbols (`open/read/write/...`) and helpers for `argc/argv`;
- CMake helpers for building TinyGo code into Magnolia ELF applets (see `sdk/tinygo/cmake/MagnoliaTinyGo.cmake`).

It is designed for Magnolia applets (ET_DYN) loaded by the kernel ELF loader, not for full ESP32 firmware.

## Prereqs

- ESP-IDF environment (`source esp-idf/export.sh`)
- TinyGo (`tinygo version`)

## Typical applet layout

```
applets/<name>/
  CMakeLists.txt          # calls `project_elf(<name>)`
  main/
    CMakeLists.txt        # builds tinygo object and adds it to `main` component
  tinygo/
    go.mod
    *.go                  # your applet code
```

## Go entrypoint

The simplest TinyGo applet is a Go-exported `app_main`:

```go
package main

/*
typedef int int32_t;
typedef char char_t;
*/
import "C"

import "magnolia/tinygo"
import "unsafe"

//export app_main
func app_main(argc C.int, argv **C.char) C.int {
	args := magnolia.Args(int32(argc), unsafe.Pointer(argv))
	_, _ = magnolia.WriteString(magnolia.Stdout, "args:\n")
	for _, a := range args {
		_, _ = magnolia.WriteString(magnolia.Stdout, a+"\n")
	}
	return 0
}

func main() {}
```

## Notes (Xtensa)

TinyGo does not currently emit fully PIC-friendly Xtensa code for Magnolia-style `ET_DYN` applets.
For ESP32-S3 builds, the SDK build helpers:

- post-process the TinyGo object to make `.literal` writable (WAX);
- link the final applet with text relocations enabled (`-Wl,-z,notext`).

## Scaffold a new TinyGo applet

From repo root:

```bash
python tools/new_tinygo_applet.py myapplet --add-to-targets
```
