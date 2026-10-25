"""Install a dependency in a pinned, already ad-hoc-signed arm64 iOS executable.

Only unused header padding and existing CodeDirectory page hashes change.
Entitlements, signature metadata, file offsets and dependency ordinals stay intact.
Mutations are restricted to an explicitly selected offline root.
"""
import hashlib
import errno
import os
from pathlib import Path, PurePosixPath
import plistlib
import stat
import struct
import sys
import tempfile
import uuid


def digest(data):
    return hashlib.sha256(data).hexdigest()


def commands(data, filetype=2):
    if len(data) < 32 or struct.unpack_from('<II', data) != (0xfeedfacf, 0x100000c):
        raise ValueError('Expected a thin arm64 Mach-O')
    if struct.unpack_from('<I', data, 12)[0] != filetype:
        raise ValueError('Unexpected Mach-O file type')
    count, size = struct.unpack_from('<II', data, 16)
    end, at, result = 32 + size, 32, []
    if end > len(data) or count > size // 8:
        raise ValueError('Invalid Mach-O load-command bounds')
    for _ in range(count):
        if at + 8 > end:
            raise ValueError('Truncated Mach-O load command')
        kind, length = struct.unpack_from('<II', data, at)
        if length < 8 or length % 8 or length > end - at:
            raise ValueError('Invalid Mach-O load command')
        result.append((kind, at, length))
        at += length
    if at != end:
        raise ValueError('Load commands do not occupy their declared range')
    return result


def command_string(data, at, length, offset):
    if offset < 8 or offset >= length:
        raise ValueError('Invalid load-command string offset')
    end = data.find(b'\0', at + offset, at + length)
    if end < 0:
        raise ValueError('Unterminated load-command string')
    return data[at + offset:end].decode('utf-8')


def image_info(data, filetype=2):
    result = {'uuid': None, 'platform': None, 'dependencies': [], 'first_data': len(data)}
    signature = None
    for kind, at, length in commands(data, filetype):
        if kind == 0x1b and length == 24:
            if result['uuid'] is not None:
                raise ValueError('Duplicate image UUID')
            result['uuid'] = str(uuid.UUID(bytes=bytes(data[at + 8:at + 24])))
        elif kind == 0x32 and length >= 24:
            result['platform'] = struct.unpack_from('<I', data, at + 8)[0]
        elif kind in (0xc, 0x80000018, 0x8000001f, 0x80000023):
            if length < 24:
                raise ValueError('Truncated dylib command')
            name = command_string(data, at, length, struct.unpack_from('<I', data, at + 8)[0])
            result['dependencies'].append((kind, name))
        elif kind == 0xd and length >= 24:
            result['install_name'] = command_string(data, at, length, struct.unpack_from('<I', data, at + 8)[0])
        elif kind == 0x19:
            if length < 72:
                raise ValueError('Truncated segment command')
            count = struct.unpack_from('<I', data, at + 64)[0]
            if length != 72 + 80 * count:
                raise ValueError('Invalid section table')
            for index in range(count):
                section = at + 72 + 80 * index
                size = struct.unpack_from('<Q', data, section + 40)[0]
                offset = struct.unpack_from('<I', data, section + 48)[0]
                flags = struct.unpack_from('<I', data, section + 64)[0] & 0xff
                if size and offset and flags not in (1, 0xc, 0x12):
                    if offset > len(data) or size > len(data) - offset:
                        raise ValueError('Section extends beyond the file')
                    result['first_data'] = min(result['first_data'], offset)
        elif kind == 0x2c:
            if length < 24 or struct.unpack_from('<I', data, at + 16)[0]:
                raise ValueError('Encrypted executables are unsupported')
        elif kind == 0x1d:
            if signature is not None or length != 16:
                raise ValueError('Invalid code-signature command')
            signature = struct.unpack_from('<II', data, at + 8)
    if result['platform'] != 2 or result['uuid'] is None:
        raise ValueError('Expected an identified iOS image')
    result['signature'] = signature
    return result


def signature_info(data):
    image = image_info(data)
    if image['signature'] is None:
        raise ValueError('Missing embedded signature')
    offset, size = image['signature']
    if size < 12 or offset > len(data) or size > len(data) - offset:
        raise ValueError('Invalid signature bounds')
    magic, length, count = struct.unpack_from('>III', data, offset)
    if magic != 0xfade0cc0 or length > size or length < 12 + count * 8:
        raise ValueError('Invalid signature SuperBlob')
    slots, ranges = {}, []
    for index in range(count):
        kind, relative = struct.unpack_from('>II', data, offset + 12 + index * 8)
        if kind in slots or relative < 12 + count * 8 or relative > length - 8:
            raise ValueError('Invalid signature slot')
        start = offset + relative
        blob_magic, blob_size = struct.unpack_from('>II', data, start)
        if blob_size < 8 or blob_size > length - relative:
            raise ValueError('Invalid embedded signature blob')
        if any(relative < end and begin < relative + blob_size for begin, end in ranges):
            raise ValueError('Overlapping signature blobs')
        ranges.append((relative, relative + blob_size))
        slots[kind] = (start, blob_size, blob_magic)
    if set(slots) != {0, 2, 5, 7, 65536} or slots[65536][1:] != (8, 0xfade0b01):
        raise ValueError('Only the inspected ad-hoc signature layout is supported')
    start, cd_size, cd_magic = slots[0]
    if cd_magic != 0xfade0c02 or cd_size < 88:
        raise ValueError('Invalid CodeDirectory')
    version, flags, hash_offset, ident_offset, special, pages, limit = struct.unpack_from('>7I', data, start + 8)
    hash_size, hash_type, platform, page_shift = struct.unpack_from('4B', data, start + 36)
    scatter = struct.unpack_from('>I', data, start + 44)[0]
    limit64 = struct.unpack_from('>Q', data, start + 56)[0]
    if (version != 0x20400 or flags != 2 or hash_type != 2 or hash_size != 32 or
            page_shift != 12 or scatter or special != 7 or limit != offset or
            limit64 not in (0, limit) or pages != (limit + 4095) // 4096 or
            hash_offset < 88 + special * 32 or hash_offset + pages * 32 > cd_size):
        raise ValueError('Unsupported CodeDirectory layout or security flags')
    if ident_offset < 88 or ident_offset >= hash_offset - special * 32:
        raise ValueError('Invalid CodeDirectory identifier')
    ident_end = data.find(b'\0', start + ident_offset, start + hash_offset - special * 32)
    if ident_end < 0:
        raise ValueError('Unterminated CodeDirectory identifier')
    for index in range(pages):
        wanted = hashlib.sha256(data[index * 4096:min(limit, (index + 1) * 4096)]).digest()
        actual = data[start + hash_offset + index * 32:start + hash_offset + (index + 1) * 32]
        if wanted != actual:
            raise ValueError('Invalid executable page hash')
    for kind in (2, 5, 7):
        blob_start, blob_size, _ = slots[kind]
        wanted = hashlib.sha256(data[blob_start:blob_start + blob_size]).digest()
        if data[start + hash_offset - kind * 32:start + hash_offset - (kind - 1) * 32] != wanted:
            raise ValueError('Invalid embedded special-slot hash')
    xml_start, xml_size, xml_magic = slots[5]
    if xml_magic != 0xfade7171 or not isinstance(plistlib.loads(data[xml_start + 8:xml_start + xml_size]), dict):
        raise ValueError('Invalid XML entitlements')
    return {'image': image, 'cd_start': start, 'hash_offset': hash_offset,
            'pages': pages, 'limit': limit, 'slots': slots,
            'cdhash': hashlib.sha256(data[start:start + cd_size]).digest()[:20].hex(),
            'identifier': data[start + ident_offset:ident_end].decode('utf-8'), 'platform': platform}


def add_dependency(data, library, expected_uuid, expected_sha256):
    """Return a fixed-size, independently hash-verifiable mutation of exact bytes."""
    if digest(data) != expected_sha256:
        raise ValueError('Executable does not match the pinned preimage')
    path = PurePosixPath(library)
    if not path.is_absolute() or '..' in path.parts or '\0' in library:
        raise ValueError('Dependency must be an absolute in-root library path')
    before = signature_info(data)
    if before['image']['uuid'] != str(uuid.UUID(expected_uuid)):
        raise ValueError('Executable UUID does not match')
    if any(name == library for _, name in before['image']['dependencies']):
        raise ValueError('Dependency already exists in the pinned preimage')
    name = library.encode('utf-8') + b'\0'
    size = (24 + len(name) + 7) & ~7
    count, old_size = struct.unpack_from('<II', data, 16)
    at = 32 + old_size
    if at + size > before['image']['first_data'] or any(data[at:at + size]):
        raise ValueError('Insufficient zero header padding; relocation is unsupported')
    result = bytearray(data)
    struct.pack_into('<II', result, 16, count + 1, old_size + size)
    result[at:at + size] = struct.pack('<6I', 0xc, size, 24, 0, 0, 0) + name + bytes(size - 24 - len(name))
    for index in range(before['pages']):
        hashed = hashlib.sha256(result[index * 4096:min(before['limit'], (index + 1) * 4096)]).digest()
        start = before['cd_start'] + before['hash_offset'] + index * 32
        result[start:start + 32] = hashed
    result = bytes(result)
    after = signature_info(result)
    if (len(result) != len(data) or after['identifier'] != before['identifier'] or
            after['image']['uuid'] != before['image']['uuid'] or
            after['image']['dependencies'] != before['image']['dependencies'] + [(0xc, library)]):
        raise ValueError('Dependency mutation changed an unrelated image contract')
    for kind in (2, 5, 7, 65536):
        start, length, _ = before['slots'][kind]
        if result[start:start + length] != data[start:start + length]:
            raise ValueError('Signature special-slot contents changed')
    return result


def checked_path(root, relative):
    relative = PurePosixPath(relative)
    if relative.is_absolute() or '..' in relative.parts:
        raise ValueError('Expected a relative offline-root path')
    path = root.joinpath(*relative.parts)
    if not path.resolve().is_relative_to(root) or path.is_symlink():
        raise ValueError('Offline-root path escapes its root or is a symlink')
    return path


def file_metadata(path):
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or path.is_symlink():
        raise ValueError('Expected a regular, non-symlink file')
    if getattr(info, 'st_flags', 0):
        raise ValueError('Executables with BSD file flags require separate handling')
    if sys.platform.startswith('linux'):
        import array
        import fcntl
        flags = array.array('L', [0])
        with path.open('rb') as stream:
            fcntl.ioctl(stream.fileno(), 0x80086601, flags, True)
        # Ext4's intrinsic extent flag follows its new inode automatically.
        # Refuse other flags rather than silently dropping protected metadata.
        if flags[0] & ~0x80000:
            raise ValueError('Executables with filesystem flags require separate handling')
    attributes = {}
    for name in os.listxattr(path):
        try:
            value = os.getxattr(path, name)
        except OSError as error:
            # Some hfsplus listxattr implementations truncate this virtual,
            # catalog-backed name. Read its canonical record rather than lose it.
            if error.errno != errno.ENODATA or name != 'osx.com.apple.FinderInf':
                raise
            name = 'osx.com.apple.FinderInfo'
            value = os.getxattr(path, name)
            if len(value) != 32:
                raise ValueError('Invalid HFS FinderInfo record')
        attributes[name] = value
    return {'mode': stat.S_IMODE(info.st_mode), 'uid': info.st_uid, 'gid': info.st_gid,
            'atime_ns': info.st_atime_ns, 'mtime_ns': info.st_mtime_ns,
            'xattrs': attributes}


def atomic_write(path, data, mode, uid, gid, xattrs=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fchown(stream.fileno(), uid, gid)
            os.fchmod(stream.fileno(), mode)
            for name, value in (xattrs or {}).items():
                os.setxattr(stream.fileno(), name, value)
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def install(root, executable_rel, library, expected_uuid, expected_sha256, state_rel):
    if os.name != 'posix':
        raise ValueError('Offline-root installation requires a POSIX root shell')
    root = Path(root).resolve(strict=True)
    target, state_path = checked_path(root, executable_rel), checked_path(root, state_rel)
    backup = checked_path(root, str(PurePosixPath(state_rel)) + '.executable')
    helper = checked_path(root, library.lstrip('/'))
    helper_info = image_info(helper.read_bytes(), filetype=6)
    if helper_info.get('install_name') != library:
        raise ValueError('Dependency library has the wrong install name')
    metadata = file_metadata(target)
    current = target.read_bytes()
    if state_path.exists():
        saved = plistlib.loads(state_path.read_bytes())
        if (saved.get('version') != 1 or saved.get('executable') != executable_rel or
                saved.get('library') != library or saved.get('original_sha256') != expected_sha256 or
                saved.get('uuid') != str(uuid.UUID(expected_uuid))):
            raise ValueError('Dependency recovery state does not match this installation')
        file_metadata(backup)
        original = backup.read_bytes()
    else:
        original = current
        saved = {'version': 1, 'executable': executable_rel, 'library': library,
                 'uuid': str(uuid.UUID(expected_uuid)), 'original_sha256': expected_sha256,
                 **metadata}
    if 'xattrs' not in saved:
        raise ValueError('Dependency recovery state lacks filesystem metadata')
    patched = add_dependency(original, library, expected_uuid, expected_sha256)
    wanted = digest(patched)
    if digest(current) not in (expected_sha256, wanted):
        raise ValueError('Executable changed outside this dependency installation')
    if state_path.exists() and saved.get('patched_sha256') != wanted:
        raise ValueError('Dependency mutation no longer matches its recorded bytes')
    if not state_path.exists():
        if backup.exists():
            file_metadata(backup)
            if backup.read_bytes() != original:
                raise ValueError('An unrelated executable backup already exists')
        atomic_write(backup, original, 0o600, 0, 0)
        saved['patched_sha256'] = wanted
        atomic_write(state_path, plistlib.dumps(saved, fmt=plistlib.FMT_BINARY), 0o600, 0, 0)
    if current != patched:
        if target.read_bytes() != current:
            raise ValueError('Executable changed during dependency preparation')
        atomic_write(target, patched, saved['mode'], saved['uid'], saved['gid'], saved['xattrs'])
    if target.read_bytes() != patched:
        raise ValueError('Installed dependency failed readback')
    current_metadata = file_metadata(target)
    if any(current_metadata[key] != saved[key] for key in ('mode', 'uid', 'gid', 'xattrs')):
        raise ValueError('Installed executable metadata differs from its preimage')
    return saved


def remove(root, state_rel):
    if os.name != 'posix':
        raise ValueError('Offline-root removal requires a POSIX root shell')
    root = Path(root).resolve(strict=True)
    state_path = checked_path(root, state_rel)
    if not state_path.exists():
        return False
    saved = plistlib.loads(state_path.read_bytes())
    if saved.get('version') != 1:
        raise ValueError('Unsupported dependency recovery state')
    target = checked_path(root, saved['executable'])
    backup = checked_path(root, str(PurePosixPath(state_rel)) + '.executable')
    file_metadata(backup)
    if 'xattrs' not in saved:
        raise ValueError('Dependency recovery state lacks filesystem metadata')
    original = backup.read_bytes()
    patched = add_dependency(original, saved['library'], saved['uuid'], saved['original_sha256'])
    if digest(patched) != saved['patched_sha256']:
        raise ValueError('Dependency recovery identity changed')
    current = target.read_bytes()
    file_metadata(target)
    if current not in (original, patched):
        raise ValueError('Refusing to overwrite unrelated executable changes')
    if current != original:
        atomic_write(target, original, saved['mode'], saved['uid'], saved['gid'], saved['xattrs'])
    if target.read_bytes() != original:
        raise ValueError('Dependency rollback failed readback')
    restored = file_metadata(target)
    if any(restored[key] != saved[key] for key in ('mode', 'uid', 'gid', 'xattrs')):
        raise ValueError('Restored executable metadata differs from its preimage')
    os.utime(target, ns=(saved['atime_ns'], saved['mtime_ns']))
    with target.open('rb') as stream:
        os.fsync(stream.fileno())
    return True
