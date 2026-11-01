# libsecp256k1 (vendored)

- Source: https://github.com/bitcoin-core/secp256k1
- Tag: `v0.8.0`
- Commit: `6e2c8bc4ecdc6e71dbe7a368f360d8d453ce435d`
- License: MIT (see `COPYING`)
- Copied: `include/`, `src/` (benchmarks and upstream tests not copied)
- Local changes: **none**.

Used for: BIP340 Schnorr / Taproot key tweaks (`extrakeys`, `schnorrsig`) and,
later, ECDSA signing with recovery id (`recovery`) for EVM. Built as a single
translation unit (`src/secp256k1.c` + precomputed tables), configured by defines
in `firmware/tools/host_test.py` and `firmware/CMakeLists.txt`.
