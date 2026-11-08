"""
PC tests for the microSD file layer (app/fs/sdfs.c + third_party/fatfs) on a RAM card:
FAT16/FAT32/exFAT layouts, file operations, error cases and randomly corrupted cards.

    python firmware/tools/fs_test.py            # default 3000 corrupted cards per filesystem
    python firmware/tools/fs_test.py 20000      # more

Same FatFs configuration as the device (app/fs/ffconf.h) except FF_USE_MKFS=1, which the test
needs to build card images; the device build has no formatter.
"""
import os
import subprocess
import sys

from host_test import find_gcc, _env

FW = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BUILD = os.path.join(FW, "build-host", "fs")


def main():
    gcc = find_gcc()
    os.makedirs(BUILD, exist_ok=True)
    inc = [os.path.join(FW, d) for d in ("app/include", "app/fs", "third_party/fatfs", "tests/fs")]
    strict = ["-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wvla"]
    units = [
        ("third_party/fatfs/ff.c", ["-w"]),
        ("third_party/fatfs/ffunicode.c", ["-w"]),
        ("app/fs/sdfs.c", strict),
        ("tests/fs/ram_sd.c", strict),
        ("tests/fs/test_fs.c", strict + ["-Wno-parentheses"]),
    ]
    objs = []
    for rel, flags in units:
        obj = os.path.join(BUILD, os.path.basename(rel) + ".o")
        cmd = [gcc, "-c", "-O2", "-std=c11", "-DFF_USE_MKFS=1", os.path.join(FW, rel), "-o", obj] + \
              [f"-I{i}" for i in inc] + flags
        r = subprocess.run(cmd, capture_output=True, text=True, env=_env(gcc))
        if r.returncode:
            sys.stderr.write(r.stdout + r.stderr)
            sys.exit(f"compile failed: {rel}")
        objs.append(obj)
    exe = os.path.join(BUILD, "fs_tests.exe")
    r = subprocess.run([gcc, "-o", exe] + objs, capture_output=True, text=True, env=_env(gcc))
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        sys.exit("link failed")
    sys.exit(subprocess.run([exe] + sys.argv[1:], env=_env(gcc)).returncode)


if __name__ == "__main__":
    main()
