#!/usr/bin/env python3
"""Build the opt-in, build-specific PosterBoard compatibility dylib."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import ziglang


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sysroot', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--linker', default='ld64.lld')
    parser.add_argument('--wsl', action='store_true')
    parser.add_argument('--registration-helper', action='store_true',
                        help='build the explicit provider-registration/query executable')
    args = parser.parse_args()
    stem = 'SweetRegisterPosters' if args.registration_helper else 'SweetPosterBoard'
    source = Path(__file__).with_name(stem + '.c')
    source_digest = hashlib.sha256(source.read_bytes()).hexdigest()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sysroot = args.sysroot.resolve(strict=True)
    obj = output / (stem + '.o')
    binary = output / (stem if args.registration_helper else stem + '.dylib')
    headers = Path(ziglang.__file__).parent / 'lib/libc/include/any-darwin-any'
    subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-target', 'aarch64-ios.16.7',
                    '-mcpu=cortex_a55', '-O2', '-fno-sanitize=all', '-Wall', '-Wextra',
                    '-Werror', '-fblocks', '-isystem', str(headers), '-c', str(source), '-o', str(obj)], check=True)

    def link_path(path):
        value = str(path)
        if not args.wsl:
            return value
        drive, tail = os.path.splitdrive(value)
        if len(drive) != 2 or drive[1] != ':':
            raise ValueError('WSL linker paths must be on a Windows drive')
        return '/mnt/' + drive[0].lower() + tail.replace('\\', '/')

    launcher = ['wsl.exe', '-d', 'Ubuntu', '--exec'] if args.wsl else []
    kind_flags = [] if args.registration_helper else [
        '-dylib', '-install_name', '/usr/local/lib/sweet/SweetPosterBoard.dylib']
    subprocess.run([*launcher, args.linker, *kind_flags, '-arch', 'arm64', '-platform_version',
                    'ios', '16.7', '16.7', '-syslibroot', link_path(sysroot), '-adhoc_codesign',
                    '-o', link_path(binary), link_path(obj), '-lSystem'], check=True)
    data = binary.read_bytes()
    expected_kind = 2 if args.registration_helper else 6
    if len(data) < 32 or struct.unpack_from('<II', data) != (0xfeedfacf, 0x100000c) or struct.unpack_from('<I', data, 12)[0] != expected_kind:
        raise ValueError('Unexpected arm64 Mach-O artifact kind')
    commands, command_bytes = struct.unpack_from('<II', data, 16)
    if command_bytes > len(data) - 32:
        raise ValueError('Truncated Mach-O load commands')
    offset = 32
    platform = None
    for _ in range(commands):
        command, size = struct.unpack_from('<II', data, offset)
        if size < 8 or size > 32 + command_bytes - offset:
            raise ValueError('Invalid Mach-O load command')
        if command == 0x32:
            platform = struct.unpack_from('<I', data, offset + 8)[0]
        offset += size
    if platform != 2:
        raise ValueError('Artifact is not built for iOS')
    if hashlib.sha256(source.read_bytes()).hexdigest() != source_digest:
        raise ValueError('Source changed during build')
    (output / (stem + '.build.json')).write_text(json.dumps({
        'os_build': '20H392', 'source_sha256': source_digest,
        'kind': 'executable' if args.registration_helper else 'dylib',
        'framework_uuid': '8d42103d-bd59-308d-9d2c-2082b39738e7' if args.registration_helper else
                          'fbd32eac-fbbf-3f56-a0ba-5bd44d2e2725',
        'binary_sha256': hashlib.sha256(data).hexdigest(),
        'runtime_validation': 'pending',
    }, indent=2) + '\n', encoding='utf-8')
    print(binary)


if __name__ == '__main__':
    main()
