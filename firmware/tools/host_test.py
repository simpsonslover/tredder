"""
Build and run the wallet-core unit tests on the PC (no Pico SDK needed).

    python firmware/tools/host_test.py            # build + run
    python firmware/tools/host_test.py --clean    # rebuild everything

Uses MSYS2 MinGW-w64 GCC (C:\\msys64\\mingw64\\bin\\gcc.exe) or any `gcc` on PATH.
The same compile-time configuration is used by the RP2350 build (CMakeLists.txt):
keep the two DEFINES lists in sync.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

FW = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TC = os.path.join(FW, "third_party", "trezor-crypto")
SECP = os.path.join(FW, "third_party", "secp256k1")
BUILD = os.path.join(FW, "build-host")

# Security-relevant configuration (mirrored in CMakeLists.txt)
DEFINES = [
    "USE_BIP32_CACHE=0",       # no private nodes cached in RAM
    "USE_BIP39_CACHE=0",       # no mnemonic->seed cache in RAM
    "USE_ETHEREUM=1", "USE_KECCAK=1",
    "USE_CARDANO=0", "USE_MONERO=0", "USE_NEM=0",
    "USE_PRECOMPUTED_CP=0",
    # libsecp256k1 single-TU build, linked statically
    "SECP256K1_STATIC", "ENABLE_MODULE_EXTRAKEYS=1", "ENABLE_MODULE_SCHNORRSIG=1", "ENABLE_MODULE_RECOVERY=1",
    "ECMULT_WINDOW_SIZE=2", "COMB_BLOCKS=11", "COMB_TEETH=6",
]

TREZOR_SRC = [
    "address.c", "base58.c", "bignum.c", "bip32.c", "bip39.c", "bip39_english.c", "blake256.c",
    "blake2b.c", "blake2s.c", "groestl.c", "ecdsa.c", "curves.c", "secp256k1.c", "nist256p1.c",
    "hasher.c", "hmac.c", "hmac_drbg.c", "rfc6979.c", "memzero.c", "consteq.c", "pbkdf2.c",
    "ripemd160.c", "sha2.c", "sha3.c", "segwit_addr.c", "rand.c",
    "ed25519-donna/ed25519.c", "ed25519-donna/ed25519-keccak.c", "ed25519-donna/ed25519-sha3.c",
    "ed25519-donna/curve25519-donna-32bit.c", "ed25519-donna/curve25519-donna-helpers.c",
    "ed25519-donna/curve25519-donna-scalarmult-base.c", "ed25519-donna/modm-donna-32bit.c",
    "ed25519-donna/ed25519-donna-basepoint-table.c", "ed25519-donna/ed25519-donna-32bit-tables.c",
    "ed25519-donna/ed25519-donna-impl-base.c",
]
SECP_SRC = ["src/secp256k1.c", "src/precomputed_ecmult.c", "src/precomputed_ecmult_gen.c"]


def find_gcc():
    for cand in (r"C:\msys64\mingw64\bin\gcc.exe", r"C:\msys64\ucrt64\bin\gcc.exe", shutil.which("gcc")):
        if cand and os.path.exists(cand):
            return cand
    sys.exit("gcc not found: install MSYS2 MinGW-w64 gcc (pacman -S mingw-w64-x86_64-gcc)")


def compile_one(gcc, src, obj, includes, extra):
    if os.path.exists(obj) and os.path.getmtime(obj) >= os.path.getmtime(src):
        return
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    cmd = [gcc, "-c", "-O2", "-std=c11", src, "-o", obj] + [f"-I{i}" for i in includes] + \
          [f"-D{d}" for d in DEFINES] + extra
    r = subprocess.run(cmd, capture_output=True, text=True, env=_env(gcc))
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit(f"compile failed: {src}")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)


def _env(gcc):
    env = dict(os.environ)
    env["PATH"] = os.path.dirname(gcc) + os.pathsep + env.get("PATH", "")
    return env


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true")
    ap.add_argument("--export-mock", metavar="OUT_JSON",
                    help="also build tools/mock_export.c and write the app's mock-device data")
    args = ap.parse_args()
    if args.clean and os.path.isdir(BUILD):
        shutil.rmtree(BUILD)
    gcc = find_gcc()
    core_inc = [os.path.join(FW, "core", "include"), TC, os.path.join(SECP, "include")]
    objs = []
    # Third-party code: build as-is, warnings off (not ours to edit).
    for rel in TREZOR_SRC:
        src = os.path.join(TC, rel)
        obj = os.path.join(BUILD, "tc", rel.replace("/", "_") + ".o")
        compile_one(gcc, src, obj, [TC], ["-w"])
        objs.append(obj)
    for rel in SECP_SRC:
        src = os.path.join(SECP, rel)
        obj = os.path.join(BUILD, "secp", os.path.basename(rel) + ".o")
        compile_one(gcc, src, obj, [os.path.join(SECP, "include"), os.path.join(SECP, "src")], ["-w"])
        objs.append(obj)
    # Our code: strict warnings, warnings are errors.
    strict = ["-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wformat=2", "-Wvla"]
    ours = sorted(glob.glob(os.path.join(FW, "core", "src", "*.c"))) + \
        sorted(glob.glob(os.path.join(FW, "tests", "host", "*.c")))
    for src in ours:
        obj = os.path.join(BUILD, "ours", os.path.basename(src) + ".o")
        compile_one(gcc, src, obj, core_inc + [os.path.join(FW, "tests", "host")], strict)
        objs.append(obj)
    exe = os.path.join(BUILD, "wallet_tests.exe")
    r = subprocess.run([gcc, "-o", exe] + objs, capture_output=True, text=True, env=_env(gcc))
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("link failed")
    r = subprocess.run([exe], env=_env(gcc))
    if r.returncode or not args.export_mock:
        sys.exit(r.returncode)

    # Mock-device export: core + libs + test platform hooks, its own main().
    src = os.path.join(FW, "tools", "mock_export.c")
    obj = os.path.join(BUILD, "ours", "mock_export.c.o")
    compile_one(gcc, src, obj, core_inc, strict)
    lib_objs = [o for o in objs if not o.endswith("test_main.c.o")]
    mexe = os.path.join(BUILD, "mock_export.exe")
    r = subprocess.run([gcc, "-o", mexe, obj] + lib_objs, capture_output=True, text=True, env=_env(gcc))
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("mock_export link failed")
    out = os.path.abspath(args.export_mock)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    r = subprocess.run([mexe, out], env=_env(gcc))
    print(f"mock device data -> {out}" if r.returncode == 0 else "mock export FAILED")
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
