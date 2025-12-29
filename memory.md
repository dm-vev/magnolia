## Codex Intercall Memory
- Updated `applets/mainline/cat` to implement BSD cat flags (-b/-e/-n/-s/-t/-u/-v) with line numbering and robust I/O/error handling.
- Updated `applets/mainline/echo` and `applets/go/echo` to match BSD echo behavior (only leading `-n`, escape processing by default, `\c` stop handling, robust writes).

- Updated mainline true/false to ignore arguments and return BSD-compatible exit codes.
- Updated mainline and TinyGo sleep to BSD semantics (single seconds operand, BSD-style errors, safe time_t conversion).
- Updated `applets/mainline/head` to match BSD head options (-n/-c/-q/-v and legacy -N), headers, and I/O/error handling.
- Added mainline BSD applets `basename`, `dirname`, `rmdir`, and `unlink`.
- Reworked `applets/mainline/tail` to implement BSD tail option parsing (legacy counts, -n/-c, -q/-v, -f/-F, -r), safe binary line/byte handling, and follow-mode behavior with robust I/O.
- Updated `applets/mainline/pwd` to validate `PWD` against the current directory and handle long paths via dynamic getcwd buffers.
