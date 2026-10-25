#!/usr/bin/env python3
"""Inspect or explicitly prepare native X676C UIKit metadata in an offline root."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import plistlib
import struct
import tempfile

from sweet_daemons import read, write
import sweet_macho_dependency as dependency


CACHE = 'System/Library/xpc/launchd.plist'
POLICY = 'System/Library/FeatureFlags/Domain/SpringBoard.plist'
STATE = 'private/var/db/sweet-native-display/original.plist'
DEPENDENCY_STATE = 'private/var/db/sweet-native-display/dependency.plist'
EXECUTABLE = 'System/Library/CoreServices/SpringBoard.app/SpringBoard'
EXECUTABLE_UUID = 'e1a4fe7d-29ff-3269-b3ab-50aed40c4495'
EXECUTABLE_SHA256 = 'ea835980610a95591fb349bca077840fab7bdd663efaa11ef054d91bc2125475'
LIBRARY = '/usr/local/lib/sweet/SweetNativeDisplay.dylib'
LOG = '/private/var/mobile/Library/Logs/SweetNativeDisplay.log'


def checked_path(root, relative):
    path = root / relative
    if not path.resolve().is_relative_to(root):
        raise ValueError(f'Offline-root path escapes the mount: {relative}')
    return path


def springboard(cache):
    matches = [(key, job) for key, job in cache['LaunchDaemons'].items()
               if job.get('Label') == 'com.apple.SpringBoard']
    if len(matches) != 1:
        raise ValueError('Expected exactly one existing SpringBoard launchd job')
    key, job = matches[0]
    if job.get('ProgramArguments') != ['/System/Library/CoreServices/SpringBoard.app/SpringBoard'] or job.get('UserName') != 'mobile':
        raise ValueError('Unexpected SpringBoard program/user contract')
    environment = job.get('EnvironmentVariables', {})
    if not isinstance(environment, dict) or any(not isinstance(v, str) for v in environment.values()):
        raise ValueError('Unexpected SpringBoard environment')
    return key, job


def artifact(build):
    build = Path(build)
    manifest = json.loads((build / 'SweetNativeDisplay.build.json').read_text(encoding='utf-8'))
    source = Path(__file__).with_name('native-display') / 'SweetNativeDisplay.c'
    data = (build / 'SweetNativeDisplay.dylib').read_bytes()
    if (manifest.get('os_build') != '20H392' or
            manifest.get('profile') != 'x676c-native-1080x2400-scale3' or
            hashlib.sha256(source.read_bytes()).hexdigest() != manifest.get('source_sha256') or
            hashlib.sha256(data).hexdigest() != manifest.get('binary_sha256')):
        raise ValueError('Native adapter manifest, source or artifact does not match')
    if len(data) < 32 or struct.unpack_from('<II', data) != (0xfeedfacf, 0x100000c) or struct.unpack_from('<I', data, 12)[0] != 6:
        raise ValueError('Native adapter is not an arm64 Mach-O dylib')
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
    if platform != 2:
        raise ValueError('Native adapter does not target iOS')
    return data


def install(root, build):
    data = artifact(build)
    version = read(checked_path(root, 'System/Library/CoreServices/SystemVersion.plist'))
    if version.get('ProductBuildVersion') != '20H392':
        raise ValueError('Native display preparation is restricted to iOS build 20H392')
    cache_path, policy_path, state_path = [checked_path(root, p) for p in (CACHE, POLICY, STATE)]
    cache, policy = read(cache_path), read(policy_path)
    key, job = springboard(cache)
    environment = job.setdefault('EnvironmentVariables', {})
    if 'SpringBoard/Jindo' in environment.get('FEATUREFLAGS_ENABLED', '').split(':'):
        raise ValueError('SpringBoard has a conflicting Jindo environment override')
    old_jindo = policy.get('Jindo')
    if not isinstance(old_jindo, dict):
        raise ValueError('Missing inspected Jindo feature definition')
    if state_path.exists():
        saved = read(state_path)
        if saved.get('job_key') != key or old_jindo not in (saved['jindo'], {'Enabled': False}):
            raise ValueError('Native display recovery state conflicts with current configuration')
        previous_enabled = {'1', saved['old_enabled']}
        if not saved['old_enabled_present']:
            previous_enabled.add(None)
        if environment.get('SWEET_NATIVE_DISPLAY') not in previous_enabled:
            raise ValueError('Native display environment changed after the recovery snapshot')
    else:
        destination = checked_path(root, LIBRARY.lstrip('/'))
        if destination.exists():
            raise ValueError('An untracked native adapter already exists; preserve it explicitly first')
        saved = {
            'job_key': key, 'jindo': copy.deepcopy(old_jindo),
            'library_was_present': LIBRARY in environment.get('DYLD_INSERT_LIBRARIES', '').split(':'),
            'old_enabled_present': 'SWEET_NATIVE_DISPLAY' in environment,
            'old_enabled': environment.get('SWEET_NATIVE_DISPLAY', ''),
            'added_stderr': 'StandardErrorPath' not in job,
        }
    saved['active'] = True
    write(state_path, saved)
    # Enabled and DevelopmentPhase are alternative disclosure forms. Publish
    # one valid definition instead of adding a conflicting Enabled field.
    policy['Jindo'] = {'Enabled': False}
    libraries = [p for p in environment.get('DYLD_INSERT_LIBRARIES', '').split(':') if p]
    libraries = [p for p in libraries if p != LIBRARY]
    if libraries:
        environment['DYLD_INSERT_LIBRARIES'] = ':'.join(libraries)
    else:
        environment.pop('DYLD_INSERT_LIBRARIES', None)
    environment['SWEET_NATIVE_DISPLAY'] = '1'
    job.setdefault('StandardErrorPath', LOG)
    destination = checked_path(root, LIBRARY.lstrip('/'))
    missing = []
    directory = destination.parent
    while not directory.exists():
        missing.append(directory)
        directory = directory.parent
    destination.parent.mkdir(parents=True, exist_ok=True)
    for directory in missing:
        os.chown(directory, 0, 0)
        os.chmod(directory, 0o755)
    descriptor, temporary = tempfile.mkstemp(prefix=destination.name + '.', dir=destination.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chown(temporary, 0, 0)
        os.chmod(temporary, 0o755)
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    if destination.read_bytes() != data:
        raise ValueError('Native adapter failed readback before dependency preparation')
    prepared_dependency = dependency.install(root, EXECUTABLE, LIBRARY,
                                             EXECUTABLE_UUID, EXECUTABLE_SHA256,
                                             DEPENDENCY_STATE)
    saved['load_method'] = 'signed-dependency'
    write(state_path, saved)
    log_directory = checked_path(root, 'private/var/mobile/Library/Logs')
    if not log_directory.exists():
        log_directory.mkdir(parents=True)
        os.chown(log_directory, 501, 501)
        os.chmod(log_directory, 0o755)
    for path, value in ((policy_path, policy), (cache_path, cache)):
        stat = path.stat()
        write(path, value, mode=stat.st_mode & 0o7777, owner=(stat.st_uid, stat.st_gid))
    _, prepared_job = springboard(read(cache_path))
    prepared_env = prepared_job.get('EnvironmentVariables', {})
    if (read(policy_path)['Jindo'] != {'Enabled': False} or
            hashlib.sha256(destination.read_bytes()).digest() != hashlib.sha256(data).digest() or
            prepared_env.get('SWEET_NATIVE_DISPLAY') != '1' or
            LIBRARY in prepared_env.get('DYLD_INSERT_LIBRARIES', '').split(':') or
            hashlib.sha256(checked_path(root, EXECUTABLE).read_bytes()).hexdigest() !=
            prepared_dependency['patched_sha256']):
        raise ValueError('Offline native display preparation did not read back correctly')
    print('Native metadata and non-Jindo policy prepared for the next assembled boot')
    print('Signed SpringBoard dependency prepared; loading, feature-table policy, app contexts, renderer and orientation remain runtime-unverified')


def remove(root):
    state_path = checked_path(root, STATE)
    saved = read(state_path)
    cache_path, policy_path = checked_path(root, CACHE), checked_path(root, POLICY)
    cache, policy = read(cache_path), read(policy_path)
    key, job = springboard(cache)
    if key != saved['job_key'] or policy.get('Jindo') not in ({'Enabled': False}, saved['jindo']):
        raise ValueError('Configuration changed since preparation; recovery data retained for review')
    environment = job.get('EnvironmentVariables', {})
    previous_enabled = {'1', saved['old_enabled']}
    if not saved['old_enabled_present']:
        previous_enabled.add(None)
    if environment.get('SWEET_NATIVE_DISPLAY') not in previous_enabled:
        raise ValueError('Native display environment was changed after preparation')
    if saved.get('load_method') == 'signed-dependency' and not checked_path(root, DEPENDENCY_STATE).exists():
        raise ValueError('Native dependency recovery state is missing; configuration retained')
    dependency.remove(root, DEPENDENCY_STATE)
    policy['Jindo'] = saved['jindo']
    libraries = [p for p in environment.get('DYLD_INSERT_LIBRARIES', '').split(':') if p and p != LIBRARY]
    if saved['library_was_present']:
        libraries.append(LIBRARY)
    if libraries:
        environment['DYLD_INSERT_LIBRARIES'] = ':'.join(libraries)
    else:
        environment.pop('DYLD_INSERT_LIBRARIES', None)
    if saved['old_enabled_present']:
        environment['SWEET_NATIVE_DISPLAY'] = saved['old_enabled']
    else:
        environment.pop('SWEET_NATIVE_DISPLAY', None)
    if saved['added_stderr'] and job.get('StandardErrorPath') == LOG:
        del job['StandardErrorPath']
    for path, value in ((policy_path, policy), (cache_path, cache)):
        stat = path.stat()
        write(path, value, mode=stat.st_mode & 0o7777, owner=(stat.st_uid, stat.st_gid))
    saved['active'] = False
    write(state_path, saved)
    print('Original SpringBoard executable and Jindo policy restored; native opt-in recovered; dylib and logs retained')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument('--install', type=Path, metavar='BUILD_DIR')
    actions.add_argument('--remove', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    if args.install or args.remove:
        if os.name != 'posix' or os.geteuid() != 0:
            parser.error('Offline root mutations require a POSIX root shell (for example sudo in WSL)')
        if args.remove:
            remove(root)
        else:
            install(root, args.install)
        return
    policy = read(checked_path(root, POLICY))
    _, job = springboard(read(checked_path(root, CACHE)))
    print('Jindo definition: ' + json.dumps(policy.get('Jindo'), sort_keys=True))
    print('Native adapter environment: ' + job.get('EnvironmentVariables', {}).get('SWEET_NATIVE_DISPLAY', 'absent'))
    print('Recovery state: ' + ('present' if checked_path(root, STATE).exists() else 'absent'))


if __name__ == '__main__':
    main()
