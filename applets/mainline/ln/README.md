# ln (Magnolia OS)

BSD reference: FreeBSD 14.0 `ln(1)`.

This applet creates hard or symbolic links and supports the common BSD
flags for overwrite behavior, symlink handling, and verbosity.

Edge cases:
- `-F` removes existing target directories recursively before creating the link.
- `-h`/`-n` avoid dereferencing an existing target symlink.
- Multiple sources require the destination to be a directory.
