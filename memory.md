## Codex Intercall Memory
- Updated `applets/mainline/cat` to implement BSD cat flags (-b/-e/-n/-s/-t/-u/-v) with line numbering and robust I/O/error handling.
- Updated `applets/mainline/echo` and `applets/go/echo` to match BSD echo behavior (only leading `-n`, escape processing by default, `\c` stop handling, robust writes).

- Updated mainline true/false to ignore arguments and return BSD-compatible exit codes.
- Updated mainline and TinyGo sleep to BSD semantics (single seconds operand, BSD-style errors, safe time_t conversion).
- Updated `applets/mainline/head` to match BSD head options (-n/-c/-q/-v and legacy -N), headers, and I/O/error handling.
- Added mainline BSD applets `basename`, `dirname`, `rmdir`, and `unlink`.
- Added mainline BSD applets `wc` and `yes`.
- Reworked `applets/mainline/tail` to implement BSD tail option parsing (legacy counts, -n/-c, -q/-v, -f/-F, -r), safe binary line/byte handling, and follow-mode behavior with robust I/O.
- Updated `applets/mainline/pwd` to validate `PWD` against the current directory and handle long paths via dynamic getcwd buffers.
- Added mainline BSD applets `cmp` and `comm`.
- Updated mainline and Go `uname` to BSD-style options/output ordering, removed GNU-only flags, and aligned nodename/arch/version outputs for cross-distribution parity.
- Updated mainline and Zig `tee` to BSD-compatible option parsing (-a/-i), signal handling, output file semantics, and robust read/write error handling.
- Updated mainline and Rust `mkdir` to match BSD option parsing, symbolic mode handling, and -p directory creation semantics with correct errors and permissions.
- Updated `applets/mainline/rm` to use `lstat` so symlinks are removed directly (no recursive traversal), and to avoid prompting for non-existent paths.
- Added mainline BSD applets `printenv` and `rev`.
- Updated mainline and Zig `cut` to enforce BSD option rules (-b/-c/-f exclusivity, -d/-s/-n validation), improve range parsing and error reporting, and fix field output for unterminated lines with robust I/O handling.
- Updated `applets/mainline/touch` to implement BSD touch option parsing (-A/-a/-c/-f/-h/-m/-r/-t), time parsing, and robust timestamp updates with correct creation semantics.
