#!/usr/bin/env python3
"""Unpack the vendor ramdisk of a vendor_boot v4 image into a directory.

usage: unpack_vendor_boot.py vendor_boot.img outdir
Handles the lz4 legacy and gzip ramdisk formats Android uses; no deps.
"""
import gzip
import os
import struct
import sys

LZ4_LEGACY = 0x184C2102


def lz4_block(src, out):
    i = 0
    while i < len(src):
        token = src[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        out += src[i:i + lit]
        i += lit
        if i >= len(src):
            break
        off = src[i] | src[i + 1] << 8
        i += 2
        mlen = token & 15
        if mlen == 15:
            while True:
                b = src[i]
                i += 1
                mlen += b
                if b != 255:
                    break
        mlen += 4
        start = len(out) - off
        for k in range(mlen):          # overlapping copies are legal
            out.append(out[start + k])


def lz4_legacy(data):
    out = bytearray()
    i = 4
    while i + 4 <= len(data):
        size, = struct.unpack_from("<I", data, i)
        if size == LZ4_LEGACY:          # concatenated frames
            i += 4
            continue
        i += 4
        if size == 0 or i + size > len(data):
            break
        lz4_block(data[i:i + size], out)
        i += size
    return bytes(out)


def decompress(blob):
    if struct.unpack_from("<I", blob)[0] == LZ4_LEGACY:
        return lz4_legacy(blob)
    if blob[:2] == b"\x1f\x8b":
        return gzip.decompress(blob)
    return blob


def extract_cpio(data, outdir):
    i = 0
    while i + 110 <= len(data):
        if data[i:i + 6] != b"070701":
            break
        f = [int(data[i + 6 + k * 8:i + 14 + k * 8], 16) for k in range(13)]
        mode, filesize, namesize = f[1], f[6], f[11]
        name = data[i + 110:i + 110 + namesize - 1].decode()
        i += 110 + namesize
        i += -i % 4
        body = data[i:i + filesize]
        i += filesize
        i += -i % 4
        if name == "TRAILER!!!":
            # Several cpio archives may be concatenated (fragments).
            while i < len(data) and data[i] == 0:
                i += 1
            continue
        path = os.path.join(outdir, name)
        if mode & 0o170000 == 0o040000:
            os.makedirs(path, exist_ok=True)
        elif mode & 0o170000 == 0o100000:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "wb").write(body)


def main():
    img, outdir = sys.argv[1], sys.argv[2]
    d = open(img, "rb").read()
    assert d[:8] == b"VNDRBOOT"
    page, = struct.unpack_from("<I", d, 12)
    rd_size, = struct.unpack_from("<I", d, 24)
    hdr_size, = struct.unpack_from("<I", d, 8 + 4 * 4 + 4 + 2048 + 4 + 16)
    rd_off = (hdr_size + page - 1) // page * page
    cpio = decompress(d[rd_off:rd_off + rd_size])
    os.makedirs(outdir, exist_ok=True)
    extract_cpio(cpio, outdir)
    print(f"ramdisk {rd_size} -> {len(cpio)} bytes, extracted to {outdir}")


if __name__ == "__main__":
    main()
