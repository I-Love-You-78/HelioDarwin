#!/usr/bin/env python3
"""Write the payload of an IM4P container (the 4th DER element) to a file.

usage: im4p-payload.py IN.im4p OUT

IM4P ::= SEQUENCE { IA5String "IM4P", IA5String type, IA5String description,
OCTET STRING payload, ... }. The payload of DeviceTree.*.im4p is an LZFSE
stream ("bvx2"); decompress it with `lzfse -decode` (apfs-fuse's 3rdparty).
"""
import sys


def der(buf, pos):
    """(tag, content start, content end) of the DER element at pos."""
    tag, n = buf[pos], buf[pos + 1]
    pos += 2
    if n & 0x80:
        k, n = n & 0x7f, 0
        for _ in range(k):
            n = n << 8 | buf[pos]
            pos += 1
    return tag, pos, pos + n


def main():
    buf = open(sys.argv[1], "rb").read()
    tag, pos, end = der(buf, 0)
    assert tag == 0x30, "not a DER SEQUENCE"
    fields = []
    while pos < end and len(fields) < 4:
        tag, start, stop = der(buf, pos)
        fields.append((tag, buf[start:stop]))
        pos = stop
    assert fields[0] == (0x16, b"IM4P"), "not an IM4P"
    assert fields[3][0] == 0x04, "payload is not an OCTET STRING"
    open(sys.argv[2], "wb").write(fields[3][1])
    print(f"{fields[1][1].decode()} {fields[2][1].decode()!r}: {len(fields[3][1])} bytes, "
          f"magic {fields[3][1][:4]!r}")


if __name__ == "__main__":
    main()
