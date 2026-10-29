#!/usr/bin/env python3
"""Build the minimal Apple Device Tree the XNU kernel needs on the Galaxy S6
Edge+ (zenlte, SM-G928F, Samsung Exynos 7420).

Every address here is transcribed from the vendor kernel tree for this exact
board, samsungexynos7420/android_kernel_samsung_universal7420 @ lineage-19.1:

  arch/arm64/boot/dts/exynos7420.dtsi               SoC blocks (gic, decon, dsim)
  arch/arm64/boot/dts/exynos7420-zenlte_common.dtsi board memory map and peripherals
  arch/arm64/boot/dts/exynos7420-universal7420.dts  panel timing (s6e3hf2)

Nothing is inherited from the sweet/topaz/x676c ports: those are Qualcomm
SM7150 and MediaTek MT6789 boards whose fixed physical addresses mean
unrelated hardware here.

Unlike every other board in this tree the Exynos 7420 has a GIC-400, not a
GICv3. It therefore has no redistributors and no ICC_* system registers, and
the distributor and CPU interface are plain memory-mapped frames. The kernel
side of that lives in pexpert/arm/pe_fiq.c and osfmk/arm64/sleh.c.

Usage:
  mkadt.py OUT.adt [--dump]
"""
import argparse, hashlib, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402

u32 = lambda *v: struct.pack('<%dI' % len(v), *v)
u64 = lambda *v: struct.pack('<%dQ' % len(v), *v)
cstr = lambda s: s.encode() + b'\0'


def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, cstr(name))] + [(k, 0, v) for k, v in props]
    n.children = list(children)
    return n


# ---------------------------------------------------------------------------
# Memory. exynos7420-zenlte_common.dtsi: memory@40000000 reg = <0x40000000
# 0xC0000000>, i.e. the DRAM window ends exactly at 4 GiB.
#
# The window is NOT all usable: the TEE, BL31, mfc_fw, secdma and the ION
# video pools are carved out of it (0xE1F00000 upwards in the vendor dts).
# XNU only ever maps the ranges handed to it here, so this ADT publishes the
# low region as kernel-managed RAM and leaves the carve-outs out of it. The
# upper 3 GiB is what the kernel actually gets.
PHYS_BASE = 0x40000000
MEM_SIZE = 0xC0000000
# Carve-outs inside the window (vendor dts reserved-memory, ascending).
RESERVED = [
    (0xE1000000, 0x800000, 'mipi-lli-shmem'),
    (0xE1F00000, 0x040000, 'secdma'),
    (0xE2000000, 0x600000, 'ion common'),
    (0xE2600000, 0x110000, 'mfc_nfw'),
    (0xE2800000, 0x200000, 'mfc_fw'),
    (0xE2A00000, 0xE400000, 'ion video'),
    (0xF0E00000, 0x600000, 'ion video_ext'),
    (0xF2A00000, 0x200000, 'mali iram-vector'),
    (0xF2C00000, 0x100000, 'sss debug'),
]

# /arm-io children are relative to this; it must not be 0 (serial_init bails).
SOC_BASE = 0x10000000

# exynos7420.dtsi gic: interrupt-controller@11001000
#   compatible = "arm,cortex-a15-gic", "arm,cortex-a9-gic"
#   reg = <0x11001000 0x1000>, <0x11002000 0x1000>,
#         <0x11004000 0x2000>, <0x11006000 0x2000>
GICD, GICD_SIZE = 0x11001000, 0x1000
GICC, GICC_SIZE = 0x11002000, 0x1000
GICH, GICH_SIZE = 0x11004000, 0x2000
GICV, GICV_SIZE = 0x11006000, 0x2000

# Architected timer PPIs are the standard layout <13 14 11 10>: secure
# physical, non-secure physical, virtual, hypervisor. XNU drives the
# non-secure physical timer, so the timer INTID is 14 - not 16+11, and not
# the 27 the S7 Edge's UEFI used for its virtual timer.
TIMEBASE = 24000000            # 24 MHz, exynos7420.dtsi fixed-rate-clocks/oscclk
VTIMER_INTID = 14
IPI_SGI = 1                    # SGI used for cross-CPU signals

# Four Cortex-A57 (cluster 0) + four Cortex-A53 (cluster 1), PSCI CPU_ON.
# This is a heterogeneous (big.LITTLE) part, unlike the homogeneous sweet
# ports: cluster 1 has MPIDR Aff1 = 1, so its regs are 0x100..0x103.
CPUS = [
    (0x000, 0), (0x001, 0), (0x002, 0), (0x003, 0),   # A57, cluster 0
    (0x100, 1), (0x101, 1), (0x102, 1), (0x103, 1),   # A53, cluster 1
]

# Display. The panel is s6e3hf2 (exynos7420-universal7420.dts): 1600x2560,
# MIPI command mode, 60 Hz, DSI high-speed clock 1100 MHz. The framebuffer
# address is supplied by the boot loader, not fixed by the dts, so it is an
# input here (see --fb). The pixel format is the BGRA the LK splash uses.
FB_W, FB_H = 1600, 2560
FB_STRIDE = FB_W * 4
FB_DEFAULT = 0xE2A00000        # start of the ion video pool: overridden by --fb
FB_SIZE_DEFAULT = 0x8000000

ZENLTE_ECID = 0x5A454E4C54450001   # 'ZENLTE' + 1, synthetic
BOOT_MANIFEST_HASH = hashlib.sha384(b'zenlte boot manifest').digest()


def check_layout(fb, fb_size):
    """Refuse to emit an ADT whose framebuffer overlaps a carve-out."""
    end = fb + fb_size
    if fb < PHYS_BASE or end > PHYS_BASE + MEM_SIZE:
        raise ValueError(f'framebuffer {fb:#x}+{fb_size:#x} is outside DRAM')
    for base, size, name in RESERVED:
        if fb < base + size and base < end:
            raise ValueError(
                f'framebuffer {fb:#x}+{fb_size:#x} overlaps reserved {name} '
                f'{base:#x}+{size:#x}; pass --fb with the boot loader address')


def build(seed=None, fb=FB_DEFAULT, fb_size=FB_SIZE_DEFAULT):
    check_layout(fb, fb_size)
    seed = seed if seed is not None else os.urandom(64)
    phandle = iter(range(1, 1000))
    ph = lambda: ('AAPL,phandle', u32(next(phandle)))

    chosen = node('chosen', [
        ph(),
        ('dram-base', u64(PHYS_BASE)),
        ('dram-size', u64(MEM_SIZE)),
        ('random-seed', seed),
        ('debug-enabled', u32(1)),
        ('firmware-version', cstr('zenlte-shim')),
        ('system-firmware-version', cstr('zenlte-shim')),
        ('boot-manifest-hash', BOOT_MANIFEST_HASH),
        ('crypto-hash-method', cstr('sha2-384')),
        # 1600x2560 at 3x would not fit the panel; 1x keeps the console grid
        # at the native 200x160 cells.
        ('display-scale', u32(1)),
        ('unique-chip-id', struct.pack('<Q', ZENLTE_ECID)),
        ('chip-id', u32(0x8001)),
        ('certificate-production-status', u32(0)),
        ('effective-production-status-ap', u32(0)),
        ('certificate-security-mode', u32(1)),
        ('effective-security-mode-sep', u32(1)),
    ], [node('memory-map', [ph()]),
        node('asmb', [ph(), ('lp-sip0', u64(0x6f)), ('lp-sip1', u32(1))])])

    cpu_nodes = []
    for i, (mpidr, cluster) in enumerate(CPUS):
        cpu_nodes.append(node(f'cpu{i}', [
            ph(),
            ('device_type', cstr('cpu')),
            ('compatible', cstr('arm,cortex-a57' if cluster == 0 else 'arm,cortex-a53')
                           + cstr('ARM,v8')),
            ('reg', u32(mpidr)),        # real MPIDR Aff1:Aff0, not a logical id
            ('cpu-id', u32(i)),
            ('cluster-id', u32(cluster)),
            # ml_parse_cpu_topology() compares this against the CHARACTER
            # constants 'P' and 'E' (osfmk/arm64/machine_routines.c), not
            # against 0/1. Anything else leaves the cluster CLUSTER_TYPE_SMP.
            ('cluster-type', u32(ord('P') if cluster == 0 else ord('E'))),
            ('die-cluster-id', u32(cluster)),
            ('cluster-core-id', u32(i - (4 if cluster else 0))),
            ('l2-cache-id', u32(cluster)),
            ('l3-cache-id', u32(0)),
            ('state', cstr('running' if i == 0 else 'stopped')),
            ('timebase-frequency', u32(TIMEBASE)),
            ('clock-frequency', u32(2100000000 if cluster == 0 else 1500000000)),
            ('bus-frequency', u32(100000000)),
            ('memory-frequency', u32(1552000000)),
            ('peripheral-frequency', u32(TIMEBASE)),
            ('fixed-frequency', u32(TIMEBASE)),
        ]))
    cpus = node('cpus', [ph(), ('#address-cells', u32(1)), ('#size-cells', u32(0))], cpu_nodes)

    # GIC-400. The kernel reads the four frames from this reg. "arm,gic-v2" is
    # the marker pe_fiq.c looks for to take the memory-mapped path.
    gic = node('gic', [
        ph(),
        ('compatible', cstr('arm,gic-400') + cstr('arm,cortex-a15-gic')
                       + cstr('arm,gic-v2')),
        ('interrupt-controller', b''),
        ('#interrupt-cells', u32(3)),
        ('virtual-timer-intid', u32(VTIMER_INTID)),
        ('ipi-sgi', u32(IPI_SGI)),
        ('reg', u64(GICD - SOC_BASE, GICD_SIZE,
                    GICC - SOC_BASE, GICC_SIZE,
                    GICH - SOC_BASE, GICH_SIZE,
                    GICV - SOC_BASE, GICV_SIZE)),
    ])

    arm_io = node('arm-io', [
        ph(),
        ('device_type', cstr('sweet-io')),
        ('compatible', cstr('sweet-io') + cstr('arm-io')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
        ('ranges', u64(0, SOC_BASE, 0x100000000 - SOC_BASE)),
    ], [gic,
        node('wdt', [ph(), ('no-pmu', u32(1))]),
        node('disp0', [
            ph(),
            ('device_type', cstr('display-subsystem')),
            ('compatible', cstr('sweet,display')),
            # decon_0 @0x13930000, dsim_0 @0x13900000 (exynos7420.dtsi)
            ('decon-base', u64(0x13930000)),
            ('dsim-base', u64(0x13900000)),
            ('panel', cstr('s6e3hf2')),
            ('canvas-width', u32(FB_W)),
            ('canvas-height', u32(FB_H)),
        ])])

    # The framebuffer console: pe_serial.c fbcon_probe() finds this node,
    # maps reg, and registers itself as the serial console, so every kprintf
    # lands on the panel. This is how the boot log reaches the screen.
    fbcon = node('sweet-fbcon', [
        ph(),
        ('reg', u64(fb, fb_size)),
        ('width', u32(FB_W)),
        ('height', u32(FB_H)),
        ('stride', u32(FB_STRIDE)),
    ])

    defaults = node('defaults', [ph(), ('no-effaceable-storage', u32(1))])
    buttons = node('buttons', [
        ph(),
        ('button-names', cstr('volup') + cstr('voldown') + cstr('hold')),
        ('home-button-type', u32(2)),
        ('opposed-power-vol-buttons', u32(1)),
    ])

    return node('device-tree', [
        ph(),
        ('compatible', cstr('D22AP') + cstr('iPhone10,3')
                       + cstr('samsung,zenlte') + cstr('samsung,exynos7420')
                       + cstr('exynos7420') + cstr('AppleARM')),
        ('model', cstr('iPhone10,3')),
        ('target-type', cstr('D22')),
        ('target-sub-type', cstr('D22AP')),
        ('manufacturer', cstr('Apple Inc.')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
    ], [chosen, defaults, cpus, arm_io, fbcon, buttons])


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--dump', action='store_true')
    ap.add_argument('--fb', type=lambda s: int(s, 0), default=FB_DEFAULT,
                    help='framebuffer physical address from the boot loader')
    ap.add_argument('--fb-size', type=lambda s: int(s, 0), default=FB_SIZE_DEFAULT)
    a = ap.parse_args()
    try:
        blob = adt.serialize(build(fb=a.fb, fb_size=a.fb_size))
    except ValueError as error:
        ap.error(str(error))
    open(a.out, 'wb').write(blob)
    if a.dump:
        adt.dump(adt.parse(blob)[0])
    print(f'{a.out}: {len(blob)} bytes', file=sys.stderr)
