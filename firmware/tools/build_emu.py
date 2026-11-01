"""
Build the tredder emulator: the real firmware (app/ + core/ + the same vendored
crypto, same security configuration) linked against emulated board hardware
(platform/emu/) into one Windows program.

    python firmware/tools/build_emu.py            # -> firmware/build-emu/tredder-emu.exe
    python firmware/tools/build_emu.py --clean

Needs MSYS2 MinGW-w64 GCC (same as host_test.py). The exe is linked statically,
so it runs without MSYS2 on PATH.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import host_test as ht  # noqa: E402  (shared source lists + DEFINES)

FW = ht.FW
BUILD = os.path.join(FW, "build-emu")
OUT = os.path.join(BUILD, "tredder-emu.exe")

CHACHA = ["chacha20poly1305/chacha20poly1305.c", "chacha20poly1305/chacha_merged.c",
          "chacha20poly1305/poly1305-donna.c", "chacha20poly1305/rfc7539.c"]
EMU_DEFINES = ["WL_DEBUGLINK", 'FW_VERSION="0.2.0-emu"', "WIN32_LEAN_AND_MEAN", "_WIN32_WINNT=0x0601"]
STRICT = ["-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wformat=2", "-Wvla"]


def compile_one(gcc, src, obj, includes, extra, defines=()):
    if os.path.exists(obj) and os.path.getmtime(obj) >= newest_input(src, includes):
        return
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    cmd = [gcc, "-c", "-O2", "-std=c11", src, "-o", obj] + [f"-I{i}" for i in includes] + \
          [f"-D{d}" for d in ht.DEFINES] + [f"-D{d}" for d in defines] + extra
    r = subprocess.run(cmd, capture_output=True, text=True, env=ht._env(gcc))
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit(f"compile failed: {os.path.relpath(src, FW)}")


_hdr_cache = {}


def newest_input(src, includes):
    """mtime of the source or any of our headers (crude but safe dependency tracking)."""
    t = os.path.getmtime(src)
    for inc in includes:
        if inc.startswith(os.path.join(FW, "third_party")):
            continue
        if inc not in _hdr_cache:
            _hdr_cache[inc] = max([os.path.getmtime(h) for h in glob.glob(os.path.join(inc, "**", "*.h"), recursive=True)] or [0])
        t = max(t, _hdr_cache[inc])
    return t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true")
    args = ap.parse_args()
    if args.clean and os.path.isdir(BUILD):
        shutil.rmtree(BUILD)
    gcc = ht.find_gcc()
    objs = []
    for rel in ht.TREZOR_SRC + CHACHA:
        obj = os.path.join(BUILD, "tc", rel.replace("/", "_") + ".o")
        compile_one(gcc, os.path.join(ht.TC, rel), obj, [ht.TC], ["-w"])
        objs.append(obj)
    for rel in ht.SECP_SRC:
        obj = os.path.join(BUILD, "secp", os.path.basename(rel) + ".o")
        compile_one(gcc, os.path.join(ht.SECP, rel), obj, [os.path.join(ht.SECP, "include"), os.path.join(ht.SECP, "src")], ["-w"])
        objs.append(obj)

    core_inc = [os.path.join(FW, "core", "include"), ht.TC, os.path.join(ht.SECP, "include")]
    app_inc = core_inc + [os.path.join(FW, "app", "include")]
    emu_inc = app_inc + [os.path.join(FW, "platform", "emu")]
    groups = [
        ("core", sorted(glob.glob(os.path.join(FW, "core", "src", "*.c"))), core_inc, STRICT),
        ("app", sorted(glob.glob(os.path.join(FW, "app", "src", "*.c"))), app_inc, STRICT),
        ("emu", sorted(glob.glob(os.path.join(FW, "platform", "emu", "*.c"))), emu_inc,
         ["-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wvla", "-Wno-format-truncation"]),
    ]
    for name, srcs, inc, flags in groups:
        for src in srcs:
            obj = os.path.join(BUILD, name, os.path.basename(src) + ".o")
            compile_one(gcc, src, obj, inc, flags, EMU_DEFINES)
            objs.append(obj)

    rc = os.path.join(FW, "platform", "emu", "emu.rc")
    if os.path.exists(rc):
        windres = os.path.join(os.path.dirname(gcc), "windres.exe")
        res = os.path.join(BUILD, "emu.res.o")
        r = subprocess.run([windres, rc, "-O", "coff", "-o", res], capture_output=True, text=True, env=ht._env(gcc),
                           cwd=os.path.dirname(rc))
        if r.returncode:
            sys.stderr.write(r.stdout + r.stderr)
            sys.exit("windres failed")
        objs.append(res)

    r = subprocess.run([gcc, "-o", OUT, "-mwindows", "-static"] + objs +
                       ["-lws2_32", "-lbcrypt", "-lgdi32", "-luser32", "-lwinmm"],
                       capture_output=True, text=True, env=ht._env(gcc))
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("link failed")
    print(f"built {OUT}")


if __name__ == "__main__":
    main()
