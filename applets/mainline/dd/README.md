BSD reference: FreeBSD dd(1)

Behavior summary
- Supports BSD-style operands: if=, of=, ibs=, obs=, bs=, count=, skip=, seek=.
- Implements conv=noerror,sync,notrunc and status=none for quiet mode.
- Reports read/write/seek failures with BSD-style prefixes and exits non-zero on errors.

Known edge cases
- Does not implement conversion flags beyond noerror/sync/notrunc.
- Uses buffered reads/writes, so errors from delayed storage may surface on close.
- Does not report byte counts (only record counts) and omits throughput statistics.
