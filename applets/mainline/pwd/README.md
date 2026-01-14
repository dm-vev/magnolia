# pwd applet

BSD reference: FreeBSD `pwd(1)`.

Behavior summary:
- Prints the current working directory followed by a newline.
- `-L` prints the logical path from `PWD` when it refers to the current directory.
- `-P` prints the physical path from `getcwd(3)`, ignoring `PWD`.

Edge cases:
- If `PWD` is not an absolute path or does not match `.`, `-L` falls back to the physical path.
- Write failures on stdout are reported and cause a non-zero exit.
