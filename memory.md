## Codex Intercall Memory
- Updated `applets/mainline/cat` to implement BSD cat flags (-b/-e/-n/-s/-t/-u/-v) with line numbering and robust I/O/error handling.
- Updated `applets/mainline/echo` and `applets/go/echo` to match BSD echo behavior (only leading `-n`, escape processing by default, `\c` stop handling, robust writes).

- Updated mainline true/false to ignore arguments and return BSD-compatible exit codes.
- Updated mainline and TinyGo sleep to BSD semantics (single seconds operand, BSD-style errors, safe time_t conversion).
