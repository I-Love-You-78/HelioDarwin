#!/usr/bin/env python3
"""Build the MT6789 shim into out/shim-boot.img (boot image header v4).

Toolchain: Zig from PyPI (`pip install ziglang`), used as clang + lld.
The DTB and vendor ramdisk come from vendor_boot of the active slot, so the
boot image only carries our Image and an empty ramdisk.
"""
import gzip
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
ZIG = [sys.executable, "-m", "ziglang"]
CFLAGS = [
    "-target", "aarch64-freestanding-none", "-mcpu=cortex_a55+el3", "-O2",
    "-ffreestanding",
    "-nostdlib", "-fno-pic", "-fno-pie", "-mgeneral-regs-only",
    "-mstrict-align", "-fno-stack-protector", "-fno-sanitize=all",
    "-Wall", "-Wextra",
]
SOURCES = ["start.S", "main.c", "plog.c"]
C_OBJECTS = ["main.o", "plog.o"]

PAGE = 4096
R_AARCH64_ABS64 = 257
SHT_RELA = 4
SHF_ALLOC = 0x2


def run(cmd):
    print(" ".join(cmd[2:] if cmd[:2] == ZIG[:2] else cmd))
    subprocess.run(cmd, check=True, cwd=HERE)


def abs64_relocs(path):
    """Count R_AARCH64_ABS64 relocations against loaded (SHF_ALLOC)
    sections of an ELF64 object; debug info is never loaded."""
    data = open(path, "rb").read()
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)

    def section(i):
        return shoff + i * shentsize

    count = 0
    for i in range(shnum):
        sh = section(i)
        sh_type, = struct.unpack_from("<I", data, sh + 4)
        if sh_type != SHT_RELA:
            continue
        target, = struct.unpack_from("<I", data, sh + 0x2C)
        target_flags, = struct.unpack_from("<Q", data, section(target) + 8)
        if not target_flags & SHF_ALLOC:
            continue
        offset, size = struct.unpack_from("<QQ", data, sh + 0x18)
        for e in range(offset, offset + size, 24):
            info, = struct.unpack_from("<Q", data, e + 8)
            count += (info & 0xFFFFFFFF) == R_AARCH64_ABS64
    return count


def empty_cpio_gz():
    """newc archive holding only the TRAILER!!! entry, gzipped."""
    name = b"TRAILER!!!\0"
    fields = [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(name), 0]
    hdr = b"070701" + b"".join(b"%08X" % f for f in fields)
    entry = hdr + name
    entry += b"\0" * (-len(entry) % 4)
    entry += b"\0" * (-len(entry) % 512)
    return gzip.compress(entry, 9, mtime=0)


def os_version(major, minor, patch, year, month):
    return (major << 14 | minor << 7 | patch) << 11 | (year - 2000) << 4 | month


def boot_img_v4(kernel, ramdisk, cmdline):
    header = struct.pack(
        "<8sIIII4II1536sI",
        b"ANDROID!",
        len(kernel),
        len(ramdisk),
        os_version(16, 0, 0, 2025, 12),
        1584,               # header_size of v4
        0, 0, 0, 0,         # reserved
        4,                  # header_version
        cmdline.encode().ljust(1536, b"\0"),
        0,                  # signature_size
    )

    def pad(b):
        return b + b"\0" * (-len(b) % PAGE)

    return pad(header) + pad(kernel) + pad(ramdisk)


def main():
    os.makedirs(OUT, exist_ok=True)
    objects = []
    for src in SOURCES:
        obj = os.path.join("out", os.path.splitext(src)[0] + ".o")
        run(ZIG + ["cc"] + CFLAGS + ["-c", src, "-o", obj])
        objects.append(obj)

    # The Image runs wherever LK puts it with no relocation pass: only the
    # header's image_size (start.o, linked at 0) may be an absolute address.
    for obj in C_OBJECTS:
        n = abs64_relocs(os.path.join(OUT, obj))
        if n:
            sys.exit(f"error: {n} absolute relocations in {obj}")

    elf = os.path.join("out", "shim.elf")
    # Zig's own linker ignores parts of link.ld; call lld directly.
    run(ZIG + ["ld.lld", "-T", "link.ld", "--build-id=none", "-static"]
        + objects + ["-o", elf])
    image = os.path.join("out", "Image")
    run(ZIG + ["objcopy", "-O", "binary", elf, image])

    # Pad to the header's image_size so the stack exists as zeroed bytes.
    data = open(os.path.join(HERE, image), "rb").read()
    image_size, = struct.unpack_from("<Q", data, 16)
    if data[56:60] != b"ARM\x64" or image_size < len(data):
        sys.exit("error: bad Image header")
    data += b"\0" * (image_size - len(data))
    open(os.path.join(HERE, image), "wb").write(data)

    # The X676C LK always gunzips the kernel (stock boot_a carries Image.gz).
    boot = boot_img_v4(gzip.compress(data, 9, mtime=0), empty_cpio_gz(),
                       "console=null")
    out = os.path.join(OUT, "shim-boot.img")
    open(out, "wb").write(boot)
    print(f"Image {len(data)} bytes, shim-boot.img {len(boot)} bytes")


if __name__ == "__main__":
    main()
