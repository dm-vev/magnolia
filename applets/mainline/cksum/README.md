# cksum (Magnolia OS)

BSD reference: FreeBSD 14.0 `cksum(1)`.

This applet implements the BSD/POSIX CRC-32 checksum and length output format.
Standard input is used when no file operands are provided or when an operand is
`-`.

Edge cases:
- `-` operands are read from standard input in order; stdin is not rewound.
- Very large inputs that overflow the internal length counter report an error.
- Write failures on stdout are treated as fatal errors.
