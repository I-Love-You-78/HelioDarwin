#!/usr/bin/env python3
"""Build the optional native AIR compiler from a verified external checkout.

python MaliMetal/Shader/build_air.py --compiler-source PATH --out PATH

Requires Rust 1.98+ and the aarch64-apple-ios standard library. Rust 1.97 was
also compile-checked; --allow-rust-197 opts into that older toolchain. Download
the compiler separately from https://github.com/steelbrain/metal2vulkan at the
PIN below. Third-party source and all artifacts stay outside this directory.
This adapter is MIT. The compiler is separately GPL-2.0-or-later OR
LGPL-3.0-or-later; the generated license directory preserves its own notices.
Keep the archive and the consumer's object files available for relinking when
distributing an LGPL combined work. Building is not a redistribution license.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
PIN = 'a2c440b8bcdcf20d34aa1694b61ae22c2a7ea68b'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler-source', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--target', default='aarch64-apple-ios')
    p.add_argument('--host-dll', action='store_true', help='also build a native shared library for offline inspection')
    p.add_argument('--allow-rust-197', action='store_true')
    args = p.parse_args()
    source = args.compiler_source.resolve(strict=True)
    output = args.out.resolve()
    if output == HERE or HERE in output.parents:
        p.error('compiler artifacts must be outside the Shader source directory')
    head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if head != PIN:
        p.error(f'compiler must be pinned to {PIN}; got {head}')
    subprocess.run(['git', '-C', str(source), 'diff', '--exit-code', 'HEAD', '--'], check=True, capture_output=True)
    output.mkdir(parents=True, exist_ok=True)
    manifest_dir = output / 'adapter'
    manifest_dir.mkdir(exist_ok=True)
    template = (HERE / 'air-compiler/Cargo.toml.in').read_text()
    template = template.replace('@COMPILER_SOURCE@', source.as_posix())
    template = template.replace('@ADAPTER_SOURCE@', (HERE / 'air-compiler/src/lib.rs').as_posix())
    template = template.replace('@CRATE_TYPES@', '"staticlib", "cdylib"' if args.host_dll else '"staticlib"')
    manifest = manifest_dir / 'Cargo.toml'
    manifest.write_text(template, encoding='utf-8')
    lock = HERE / 'air-compiler/Cargo.lock'
    if lock.is_file():
        shutil.copy2(lock, manifest_dir / 'Cargo.lock')
    command = ['cargo', 'build', '--release', '--manifest-path', str(manifest), '--target', args.target]
    if lock.is_file():
        command.append('--locked')
    if args.allow_rust_197:
        command.append('--ignore-rust-version')
    subprocess.run(command, check=True)
    licenses = output / 'licenses/metal2vulkan'
    licenses.mkdir(parents=True, exist_ok=True)
    for name in ('LICENSE', 'LICENSE-GPL-2.0', 'LICENSE-GPL-3.0', 'LICENSE-LGPL-3.0'):
        shutil.copy2(source / name, licenses / name)
    (licenses / 'SOURCE.txt').write_text(f'https://github.com/steelbrain/metal2vulkan/tree/{PIN}\n', encoding='utf-8')
    metadata = json.loads(subprocess.check_output(
        ['cargo', 'metadata', '--manifest-path', str(manifest), '--locked',
         '--format-version', '1', '--filter-platform', args.target], text=True))
    dependencies = []
    for package in metadata['packages']:
        package_root = Path(package['manifest_path']).parent
        notice_dir = output / 'licenses' / (package['name'] + '-' + package['version'])
        notices = [path for path in package_root.iterdir() if path.is_file() and
                   path.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE', 'COPYRIGHT'))]
        if notices:
            notice_dir.mkdir(parents=True, exist_ok=True)
            for path in notices:
                shutil.copy2(path, notice_dir / path.name)
        dependencies.append({key: package.get(key) for key in ('name', 'version', 'license', 'source', 'repository')})
    (output / 'licenses/dependencies.json').write_text(json.dumps(dependencies, indent=2) + '\n', encoding='utf-8')
    artifacts = [path for path in (manifest_dir / 'target' / args.target / 'release').glob('*MaliMetalAIR*')
                 if path.suffix in ('.a', '.lib', '.dll', '.dylib')]
    hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in artifacts if path.is_file()}
    result = {'compiler_revision': PIN, 'compiler_abi': 3, 'compute_abi': 1, 'target': args.target,
              'compiler_tree': subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD^{tree}'], text=True).strip(),
              'rustc_version': subprocess.check_output(['rustc', '--version'], text=True).strip(),
              'cargo_lock_sha256': hashlib.sha256((manifest_dir / 'Cargo.lock').read_bytes()).hexdigest(),
              'adapter_sha256': hashlib.sha256((HERE / 'air-compiler/src/lib.rs').read_bytes()).hexdigest(),
              'adapter_sources': {str(path.relative_to(HERE)).replace('\\', '/'): hashlib.sha256(path.read_bytes()).hexdigest()
                                  for path in sorted((HERE / 'air-compiler/src').rglob('*.rs'))},
              'adapter_manifest_sha256': hashlib.sha256((HERE / 'air-compiler/Cargo.toml.in').read_bytes()).hexdigest(),
              'build_helper_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'license': 'GPL-2.0-or-later OR LGPL-3.0-or-later', 'artifacts': hashes}
    (output / 'build.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
