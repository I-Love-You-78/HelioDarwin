#!/usr/bin/env python3
"""Build the loader test stub and wrap it in a kernel-shaped Mach-O.

usage: mkstub.py OUT.macho

Layout: one __TEXT segment at the kernelcache base VA, Mach-O header in its
first 16K page, stub code from offset 0x4000, LC_UNIXTHREAD pc at the code.
That is all tools/mkpayload.py reads from a real kernel.
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
ZIG = [sys.executable, "-m", "ziglang"]
CFLAGS = ["-target", "aarch64-freestanding-none", "-mcpu=cortex_a55+el3", "-O2",
          "-ffreestanding", "-nostdlib", "-fno-pic", "-fno-pie",
          "-mgeneral-regs-only", "-mstrict-align", "-fno-stack-protector",
          "-fno-sanitize=all", "-Wall", "-Wextra"]

VA_TEXT = 0xFFFFFFF007004000          # kernelcache __TEXT base
CODE_OFF = 0x4000
PAGE = 0x4000
MH_MAGIC_64, CPU_TYPE_ARM64, MH_EXECUTE = 0xFEEDFACF, 0x0100000C, 2
LC_SEGMENT_64, LC_UNIXTHREAD = 0x19, 0x5
ARM_THREAD_STATE64, ARM_THREAD_STATE64_COUNT = 6, 68


def run(cmd):
    subprocess.run(cmd, check=True, cwd=HERE)


def build_code():
    os.makedirs(OUT, exist_ok=True)
    objs = []
    for src in ("entry.S", "stub.c"):
        obj = os.path.join("out", os.path.splitext(src)[0] + ".o")
        run(ZIG + ["cc"] + CFLAGS + ["-c", src, "-o", obj])
        objs.append(obj)
    run(ZIG + ["ld.lld", "-T", "link.ld", "--build-id=none", "-static"] + objs
        + ["-o", os.path.join("out", "stub.elf")])
    run(ZIG + ["objcopy", "-O", "binary", os.path.join("out", "stub.elf"),
               os.path.join("out", "stub.bin")])
    return open(os.path.join(OUT, "stub.bin"), "rb").read()


def macho(code):
    size = CODE_OFF + len(code)
    vmsize = (size + PAGE - 1) & ~(PAGE - 1)
    seg = struct.pack("<II16sQQQQiiII", LC_SEGMENT_64, 72, b"__TEXT", VA_TEXT, vmsize,
                      0, size, 5, 5, 0, 0)
    state = [0] * 33                   # x0..x28, fp, lr, sp, pc
    state[32] = VA_TEXT + CODE_OFF
    thread = struct.pack("<IIII", LC_UNIXTHREAD, 16 + ARM_THREAD_STATE64_COUNT * 4,
                         ARM_THREAD_STATE64, ARM_THREAD_STATE64_COUNT)
    thread += struct.pack("<33QII", *state, 0, 0)
    cmds = seg + thread
    hdr = struct.pack("<8I", MH_MAGIC_64, CPU_TYPE_ARM64, 0, MH_EXECUTE, 2, len(cmds), 0, 0)
    blob = hdr + cmds
    assert len(blob) <= CODE_OFF
    return blob + b"\0" * (CODE_OFF - len(blob)) + code


def main():
    out = macho(build_code())
    open(sys.argv[1], "wb").write(out)
    print(f"{sys.argv[1]}: {len(out)} bytes, entry 0x{VA_TEXT + CODE_OFF:x}")


if __name__ == "__main__":
    main()
