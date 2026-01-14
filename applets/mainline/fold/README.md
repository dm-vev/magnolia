fold (BSD)
==========

Reference: FreeBSD fold(1).

Behavior
--------
- Wraps input lines to a given width (default 80).
- `-b` counts bytes instead of column positions.
- `-s` folds on the last blank before the width when possible.
- `-w width` sets the column width.

Notes and edge cases
--------------------
- Tab stops are every 8 columns, matching BSD fold.
- Backspace reduces the display column in non-`-b` mode.
- Very long lines saturate internal column tracking at `SIZE_MAX`.
  This avoids size_t wrap but cannot represent columns beyond that.
  Extremely large inputs may still exhaust memory before folding.
