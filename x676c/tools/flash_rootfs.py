#!/usr/bin/env python3
"""Write an explicit HFS+ image to X676C iosroot and verify its full readback.

Usage: flash_rootfs.py IMAGE [--adb EXECUTABLE] [--serial SERIAL]
Android must be running on the successful slot B with root access. This command
writes only iosroot; it does not select a boot slot or reboot the phone.
The compressed input is Base64-framed to preserve binary bytes through Windows
ADB stdin. Success requires remote completion and full-volume SHA-256 readback.
"""
import argparse
import base64
import gzip
import hashlib
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import time

DEVICE = '/dev/block/by-name/iosroot'


class Base64Stream:
    """Keep ADB stdin ASCII-only while preserving one continuous gzip stream."""

    def __init__(self, stream):
        self.stream = stream
        self.tail = b''

    def write(self, data):
        value = self.tail + data
        count = len(value) // 3 * 3
        if count:
            self.stream.write(base64.b64encode(value[:count]))
        self.tail = value[count:]
        return len(data)

    def flush(self):
        self.stream.flush()

    def finish(self):
        if self.tail:
            self.stream.write(base64.b64encode(self.tail))
            self.tail = b''
        self.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--adb', default=os.environ.get('ADB', 'adb'))
    parser.add_argument('--serial', default=os.environ.get('ANDROID_SERIAL'))
    parser.add_argument('--su', default='su', help='Android root executable name or absolute path')
    args = parser.parse_args()
    image = args.image.resolve(strict=True)
    if not image.is_file():
        parser.error('IMAGE must be a regular file')
    stat = image.stat()
    with image.open('rb') as stream:
        stream.seek(1024)
        header = stream.read(512)
    if len(header) != 512 or header[:2] not in (b'H+', b'HX'):
        parser.error('IMAGE must contain an HFS+ volume header at byte 1024')
    block_size, blocks = struct.unpack_from('>II', header, 40)
    if not block_size or not blocks or block_size * blocks != stat.st_size:
        parser.error('IMAGE length must match its declared HFS+ volume size')
    adb = [args.adb]
    serial = args.serial or subprocess.check_output([*adb, 'get-serialno'], text=True).strip()
    if not serial or serial in ('unknown', 'no devices/emulators found'):
        parser.error('Select one connected Android device with --serial')
    adb += ['-s', serial]

    def phone(command):
        return subprocess.check_output([*adb, 'exec-out', shlex.quote(args.su) + ' -c ' + shlex.quote(command)], timeout=180)

    if phone('id -u').strip() != b'0':
        raise RuntimeError('Root shell is unavailable')
    if phone('getprop ro.boot.slot_suffix').strip() != b'_b':
        raise RuntimeError('Expected Android running on slot B')
    resolved = phone('readlink -f ' + DEVICE).decode().strip()
    if not resolved.startswith('/dev/block/'):
        raise RuntimeError('iosroot is not a resolved block device')
    mounts = phone('cat /proc/mounts').decode().splitlines()
    if any(line.split()[0] in (DEVICE, resolved) for line in mounts if line.split()):
        raise RuntimeError('iosroot is mounted; refusing a raw overwrite')
    capacity = int(phone('blockdev --getsize64 ' + DEVICE))
    if stat.st_size > capacity:
        raise RuntimeError('Image is larger than iosroot')
    print(f'Source: {image} ({stat.st_size} bytes)', flush=True)
    print(f'Target: {serial} {DEVICE}', flush=True)
    acknowledgement = b'ROOT_WRITE_COMPLETE'
    command = 'set -e; set -o pipefail; base64 -d | gzip -d > ' + DEVICE + '; sync; printf ROOT_WRITE_COMPLETE'
    receiver = subprocess.Popen(
        [*adb, 'shell', '-T', '-e', 'none', shlex.quote(args.su) + ' -c ' + shlex.quote(command)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    digest = hashlib.sha256()
    started = reported = time.monotonic()
    written = 0
    try:
        encoded = Base64Stream(receiver.stdin)
        with image.open('rb') as stream:
            with gzip.GzipFile(fileobj=encoded, mode='wb', compresslevel=1) as compressed:
                while data := stream.read(4 * 1024 * 1024):
                    compressed.write(data)
                    digest.update(data)
                    written += len(data)
                    now = time.monotonic()
                    if now - reported >= 5 or written == stat.st_size:
                        print(f'{written / stat.st_size:.1%}; {written / (now - started) / 2**20:.1f} MiB/s', flush=True)
                        reported = now
        encoded.finish()
        receiver.stdin.close()
        code = receiver.wait(timeout=180)
        received = receiver.stdout.read()
        if code or received != acknowledgement:
            raise RuntimeError(f'Root image receiver failed ({code}): ' + receiver.stderr.read().decode(errors='replace'))
        if written != stat.st_size or image.stat().st_mtime_ns != stat.st_mtime_ns:
            raise RuntimeError('Source image size or modification time changed while streaming')
        expected = digest.hexdigest()
        print('Verifying SHA-256 of the written volume...', flush=True)
        actual = phone(f'head -c {written} {DEVICE} | sha256sum').decode().split()[0]
        if actual != expected:
            raise RuntimeError(f'iosroot readback mismatch: expected {expected}, got {actual}')
        print(f'Verified {written} bytes, SHA-256 {actual}', flush=True)
    except BaseException:
        if receiver.poll() is None:
            receiver.kill()
            receiver.wait()
        detail = receiver.stderr.read().decode(errors='replace')
        if detail:
            print('Root receiver: ' + detail, file=sys.stderr)
        raise
    finally:
        receiver.stdout.close()
        receiver.stderr.close()


if __name__ == '__main__':
    main()
