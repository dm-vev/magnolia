# yes

BSD reference: FreeBSD `yes(1)`.

Behavior:
- Repeatedly writes the string "y" followed by a newline to stdout.
- If operands are provided, writes all operands joined by single spaces and a trailing newline.

Edge cases:
- Extremely long combined operands are rejected with `EOVERFLOW` to avoid size_t wraparound.
- Write errors on stdout terminate the program with an error message.
