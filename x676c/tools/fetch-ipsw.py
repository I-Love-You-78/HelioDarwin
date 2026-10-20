#!/usr/bin/env python3
"""Download the iPhone X iOS 16.7.16 (20H392) IPSW from Apple and verify it.

usage: fetch-ipsw.py OUTDIR

Resumes a partial download (HTTP Range), checks SHA-256 against the value
ipsw.me lists for Apple's file. The IPSW is Apple's: keep it out of git.
"""
import hashlib
import os
import sys
import time
import urllib.request

URL = ("https://updates.cdn-apple.com/2025FallFCS/fullrestores/122-77006/"
       "1158E502-D76D-4226-AA06-ED5E30CD1111/iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw")
SIZE = 6171035648
SHA256 = "ece1194f7e233e9109c5790a8d23a4564a10a254e15ec5b09978a343265568fb"
CHUNK = 1 << 20


RETRIES = 100


def fetch(out):
    have = os.path.getsize(out) if os.path.exists(out) else 0
    req = urllib.request.Request(URL, headers={"Range": f"bytes={have}-"} if have else {})
    with urllib.request.urlopen(req, timeout=60) as r, open(out, "ab") as f:
        t0, done, last = time.time(), have, 0.0
        while True:
            b = r.read(CHUNK)
            if not b:
                break
            f.write(b)
            done += len(b)
            now = time.time()
            if now - last > 15:
                rate = (done - have) / max(now - t0, 1) / 1e6
                print(f"{done / 1e9:.2f} / {SIZE / 1e9:.2f} GB, {rate:.1f} MB/s", flush=True)
                last = now


def main():
    out = os.path.join(sys.argv[1], "iPhone10,3_16.7.16_20H392_Restore.ipsw")
    os.makedirs(sys.argv[1], exist_ok=True)
    # Flaky links (proxies, TUN VPNs) drop or stall: resume from where it stopped.
    for attempt in range(RETRIES):
        if os.path.exists(out) and os.path.getsize(out) >= SIZE:
            break
        try:
            fetch(out)
        except OSError as e:
            print(f"attempt {attempt + 1}: {e}; resuming in 10 s", flush=True)
            time.sleep(10)
    if os.path.getsize(out) != SIZE:
        sys.exit(f"size {os.path.getsize(out)} != {SIZE}; run again to resume")
    h = hashlib.sha256()
    with open(out, "rb") as f:
        for b in iter(lambda: f.read(CHUNK * 8), b""):
            h.update(b)
    if h.hexdigest() != SHA256:
        sys.exit(f"SHA-256 mismatch: {h.hexdigest()}")
    print(f"OK {out} (SHA-256 verified)")


if __name__ == "__main__":
    main()
