# date

BSD reference: FreeBSD date(1).

## Behavior
- Prints the current local time by default using the format "%Y-%m-%d %H:%M:%S".
- With `-u`, prints UTC.
- If the first non-option argument begins with `+`, treats the rest as a `strftime(3)` format string.
- `--help` and `--version` print help/version and exit successfully.

## Edge cases
- A lone `+` results in an empty formatted string, so the applet prints just a newline.
- If the formatted output is very large, the buffer grows up to 1 MiB; beyond that it reports `date: invalid format`.
