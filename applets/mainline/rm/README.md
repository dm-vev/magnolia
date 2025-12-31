# rm (Magnolia OS)

BSD reference: FreeBSD rm(1).

Summary
- Remove files or directories.
- Directories require `-r` or `-R` for recursive removal.
- `-f` suppresses prompts and ignores missing operands.
- `-i` prompts before each removal.

Known edge cases
- If stdin is not readable, `-i` behaves as "no" and skips removal.
- With `-f`, paths that vanish during traversal are ignored.
- On ESP_PLATFORM builds, `stat()` is used instead of `lstat()`, so
  symlink handling during traversal may differ from FreeBSD.
