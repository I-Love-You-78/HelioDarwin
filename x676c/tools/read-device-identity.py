#!/usr/bin/env python3
"""Save Android factory Wi-Fi and provisioned Bluetooth data for the next DT.

Never substitute an interface's current, potentially randomized MAC address.
The resulting file identifies the device and must remain outside version control.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile


BT_RECORD = '/mnt/vendor/nvdata/APCFG/APRDEB/BT_Addr'
# These exact vendor readers establish LID 1, the 438-byte record, trailer,
# and canonical byte order. Unknown firmware needs its own format review.
BT_READERS = {
    '/vendor/lib64/libcustom_nvram.so': '0f536161b03bd9b99989a6e6b106f4f6ea287b07c7bb8584769cf2432f4132cd',
    '/vendor/lib64/libbluetooth_mtk_pure.so': 'f1fa17c323f992eb21de282ebf3f7613beb9dbab02aa7b41ba5cde53ccc730e9',
    '/vendor/lib64/libnvram.so': '72df9b8a454a61a7b562617401e804c9173c5b9f54f5b6593f994817c27e63c7',
}


def canonical_address(raw):
    if len(raw) != 6 or raw in (bytes(6), bytes([255]) * 6) or raw[0] & 3:
        raise ValueError('Identity address must be globally assigned unicast data')
    return ':'.join(f'{value:02x}' for value in raw)


def vendor_default_addresses(data):
    """Read the nine template records from the hash-pinned vendor ELF."""
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('Unexpected Bluetooth vendor ELF format')
    table = struct.unpack_from('<Q', data, 32)[0]
    stride, count = struct.unpack_from('<HH', data, 54)
    if stride != 56 or table > len(data) or count > (len(data) - table) // stride:
        raise ValueError('Invalid Bluetooth vendor segment table')
    loads = []
    for index in range(count):
        kind, _, offset, address, _, size, _, _ = struct.unpack_from('<II6Q', data, table + index * stride)
        if kind == 1:
            if offset > len(data) or size > len(data) - offset:
                raise ValueError('Invalid Bluetooth vendor load segment')
            loads.append((address, offset, size))
    result = set()
    for address in range(0x226b, 0x226b + 9 * 438, 438):
        segments = [(base, offset, size) for base, offset, size in loads
                    if base <= address and address + 6 <= base + size]
        if len(segments) != 1:
            raise ValueError('Bluetooth default template does not map uniquely')
        base, offset, _ = segments[0]
        result.add(data[offset + address - base:offset + address - base + 6])
    return result


def read_factory_identity(adb, serial, su):
    selector = ['-s', serial] if serial else []

    def phone_bytes(command):
        return subprocess.check_output(
            [adb, *selector, 'exec-out', su, '-c', command],
            stderr=subprocess.PIPE, timeout=60)

    def phone(command):
        return phone_bytes(command).decode('utf-8', errors='replace')

    if phone('id -u').strip() != '0':
        raise ValueError('Root is required to read factory identity')
    if phone('getprop ro.boot.slot_suffix').strip() != '_b':
        raise ValueError('Factory identity capture requires the Android fallback slot B')
    device_serial = subprocess.check_output(
        [adb, *selector, 'get-serialno'], stderr=subprocess.PIPE,
        timeout=15, text=True, encoding='utf-8').strip()
    if not device_serial or device_serial in ('unknown', 'offline'):
        raise ValueError('Connected device identity is unavailable')
    # This WifiService field is the saved factory value, not the active wlan0
    # address or the per-network address shown elsewhere in the same dump.
    values = set(re.findall(
        r'(?m)^\s*wifi_sta_factory_mac_address\s*=\s*([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})\s*$',
        phone('dumpsys wifi')))
    normalized = {value.lower() for value in values}
    if len(normalized) != 1:
        raise ValueError('WifiService did not return one unambiguous factory Wi-Fi address')
    address = canonical_address(bytes.fromhex(normalized.pop().replace(':', '')))
    defaults = None
    for path, expected in BT_READERS.items():
        data = phone_bytes('cat ' + path)
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('Bluetooth vendor layout is not the reviewed firmware')
        if path.endswith('/libbluetooth_mtk_pure.so'):
            defaults = vendor_default_addresses(data)
    record = phone_bytes('cat ' + BT_RECORD)
    if len(record) != 440 or record[-2] != 0xaa:
        raise ValueError('Bluetooth provisioning record has the wrong format')
    check = 0
    for index, value in enumerate(record[:-2]):
        check = check ^ value if index & 1 else (check + value) & 0xff
    if record[-1] != check:
        raise ValueError('Bluetooth provisioning record checksum failed')
    if defaults is None or record[:6] in defaults:
        raise ValueError('Bluetooth record contains a vendor default, not provisioned identity')
    bluetooth = canonical_address(record[:6])
    return {'schema_version': 1, 'wifi_mac': address,
            'bluetooth_mac': bluetooth, 'bluetooth_source': 'android:mtk_nvram_btradio_v1',
            'source': 'android:dumpsys_wifi_factory', 'device_serial': device_serial,
            'captured_utc': datetime.now(timezone.utc).isoformat()}


def save_private(path, identity):
    path = Path(path).absolute()
    if path.is_symlink():
        raise ValueError('Identity output must not be a symlink')
    if path.exists():
        previous = json.loads(path.read_text(encoding='utf-8'))
        if previous.get('schema_version') != 1 or previous.get('source') != identity['source']:
            raise ValueError('Refusing to overwrite an unrelated identity file')
        if previous.get('device_serial') == identity['device_serial']:
            for field in ('wifi_mac', 'bluetooth_mac'):
                if field in previous and previous[field] != identity[field]:
                    raise ValueError('Provisioned identity changed; review before replacing it')
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as stream:
            json.dump(identity, stream, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
            os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--serial', default='')
    parser.add_argument('--su', default='su')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        identity = read_factory_identity(args.adb, args.serial, args.su)
        save_private(args.out, identity)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        # Do not include dumpsys output, MAC addresses or serials in diagnostics.
        parser.exit(1, f'Factory identity capture failed ({type(error).__name__}); no boot data written\n')
    print('Wi-Fi/Bluetooth identity saved privately (6 bytes each); addresses not logged')


if __name__ == '__main__':
    main()
