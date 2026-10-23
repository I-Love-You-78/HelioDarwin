#!/usr/bin/env python3
"""Build the opt-in iOS 20H392 native display metadata adapter."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

import ziglang


FRAMEWORKS = {
    'FrontBoard': '0abe70d4-6cc0-37f9-af9e-ca2fd42442de',
    'FrontBoardServices': '9df90f7d-2d79-316c-92e4-13ead6cf214d',
    'SpringBoardFoundation': '7949279b-dbc1-3949-b8ef-90e7ef34dd46',
    'UIKitCore': '1242978a-2c2c-3781-8d6c-9777edce2804',
    'UIKitServices': 'b9aded5a-37c0-30d4-ac8a-28ddc2f5654c',
    'SpringBoardHome': 'ee83267f-132c-35c6-aa2d-aea8eee035a3',
    'SpringBoard': '22034aba-9a9a-3b94-93a5-a0de02befd2e',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sysroot', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--linker', default='ld64.lld')
    parser.add_argument('--wsl', action='store_true')
    args = parser.parse_args()
    source = Path(__file__).with_name('SweetNativeDisplay.c')
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sysroot = args.sysroot.resolve(strict=True)
    obj, binary = output / 'SweetNativeDisplay.o', output / 'SweetNativeDisplay.dylib'
    headers = Path(ziglang.__file__).parent / 'lib/libc/include/any-darwin-any'
    subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-target', 'aarch64-ios.16.7',
                    '-mcpu=cortex_a55', '-O2', '-fno-sanitize=all', '-Wall', '-Wextra',
                    '-Werror', '-isystem', str(headers), '-c', str(source), '-o', str(obj)], check=True)

    def link_path(path):
        value = str(path)
        if not args.wsl:
            return value
        drive, tail = os.path.splitdrive(value)
        if len(drive) != 2 or drive[1] != ':':
            raise ValueError('WSL linker paths must be on a Windows drive')
        return '/mnt/' + drive[0].lower() + tail.replace('\\', '/')

    launcher = ['wsl.exe', '-d', 'Ubuntu', '--exec'] if args.wsl else []
    subprocess.run([*launcher, args.linker, '-dylib', '-arch', 'arm64', '-platform_version',
                    'ios', '16.7', '16.7', '-syslibroot', link_path(sysroot), '-adhoc_codesign',
                    '-install_name', '/usr/local/lib/sweet/SweetNativeDisplay.dylib',
                    '-o', link_path(binary), link_path(obj), '-lSystem'], check=True)
    data = binary.read_bytes()
    if len(data) < 32 or struct.unpack_from('<II', data) != (0xfeedfacf, 0x100000c) or struct.unpack_from('<I', data, 12)[0] != 6:
        raise ValueError('Unexpected arm64 Mach-O dylib')
    count, size = struct.unpack_from('<II', data, 16)
    end, at, platform = 32 + size, 32, None
    if end > len(data):
        raise ValueError('Truncated Mach-O load commands')
    for _ in range(count):
        if at + 8 > end:
            raise ValueError('Truncated Mach-O load command')
        command, length = struct.unpack_from('<II', data, at)
        if length < 8 or at + length > end:
            raise ValueError('Invalid Mach-O load command')
        if command == 0x32 and length >= 24:
            platform = struct.unpack_from('<I', data, at + 8)[0]
        at += length
    if platform != 2 or hashlib.sha256(source.read_bytes()).hexdigest() != digest:
        raise ValueError('Wrong target platform or source changed during build')
    (output / 'SweetNativeDisplay.build.json').write_text(json.dumps({
        'os_build': '20H392', 'framework_uuids': FRAMEWORKS,
        'featureflags_uuid': '3a160c9e-3488-3ffd-a194-e2a166fe1169',
        'source_sha256': digest, 'binary_sha256': hashlib.sha256(data).hexdigest(),
        'profile': 'x676c-native-1080x2400-scale3', 'runtime_validation': 'pending',
    }, indent=2) + '\n', encoding='utf-8')
    print(binary)


if __name__ == '__main__':
    main()
