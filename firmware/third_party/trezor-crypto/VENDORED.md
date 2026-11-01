# trezor-crypto (vendored)

- Source: https://github.com/trezor/trezor-firmware, directory `crypto/`
- Tag: `core/v2.13.0`
- Commit: `037e506db454691bda188df2803df2fc26c75f36`
- License: MIT (see `LICENSE`)
- Not copied: `tests/`, `fuzzer/`, `tools/`, `gui/`, `setup.py`, `Makefile` (not needed to build)
- Local changes: **none**. Do not edit files here. Configuration goes through
  compiler defines (see `options.h`), and the platform supplies `random_buffer()`/`random32()`.

To update: fetch a newer tagged release, diff it, read the changelog for
security fixes, and re-run `firmware/tools/host_test.py`.
