# dd (Rust) applet

## Behavior
- Implements BSD dd(1) argument parsing for bs/ibs/obs/count/skip/seek and conv/status.
- Honors if=/of= and default stdin/stdout streams.

## BSD reference
- FreeBSD 14.0: dd(1)

## Edge cases
- Large skip/seek values that do not fit in off_t fall back to read-and-discard
  or zero-fill loops instead of seek.
- Short reads during skip stop the copy loop, matching EOF behavior.
- Input read errors with conv=noerror keep processing, and conv=sync pads the
  block with zeros as in FreeBSD dd(1).
