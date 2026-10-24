#!/usr/bin/env python3
"""Verify the assembled SWEET cache-access policy before deployment."""
import argparse
from pathlib import Path
import struct


def startup_sctlr(data):
    magic, cpu, _, _, count, command_bytes, _, _ = struct.unpack_from('<8I', data)
    if magic != 0xfeedfacf or cpu != 0x100000c:
        raise ValueError('Expected an ARM64 Mach-O kernel')
    if 32 + command_bytes > len(data):
        raise ValueError('Truncated Mach-O commands')
    offset, values = 32, []
    for _ in range(count):
        command, size = struct.unpack_from('<2I', data, offset)
        if size < 8 or offset + size > 32 + command_bytes:
            raise ValueError('Invalid Mach-O command bounds')
        if command == 0x19:  # LC_SEGMENT_64
            address, _, file_offset, file_size, _, protection = struct.unpack_from('<4Q2I', data, offset + 24)
            if file_offset + file_size > len(data):
                raise ValueError('Truncated Mach-O segment')
            if protection & 4:  # VM_PROT_EXECUTE
                for pos in range(file_offset + 16, file_offset + file_size - 3, 4):
                    if struct.unpack_from('<I', data, pos)[0] != 0xd5181000:  # MSR SCTLR_EL1, X0
                        continue
                    value, shifts = 0, set()
                    # XNU's MOV64 macro writes all four halfwords with MOVK.
                    for word in struct.unpack_from('<4I', data, pos - 16):
                        if word & 0xff80001f != 0xf2800000:
                            break
                        shift = (word >> 21) & 3
                        shifts.add(shift)
                        value |= ((word >> 5) & 0xffff) << (16 * shift)
                    else:
                        if len(shifts) == 4:
                            values.append((address + pos - file_offset, value))
        offset += size
    if len(values) != 1:
        raise ValueError(f'Expected one MOV64/MSR SCTLR_EL1 startup sequence, found {len(values)}')
    return values[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kernel', type=Path)
    args = parser.parse_args()
    address, value = startup_sctlr(args.kernel.read_bytes())
    required = (1 << 15) | (1 << 23) | (1 << 26)  # UCT, SPAN, UCI
    print(f'SWEET startup SCTLR_EL1={value:#x} at {address:#x}')
    if value & required != required:
        raise SystemExit('Kernel lacks the configured UCT/SPAN/UCI policy; rebuild before flashing')


if __name__ == '__main__':
    main()
