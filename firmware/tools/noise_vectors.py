"""
Build tools/noise_vectors.c with the host GCC, run its self-checks and write the
cross-implementation vectors to protocol/noise_vectors.json (used by the app's jest test).

    python firmware/tools/noise_vectors.py
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import host_test as ht  # noqa: E402

CHACHA = ["chacha20poly1305/chacha20poly1305.c", "chacha20poly1305/chacha_merged.c",
          "chacha20poly1305/poly1305-donna.c", "chacha20poly1305/rfc7539.c"]
OUT = os.path.join(ht.FW, "..", "protocol", "noise_vectors.json")


def main():
    gcc = ht.find_gcc()
    build = os.path.join(ht.FW, "build-host", "noise")
    objs = []
    for rel in ht.TREZOR_SRC + CHACHA:
        obj = os.path.join(build, "tc", rel.replace("/", "_") + ".o")
        ht.compile_one(gcc, os.path.join(ht.TC, rel), obj, [ht.TC], ["-w"])
        objs.append(obj)
    inc = [os.path.join(ht.FW, "core", "include"), os.path.join(ht.FW, "app", "include"), ht.TC]
    strict = ["-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wvla"]
    for src in [os.path.join(ht.FW, "app", "src", "noise.c"), os.path.join(ht.FW, "core", "src", "secure.c"),
                os.path.join(ht.FW, "tests", "host", "test_platform.c"), os.path.join(ht.FW, "tools", "noise_vectors.c")]:
        obj = os.path.join(build, os.path.basename(src) + ".o")
        ht.compile_one(gcc, src, obj, inc + [os.path.join(ht.FW, "tests", "host")], strict)
        objs.append(obj)
    exe = os.path.join(build, "noise_vectors.exe")
    r = subprocess.run([gcc, "-o", exe] + objs, capture_output=True, text=True, env=ht._env(gcc))
    if r.returncode:
        sys.exit(r.stdout + r.stderr)
    r = subprocess.run([exe, os.path.abspath(OUT)], env=ht._env(gcc))
    if r.returncode == 0:
        print(f"vectors -> {os.path.relpath(os.path.abspath(OUT))}")
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
