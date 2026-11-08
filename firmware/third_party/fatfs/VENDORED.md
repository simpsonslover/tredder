# FatFs (vendored)

- Source: https://elm-chan.org/fsw/ff/arc/ff16.zip
  (sha256 of the zip: `99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e`)
- Version: R0.16
- License: FatFs license, BSD-style 1-clause (see `LICENSE.txt`)
- Copied: `source/ff.c`, `ff.h`, `diskio.h`, `ffunicode.c`, `00readme.txt`, `00history.txt`
  - ff.c sha256 `dbeae3d7d22dbc070983e31af897fa5e43d29c726a0a240fe9589ffe2ec0ef1e`
- Local changes: **none**. Configuration: `firmware/app/fs/ffconf.h`.

Used for: FAT12/16/32 and exFAT on the microSD card (`app/fs/sdfs.c`, milestone M6).
Tested on the PC with `tools/fs_test.py` (all layouts + randomly corrupted cards).
