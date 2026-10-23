#!/usr/bin/env python3
"""Build the experimental iOS arm64 bundle from source with Zig's Clang.

Usage: python MaliMetal/build.py --sysroot DIRECTORY [--wsl] [--out DIRECTORY]
Requires ziglang, LLVM ld64.lld and libSystem with its reexports under
SYSROOT/usr/lib, extracted from the matching iOS cache. No existing bundle is
used as input. Objective-C and IOKit references resolve from the host process
in this experimental build; this is not a replacement for a complete iOS SDK.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import os
import struct
import subprocess
import sys
import ziglang

HERE = Path(__file__).resolve().parent
SOURCES = ('MaliMetalDevice.m', 'MaliMetalCommandQueue.m', 'MaliMetalVulkan.c')


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(arguments):
    subprocess.run([sys.executable, '-m', 'ziglang', 'cc', *arguments], check=True)


def verify_platform(binary):
    data = binary.read_bytes()
    magic, cpu, _, kind, commands = struct.unpack_from('<IIIII', data)
    if (magic, cpu, kind) != (0xfeedfacf, 0x100000c, 6):
        raise ValueError('Expected an arm64 Mach-O dynamic library')
    offset = 32
    platform = None
    for _ in range(commands):
        command, size = struct.unpack_from('<II', data, offset)
        if size < 8 or offset + size > len(data):
            raise ValueError('Invalid Mach-O load command')
        if command == 0x32:
            platform = struct.unpack_from('<I', data, offset + 8)[0]
        offset += size
    if platform != 2:
        raise ValueError(f'Expected iOS platform 2, got {platform}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sysroot', type=Path, required=True)
    parser.add_argument('--linker', default='ld64.lld')
    parser.add_argument('--wsl', action='store_true', help='run the linker in WSL Ubuntu')
    parser.add_argument('--out', type=Path, default=HERE / 'out')
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sysroot = args.sysroot.resolve(strict=True)
    system_library = sysroot / 'usr/lib/libSystem.dylib'
    if not system_library.is_file():
        parser.error('sysroot must contain usr/lib/libSystem.dylib and its reexports')
    inputs = sorted([*HERE.glob('*.h'), *(HERE / name for name in SOURCES),
                     HERE / 'Info.plist', Path(__file__).resolve()])
    hashes = {path.name: sha256(path) for path in inputs}
    digest = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()
    common = ['-target', 'aarch64-ios.16.7', '-mcpu=cortex_a55', '-O2',
              '-fno-sanitize=all', '-Wall', '-Wextra', '-I', str(HERE),
              '-isystem', str(Path(ziglang.__file__).parent / 'lib/libc/include/any-darwin-any')]
    objects = []
    for source in SOURCES:
        obj = output / (Path(source).stem + '.o')
        flags = ['-fno-objc-arc', '-fblocks'] if source.endswith('.m') else []
        run([*common, *flags, '-c', str(HERE / source), '-o', str(obj)])
        objects.append(str(obj))
    bundle = output / 'MaliMetal.bundle'
    bundle.mkdir(exist_ok=True)
    binary = bundle / 'MaliMetal'
    def linker_path(path):
        if args.wsl:
            value = str(Path(path).resolve())
            drive, tail = os.path.splitdrive(value)
            if len(drive) != 2 or drive[1] != ':':
                raise ValueError('WSL linking requires paths on a Windows drive')
            return '/mnt/' + drive[0].lower() + tail.replace('\\', '/')
        return str(path)
    launcher = ['wsl.exe', '-d', 'Ubuntu', '--exec'] if args.wsl else []
    link = [*launcher, args.linker, '-dylib', '-arch', 'arm64',
            '-platform_version', 'ios', '16.7', '16.7', '-undefined', 'dynamic_lookup',
            '-syslibroot', linker_path(sysroot), '-install_name',
            '/System/Library/Extensions/MaliMetal.bundle/MaliMetal',
            '-adhoc_codesign', '-o', linker_path(binary),
            *(linker_path(obj) for obj in objects), '-lSystem']
    subprocess.run(link, check=True)
    verify_platform(binary)
    info = plistlib.loads((HERE / 'Info.plist').read_bytes())
    info['MaliMetalSourceDigest'] = digest
    (bundle / 'Info.plist').write_bytes(plistlib.dumps(info))
    version = subprocess.run([sys.executable, '-m', 'ziglang', 'version'],
                             check=True, capture_output=True, text=True).stdout.strip()
    manifest = {'source_digest': digest, 'inputs': hashes, 'zig_version': version,
                'libsystem_sha256': sha256(system_library),
                'target': 'aarch64-ios.16.7', 'binary_sha256': sha256(binary),
                'binary_size': binary.stat().st_size}
    (output / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
