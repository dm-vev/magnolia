Magnolia /flash

This is a LittleFS partition mounted via VFS as `/flash` (read/write).

Examples:
  ls /flash
  df
  echo Hello > /flash/test.txt
  echo world >> /flash/test.txt
  cat /flash/test.txt

Applets live in `/bin`:
  ls /bin
  zighello
  zigdemo
  zigtest
  rshello
  gohello
  elftest
