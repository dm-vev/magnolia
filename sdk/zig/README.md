# Magnolia Zig SDK

This folder provides a **Zig SDK for Magnolia ELF applets**:

- a Zig module `magnolia` (`sdk/zig/magnolia.zig`) with typed bindings to the kernel-exported libc/POSIX ABI;
- a small C shim (`sdk/zig/magnolia_shim.c`) for `errno` access (workaround for current Xtensa PIC limitations in Zig);
- a reusable CMake helper (`cmake/magnolia_zig.cmake`) to compile Zig sources into objects included in the applet ELF.

## Quick start

Use the existing applets as references:

- `applets/zighello` — minimal “hello world” using the SDK.
- `applets/zigdemo` — demonstrates argv parsing + basic FS/dir I/O.
- `applets/zigtest` — tiny runtime test runner using the SDK.

Build and pack applets as usual:

```bash
make applets
make flash-vfs PORT=/dev/ttyUSB0
```

## Writing a Zig applet

Zig applets must export:

```zig
pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int
```

Import the SDK:

```zig
const mg = @import("magnolia");
```

Use the provided bindings in `mg.sys`, and the convenience helpers in:

- `mg.io` — `writeAll`, `puts`, etc.
- `mg.args` — argv iterator helpers.
- `mg.fs` / `mg.dir` — small POSIX wrappers using `errno`.
- `mg.testing` — compile-time test list runner (no function-pointer tables).

## Notes / limitations

- Magnolia applets are **ET_DYN** and are dynamically resolved against the kernel’s exported symbol table (see `main/kernel/core/elf/m_elf_symbol.c`).
- Current Xtensa Zig `-fPIC` support is still limited in practice. By default `cmake/magnolia_zig.cmake` builds Zig objects **without** `-fPIC` and Zig applet projects enable text relocations via `-Wl,-z,notext` (see `applets/zighello/CMakeLists.txt`).
- If you want to experiment with Zig PIC, set `MAGNOLIA_ZIG_PIC=ON` in the applet build, but expect stricter limitations.

## Helper tool

`tools/zig_applet.py` can generate a new Zig applet skeleton:

```bash
python3 tools/zig_applet.py new myzigtool
```

and can run a host-side compilation smoke check:

```bash
python3 tools/zig_applet.py smoke applets/zighello/main/zighello.zig
```
