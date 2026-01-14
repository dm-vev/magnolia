BSD reference: FreeBSD cp(1)

Behavior summary
- Supports -R/-r for recursive directory copy and -f to remove existing destination files.
- Accepts multiple source operands only when the destination is an existing directory.
- Reports errors using the BSD-style prefix and exits non-zero on any failure.

Known edge cases
- Does not preserve metadata (permissions, ownership, timestamps).
- Does not implement BSD options beyond -R/-r and -f.
- Does not avoid TOCTOU races on filesystem changes between stat/open.
