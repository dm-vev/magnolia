# hexdump (Rust) applet

## Behavior
- Implements the BSD hexdump(1) modes: -b, -c, -C, -d, -o, -x, -v, -n, -s.
- Default output is the canonical format (-C).
- Reads stdin when no files are provided or when a file path is '-'.

## BSD reference
- FreeBSD 14.0: hexdump(1)

## Edge cases
- -s uses read-and-discard fallback on non-seekable inputs or when the skip
  count exceeds off_t; offsets still include skipped bytes.
- Non-UTF8 argv entries are ignored because the Rust Args interface exposes
  UTF-8 only.
