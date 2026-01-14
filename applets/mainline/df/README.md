# df (Magnolia OS)

BSD reference: FreeBSD 14.0 `df(1)`.

This applet reports 1K-block usage for the LittleFS root and a synthetic
`/dev` entry when requested. Option handling is intentionally minimal and
matches the existing Magnolia interface.

Edge cases:
- Symlinked directories are not followed during usage traversal to avoid loops.
- `/dev` paths are reported with zero usage and are not traversed.
- If the LittleFS partition metadata is missing, the applet exits with an error.
