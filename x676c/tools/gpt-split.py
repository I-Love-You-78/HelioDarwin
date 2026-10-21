#!/usr/bin/env python3
"""Carve an iOS root partition out of the tail of userdata on the X676C.

  gpt-split.py show  PRIMARY BACKUP BLOCKS
  gpt-split.py split PRIMARY BACKUP BLOCKS IOS_GIB OUTDIR

PRIMARY: a dump of the first blocks of the UFS user LU (LU 2, Android sdc),
         starting at LBA 0 (>= 6 blocks).
BACKUP:  a dump of the last blocks of the LU, ending at the last LBA, long
         enough to include the backup entries that begin at LastUsableLBA+1
         (>= 8 blocks here).
BLOCKS:  LU size in 4096-byte device blocks (31240192 for this phone).

The stock table has exactly 64 entries of 128 bytes, all used, so `split`
GROWS it to 128 entries (the X676C boot chain accepts up to 192 / 16 KiB)
and adds "iosroot" (Apple HFS+ type, a fresh random GUID) at array index 64,
right after the last partition. userdata (disk0s63) ends IOS_GIB earlier,
1 MiB aligned; iosroot takes exactly that freed tail, so it stays inside
userdata's old range and cannot overlap any other partition. DiskGUID, the
usable range, PartitionEntryLBA (primary 2, backup LastUsableLBA+1) and every
other entry byte are unchanged; the three header/entry CRC32s are recomputed.

Outputs (each re-parsed and checked before "OK"):
  OUTDIR/primary.bin   header + 4 entry blocks, write at LBA 1 (5 blocks)
  OUTDIR/backup.bin    4 entry blocks + padding + header, write at the LBA in
  OUTDIR/lba.txt       lba.txt (backup ends at the last LBA)
"""
import os
import struct
import sys
import uuid
import zlib

BS = 4096
ALIGN = (1 << 20) // BS                       # 1 MiB, in blocks
NEW_NENTRIES = 128
MIN_USERDATA_GIB = 16
HFSPLUS_TYPE = uuid.UUID("48465300-0000-11aa-aa11-00306543ecac")
HDR = struct.Struct("<8sIIIIQQQQ16sQIII")      # GPT header, 92 bytes
KEYS = "sig rev size crc rsvd self alt first last guid entries nentries esize ecrc".split()


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def parse_header(blk):
    h = dict(zip(KEYS, HDR.unpack_from(blk, 0)))
    assert h["sig"] == b"EFI PART", "no GPT signature"
    assert h["size"] >= HDR.size and h["size"] <= BS, "bad header size"
    raw = bytearray(blk[:h["size"]])
    raw[16:20] = b"\0\0\0\0"
    assert crc(raw) == h["crc"], "header CRC mismatch"
    return h


def parse_entries(raw, h):
    assert len(raw) >= h["nentries"] * h["esize"], "entries buffer too short"
    raw = raw[:h["nentries"] * h["esize"]]
    assert crc(raw) == h["ecrc"], "entries CRC mismatch"
    out = []
    for i in range(h["nentries"]):
        e = raw[i * h["esize"]:(i + 1) * h["esize"]]
        if e[:16] == bytes(16):
            out.append(None)
            continue
        first, last = struct.unpack_from("<QQ", e, 32)
        name = bytes(e[56:128]).decode("utf-16-le").split("\0")[0]
        out.append(dict(raw=bytearray(e), first=first, last=last, name=name))
    return out


def load(primary_path, backup_path, nblocks):
    p = open(primary_path, "rb").read()
    b = open(backup_path, "rb").read()
    assert len(p) % BS == 0 and len(b) % BS == 0, "dumps must be whole blocks"

    ph = parse_header(p[BS:2 * BS])
    assert ph["self"] == 1 and ph["alt"] == nblocks - 1, "primary header LBAs"
    assert ph["esize"] == 128 and ph["entries"] == 2, "unexpected primary layout"
    eblocks = (ph["nentries"] * ph["esize"] + BS - 1) // BS
    pents = parse_entries(p[2 * BS:(2 + eblocks) * BS], ph)

    base = nblocks - len(b) // BS                 # LBA of the first block in `b`
    bh = parse_header(b[-BS:])
    assert bh["self"] == nblocks - 1 and bh["alt"] == 1, "backup header LBAs"
    assert bh["nentries"] == ph["nentries"] and bh["esize"] == 128, "backup mismatch"
    off = (bh["entries"] - base) * BS
    assert 0 <= off and off + eblocks * BS <= len(b), "backup entries not in the dump"
    bents = parse_entries(b[off:off + eblocks * BS], bh)
    assert [e and bytes(e["raw"]) for e in pents] == [e and bytes(e["raw"]) for e in bents], \
        "primary and backup entries differ"
    return ph, bh, pents, eblocks


def show(ph, ents):
    used = [(i, e) for i, e in enumerate(ents) if e]
    print(f"usable {ph['first']}..{ph['last']}, {ph['nentries']} entries, "
          f"{len(used)} used, entries LBA {ph['entries']}")
    for i, e in sorted(used, key=lambda t: t[1]["first"])[-6:]:
        gib = (e["last"] - e["first"] + 1) * BS / 2**30
        print(f"  #{i + 1:3} {e['name']:16} {e['first']:>10}..{e['last']:<10} {gib:8.2f} GiB")


def build_header(h, self_lba, alt_lba, entries_lba, nentries, ecrc):
    f = [h["sig"], h["rev"], h["size"], 0, 0, self_lba, alt_lba, h["first"], h["last"],
         h["guid"], entries_lba, nentries, h["esize"], ecrc]
    raw = bytearray(HDR.pack(*f)) + bytes(h["size"] - HDR.size)
    struct.pack_into("<I", raw, 16, crc(raw))
    return bytes(raw).ljust(BS, b"\0")


def split(ph, bh, ents, nblocks, ios_gib, outdir):
    ud = next(i for i, e in enumerate(ents) if e and e["name"] == "userdata")
    u = ents[ud]
    ios_blocks = ios_gib * (2**30 // BS)
    new_last = (u["last"] + 1 - ios_blocks) // ALIGN * ALIGN - 1   # userdata's new end, 1 MiB aligned
    ios_first, ios_last = new_last + 1, u["last"]
    assert new_last - u["first"] + 1 >= MIN_USERDATA_GIB * (2**30 // BS), "userdata too small"
    assert ios_first > u["first"] and ios_last <= ph["last"], "iosroot out of range"
    assert len(ents) == ph["nentries"] and all(e for e in ents), "stock table must be full 64"
    ents += [None] * (NEW_NENTRIES - len(ents))                   # grow to 128 slots

    struct.pack_into("<Q", u["raw"], 40, new_last)                # EndingLBA
    u["last"] = new_last
    e = bytearray(ph["esize"])
    e[0:16] = HFSPLUS_TYPE.bytes_le
    e[16:32] = uuid.uuid4().bytes_le
    struct.pack_into("<QQQ", e, 32, ios_first, ios_last, 0)
    e[56:56 + 2 * len("iosroot")] = "iosroot".encode("utf-16-le")
    ents[64] = dict(raw=e, first=ios_first, last=ios_last, name="iosroot")

    raw = bytearray(NEW_NENTRIES * ph["esize"])
    for i, ent in enumerate(ents[:NEW_NENTRIES]):
        if ent:
            raw[i * ph["esize"]:(i + 1) * ph["esize"]] = ent["raw"]
    ecrc = crc(raw)
    eblocks = len(raw) // BS                                       # 128 * 128 / 4096 = 4

    p_hdr = build_header(ph, 1, nblocks - 1, 2, NEW_NENTRIES, ecrc)
    b_entries_lba = bh["entries"]                                  # LastUsableLBA + 1 (unchanged)
    b_hdr = build_header(bh, nblocks - 1, 1, b_entries_lba, NEW_NENTRIES, ecrc)

    primary = p_hdr + bytes(raw)                                  # LBA 1 .. 1+eblocks
    pad = (nblocks - 1) - (b_entries_lba + eblocks)               # reserved blocks before backup header
    assert pad >= 0, "backup entries would collide with the backup header"
    backup = bytes(raw) + bytes(pad * BS) + b_hdr                 # b_entries_lba .. nblocks-1

    os.makedirs(outdir, exist_ok=True)
    open(os.path.join(outdir, "primary.bin"), "wb").write(primary)
    open(os.path.join(outdir, "backup.bin"), "wb").write(backup)
    open(os.path.join(outdir, "lba.txt"), "w").write(
        f"primary {1} {len(primary) // BS}\nbackup {b_entries_lba} {len(backup) // BS}\n")

    # self-check: reassemble both regions as dumps and re-run load()
    pchk = bytes(BS) + primary
    bchk = backup
    ph2, _, ents2, _ = load_bytes(pchk, bchk, nblocks)
    changed = [i for i in range(NEW_NENTRIES)
               if (ents[i] is None) != (ents2[i] is None)
               or (ents[i] and bytes(ents[i]["raw"]) != bytes(ents2[i]["raw"]))]
    assert not changed, f"self-check mismatch at {changed}"
    print(f"userdata #{ud + 1}: ..{u['last']}, "
          f"{(u['last'] - u['first'] + 1) * BS / 2**30:.2f} GiB (was "
          f"{(ios_last - u['first'] + 1) * BS / 2**30:.2f})")
    print(f"iosroot  #65: {ios_first}..{ios_last}, {ios_blocks * BS / 2**30:.2f} GiB "
          f"(1 MiB aligned start: {ios_first % ALIGN == 0})")
    print(f"write primary.bin at LBA 1 ({len(primary) // BS} blocks), "
          f"backup.bin at LBA {b_entries_lba} ({len(backup) // BS} blocks)")
    print("OK: re-parsed, CRCs valid, only userdata end changed, iosroot added")


def load_bytes(p, b, nblocks):
    """load() on in-memory dumps, for the self-check."""
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        pp, bp = os.path.join(d, "p"), os.path.join(d, "b")
        open(pp, "wb").write(p)
        open(bp, "wb").write(b)
        return load(pp, bp, nblocks)


def main():
    cmd, primary, backup, nblocks = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    ph, bh, ents, _ = load(primary, backup, nblocks)
    print(f"GPT OK: primary+backup CRCs valid, entries identical, {nblocks} blocks")
    if cmd == "show":
        show(ph, ents)
    elif cmd == "split":
        split(ph, bh, ents, nblocks, int(sys.argv[5]), sys.argv[6])
        show(ph, ents)
    else:
        sys.exit("show | split")


if __name__ == "__main__":
    main()
