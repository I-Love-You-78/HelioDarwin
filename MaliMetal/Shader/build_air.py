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
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
PIN = 'a2c440b8bcdcf20d34aa1694b61ae22c2a7ea68b'


def compiler_identity(source, patches, output):
    """Verify the working tree equals the pinned tree plus exactly this series."""
    base = subprocess.check_output(['git', '-C', str(source), 'rev-parse', PIN + '^{tree}'], text=True).strip()
    patch_inputs = [{'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                    for path in patches]
    descriptor, name = tempfile.mkstemp(prefix='compiler-index-', dir=output)
    os.close(descriptor)
    index = Path(name)
    index.unlink()
    environment = dict(os.environ, GIT_INDEX_FILE=str(index))
    def git(*arguments):
        return subprocess.check_output(['git', '-C', str(source), *arguments],
                                       env=environment, text=True).strip()
    try:
        git('read-tree', PIN)
        for path in patches:
            git('apply', '--cached', '--binary', str(path))
        expected = git('write-tree')
        git('add', '--all', '--', '.')
        actual = git('write-tree')
        if actual != expected:
            raise ValueError('Compiler working tree differs from the pinned tree and declared patch series')
    finally:
        index.unlink(missing_ok=True)
        Path(str(index) + '.lock').unlink(missing_ok=True)
    return {'compiler_tree': base, 'compiler_effective_tree': expected, 'compiler_patches': patch_inputs}


def adapter_inputs():
    inputs = list((HERE / 'air-compiler/src').rglob('*.rs'))
    inputs += [HERE / 'air-compiler/Cargo.toml.in', HERE / 'air-compiler/Cargo.lock', Path(__file__)]
    return {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler-source', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--target', default='aarch64-apple-ios')
    p.add_argument('--host-dll', action='store_true', help='also build a native shared library for offline inspection')
    p.add_argument('--allow-rust-197', action='store_true')
    p.add_argument('--compiler-patch', action='append', type=Path, default=[],
                   help='verify this ordered patch series is already applied to the pinned checkout')
    p.add_argument('--shader-tile-image', action='store_true',
                   help='build the tile-image contract supplied by the declared compiler patch series')
    args = p.parse_args()
    source = args.compiler_source.resolve(strict=True)
    output = args.out.resolve()
    if output == HERE or HERE in output.parents:
        p.error('compiler artifacts must be outside the Shader source directory')
    head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if head != PIN:
        p.error(f'compiler must be pinned to {PIN}; got {head}')
    output.mkdir(parents=True, exist_ok=True)
    patches = [path.resolve(strict=True) for path in args.compiler_patch]
    if args.shader_tile_image and not patches:
        p.error('--shader-tile-image requires its declared compiler patch series')
    identity = compiler_identity(source, patches, output)
    adapter_before = adapter_inputs()
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
    if args.shader_tile_image:
        command += ['--features', 'shader-tile-image']
    environment = dict(os.environ, MALIMETAL_COMPILER_EFFECTIVE_TREE=identity['compiler_effective_tree'])
    subprocess.run(command, check=True, env=environment)
    if adapter_inputs() != adapter_before or compiler_identity(source, patches, output) != identity:
        raise ValueError('Compiler or adapter inputs changed during the build; no provenance manifest is written')
    licenses = output / 'licenses/metal2vulkan'
    licenses.mkdir(parents=True, exist_ok=True)
    for name in ('LICENSE', 'LICENSE-GPL-2.0', 'LICENSE-GPL-3.0', 'LICENSE-LGPL-3.0'):
        shutil.copy2(source / name, licenses / name)
    source_notice = f'https://github.com/steelbrain/metal2vulkan/tree/{PIN}\n'
    source_notice += f'Baseline tree: {identity["compiler_tree"]}\nEffective tree: {identity["compiler_effective_tree"]}\n'
    for index, patch in enumerate(identity['compiler_patches']):
        destination = licenses / 'patches' / f'{index + 1:04d}-{Path(patch["path"]).name}'
        destination.parent.mkdir(exist_ok=True)
        shutil.copy2(patch['path'], destination)
        source_notice += f'Patch: {destination.name} SHA256 {patch["sha256"]}\n'
    (licenses / 'SOURCE.txt').write_text(source_notice, encoding='utf-8')
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
    result = {'compiler_revision': PIN, 'compiler_abi': 4, 'compute_abi': 2, 'target': args.target,
              **identity,
              'compiler_features': ['shader-tile-image'] if args.shader_tile_image else [],
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
