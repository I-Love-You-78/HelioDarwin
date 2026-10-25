#!/usr/bin/env python3
"""Build the minimal Apple Device Tree the xnu kernel needs on the X676C.

Port of tools/mkadt.py (topaz) to the Infinix Note 12 2023 (MediaTek MT6789).
Every address comes from the X676C live FDT (see x676c/README.md). Early boot
nodes and optional verified factory metadata are provided here; peripheral
service nodes depend on their ported drivers. Usage:

  mkadt.py OUT.adt [--dump] [--d22ap PATH] [--device-identity PRIVATE_JSON]
           [--static-trust-cache PRIVATE_RAW_PAYLOAD]

The kernel still matches the topaz class names (SweetPlatformExpert matches
'xiaomi,sweet', CPU nubs 'qcom,kryo470'), so those compatible strings stay
until the kernel side is renamed.
"""
import argparse, hashlib, json, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402

# Kernel-managed RAM: rank 1 between the 4K dramc training pages that end
# each rank (mblock-1-dramc-rk0 0x13ffff000, mblock-2-dramc-rk1 0x23ffff000).
# Every other fixed carve-out (TEE, GZ, SSPM, SCP, modem, BL31, pstore, LK's
# framebuffer) is below 4 GiB. Must match shim/loader.c KERNEL_RAM_*.
PHYS_BASE = 0x140000000
MEM_SIZE = 0xffff0000
# /arm-io children are relative to this; it must not be 0 (serial_init bails)
SOC_BASE = 0x01000000

# interrupt-controller (arm,gic-v3): GICD 0x0c000000+0x40000, GICR 0x0c040000+0x200000
GICD, GICD_SIZE = 0x0c000000, 0x40000
GICR, GICR_SIZE = 0x0c040000, 0x200000
# arm,armv8-timer PPIs <13 14 11 10>: standard layout, virtual timer PPI 11
TIMEBASE = 13000000
VTIMER_INTID = 16 + 11

# UFS host (ufshci@11270000, mediatek,mt8183-ufshci), SPI 137. Resets live in
# infracfg_ao 0x10001000 (ti,syscon-reset): hci 0x130/0x134 bit 15, unipro
# 0x140/0x144 bit 7, crypto 0x150/0x154 bit 21 (set/clear registers).
UFS, UFS_SIZE, UFS_SPI = 0x11270000, 0x2300, 137
INFRACFG_AO = 0x10001000

# Visible top layer LK leaves on the command-mode panel (OVL0 L1). The shim
# passes this framebuffer to XNU, whose MTK display driver owns panel updates.
FB, FB_SIZE = 0xfeabe000, 0xa00000
FB_W, FB_H, FB_STRIDE = 1080, 2400, 1080 * 4

X676C_ECID = 0x5836373643000001  # 'X676C' + 1, synthetic
BOOT_MANIFEST_HASH = hashlib.sha384(b'x676c boot manifest').digest()

# CPUs xnu may use: the six Cortex-A55 (MPIDR 0x000-0x500, one cluster).
# Both A76 cores (0x600 and 0x700) remain off pending AMP topology support.
CPU_MPIDRS = [0x000, 0x100, 0x200, 0x300, 0x400, 0x500]

D22AP_ADT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../research/d22ap.adt')

# Original 20H392 StaticTrustCache IM4P payload, verified against its d22ap
# BuildManifest digest. This pins packaged software, not Apple boot authority.
STATIC_TRUST_CACHE_SHA256 = 'b7ab28e0009900326ae685a88748008a9653e6bcbd70674135dac4eb5231ad64'
STATIC_TRUST_CACHE_COUNT = 2760
STATIC_TRUST_CACHE_SIZE = 24 + STATIC_TRUST_CACHE_COUNT * 24

u32 = lambda *v: struct.pack('<%dI' % len(v), *v)
u64 = lambda *v: struct.pack('<%dQ' % len(v), *v)
cstr = lambda s: s.encode() + b'\0'


def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, cstr(name))] + [(k, 0, v) for k, v in props]
    n.children = list(children)
    return n


def d22ap_product(path):
    """/product properties of the real iPhone X (D22AP) tree, minus the
    syscfg placeholders iBoot fills in. Apple artifact, kept out of git."""
    if not path or not os.path.exists(path):
        print(f'warning: {path} missing, no /product node', file=sys.stderr)
        return None
    root, _ = adt.parse(open(path, 'rb').read())
    for c in root.children:
        if dict((k, v) for k, f, v in c.props).get('name') == b'product\0':
            return [(k, v) for k, f, v in c.props if not f and k not in ('name', 'AAPL,phandle')]
    raise SystemExit(f'{path}: no /product node')


def factory_address(value, label):
    if not isinstance(value, str) or not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', value):
        raise ValueError(f'Factory {label} identity must contain six colon-separated octets')
    address = bytes.fromhex(value.replace(':', ''))
    if address in (bytes(6), b'\xff' * 6) or address[0] & 3:
        raise ValueError(f'Factory {label} identity must be a non-placeholder globally assigned unicast address')
    return address


def factory_device_identity(path):
    """Read private factory metadata without publishing identifiers in diagnostics."""
    try:
        with open(path, encoding='utf-8') as stream:
            identity = json.load(stream)
    except (OSError, ValueError) as error:
        raise ValueError('Cannot read private device identity JSON') from error
    if (not isinstance(identity, dict) or
            not {'wifi_mac', 'source'} <= identity.keys() or
            identity.keys() - {'schema_version', 'wifi_mac', 'source', 'device_serial', 'captured_utc',
                               'bluetooth_mac', 'bluetooth_source'} or
            ('schema_version' in identity and
             (type(identity['schema_version']) is not int or identity['schema_version'] != 1)) or
            identity['source'] != 'android:dumpsys_wifi_factory' or
            any(name in identity and not isinstance(identity[name], str)
                for name in ('device_serial', 'captured_utc'))):
        raise ValueError('Unsupported private device identity schema or provenance')
    properties = {'mac-address-wifi0': factory_address(identity['wifi_mac'], 'Wi-Fi')}
    if ('bluetooth_mac' in identity) != ('bluetooth_source' in identity):
        raise ValueError('Private Bluetooth identity requires both address and provenance')
    if 'bluetooth_mac' in identity:
        if identity['bluetooth_source'] != 'android:mtk_nvram_btradio_v1':
            raise ValueError('Unsupported private Bluetooth identity provenance')
        properties['mac-address-bluetooth0'] = factory_address(identity['bluetooth_mac'], 'Bluetooth')
    return properties


def factory_wifi_identity(path):
    return factory_device_identity(path)['mac-address-wifi0']


def verified_static_trust_cache(path):
    """Accept only the exact original packaged trust-cache payload."""
    try:
        with open(path, 'rb') as stream:
            payload = stream.read(STATIC_TRUST_CACHE_SIZE + 1)
    except OSError as error:
        raise ValueError('Cannot read private static trust-cache payload') from error
    if (len(payload) != STATIC_TRUST_CACHE_SIZE or
            hashlib.sha256(payload).hexdigest() != STATIC_TRUST_CACHE_SHA256):
        raise ValueError('Static trust-cache payload does not match pinned 20H392 software')
    version, = struct.unpack_from('<I', payload)
    count, = struct.unpack_from('<I', payload, 20)
    if version != 2 or count != STATIC_TRUST_CACHE_COUNT:
        raise ValueError('Unsupported static trust-cache version or entry count')
    previous = None
    for offset in range(24, len(payload), 24):
        cdhash = payload[offset:offset + 20]
        hash_type, flags, category, reserved = payload[offset + 20:offset + 24]
        # These are the values present in the pinned original set; category
        # values are preserved as metadata, not interpreted as authorization.
        if (hash_type not in (1, 2) or flags not in (0, 1) or
                category not in (0, 1, 2, 3, 4, 5, 18) or reserved != 0 or
                (previous is not None and cdhash < previous)):
            raise ValueError('Invalid static trust-cache entry or hash ordering')
        previous = cdhash
    return payload


def build(seed=None, d22ap=D22AP_ADT, device_identity=None, static_trust_cache=None):
    factory_identity = factory_device_identity(device_identity) if device_identity is not None else {}
    trust_cache = verified_static_trust_cache(static_trust_cache) if static_trust_cache is not None else None
    seed = seed if seed is not None else os.urandom(64)
    phandle = iter(range(1, 1000))
    ph = lambda: ('AAPL,phandle', u32(next(phandle)))

    chosen = node('chosen', [
        ph(),
        ('dram-base', u64(PHYS_BASE)),
        ('dram-size', u64(MEM_SIZE)),
        ('random-seed', seed),
        ('debug-enabled', u32(1)),
        ('firmware-version', cstr('x676c-shim')),
        ('system-firmware-version', cstr('x676c-shim')),
        # see topaz tools/mkadt.py for why userland needs each of these
        ('boot-manifest-hash', BOOT_MANIFEST_HASH),
        ('crypto-hash-method', cstr('sha2-384')),
        ('display-scale', u32(3)),
        ('unique-chip-id', struct.pack('<Q', X676C_ECID)),
        ('chip-id', u32(0x8015)),
        ('certificate-production-status', u32(0)),
        ('effective-production-status-ap', u32(0)),
        ('certificate-security-mode', u32(1)),
        ('effective-security-mode-sep', u32(1)),
    ], [node('memory-map', [ph()]),
        node('asmb', [ph(), ('lp-sip0', u64(0x6f)), ('lp-sip1', u32(1))])])
    for name, value in factory_identity.items():
        # MobileGestalt reads factory addresses directly from /chosen, before
        # probing drivers. Hardware metadata is independent of radio services.
        chosen.props.append((name, 0, value))
    if trust_cache is not None:
        chosen.props.append(('sweet-static-trust-cache', 0, trust_cache))

    cpu_nodes = []
    for i, mpidr in enumerate(CPU_MPIDRS):
        cpu_nodes.append(node(f'cpu{i}', [
            ph(),
            ('device_type', cstr('cpu')),
            ('compatible', cstr('arm,cortex-a55') + cstr('qcom,kryo470') + cstr('ARM,v8')),
            ('reg', u32(mpidr)),     # actual MPIDR Aff1:Aff0, NOT logical ID
            ('cpu-id', u32(i)),
            ('cluster-id', u32(0)),
            ('cluster-type', u32(0)),  # homogeneous CLUSTER_TYPE_SMP
            ('die-cluster-id', u32(0)),
            ('cluster-core-id', u32(i)),
            ('l2-cache-id', u32(0)),
            ('l3-cache-id', u32(0)),
            ('state', cstr('running' if i == 0 else 'stopped')),
            ('timebase-frequency', u32(TIMEBASE)),
            ('clock-frequency', u32(2000000000)),
            ('bus-frequency', u32(100000000)),
            ('memory-frequency', u32(2133000000)),
            ('peripheral-frequency', u32(TIMEBASE)),
            ('fixed-frequency', u32(TIMEBASE)),
        ]))
    cpus = node('cpus', [ph(), ('#address-cells', u32(1)), ('#size-cells', u32(0))], cpu_nodes)

    gic = node('gic', [
        ph(),
        ('compatible', cstr('arm,gic-v3')),
        ('interrupt-controller', b''),
        ('virtual-timer-intid', u32(VTIMER_INTID)),
        ('reg', u64(GICD - SOC_BASE, GICD_SIZE, GICR - SOC_BASE, GICR_SIZE)),
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
            # Match the real panel (FB_W x FB_H). Leaving the iPhone X
            # canvas (1125x2436) here made AppleCLCD present that logical
            # size over a 1080-wide buffer, so the console drew 1125 px per
            # row into a 4320-byte (1080 px) stride -- skew and overflow off
            # the bottom. Keep canvas == framebuffer for a clean console.
            ('canvas-width', u32(FB_W)),
            ('canvas-height', u32(FB_H)),
        ])])

    fbcon = node('sweet-fbcon', [
        ph(),
        ('reg', u64(FB, FB_SIZE)),
        ('width', u32(FB_W)),
        ('height', u32(FB_H)),
        ('stride', u32(FB_STRIDE)),
    ])

    # bsd/dev/ufs (patch 0141): absolute physical reg, like sweet-ufs
    ufs = node('x676c-ufs', [
        ph(),
        ('compatible', cstr('mediatek,mt8183-ufshci')),
        ('reg', u64(UFS, UFS_SIZE)),
        ('interrupt', u32(32 + UFS_SPI)),
        ('reset-base', u64(INFRACFG_AO)),
        # (assert offset, deassert offset, bit) for hci, unipro, crypto
        ('resets', u32(0x130, 0x134, 15, 0x140, 0x144, 7, 0x150, 0x154, 21)),
    ])

    defaults = node('defaults', [ph(), ('no-effaceable-storage', u32(1))])
    buttons = node('buttons', [
        ph(),
        ('button-names', cstr('volup') + cstr('voldown') + cstr('hold')),
        ('home-button-type', u32(2)),
        ('opposed-power-vol-buttons', u32(1)),
    ])

    product_props = d22ap_product(d22ap)
    if product_props:
        product_props = [(k, cstr('sRGB') if k == 'artwork-display-gamut' else v) for k, v in product_props]
    product = [node('product', [ph()] + product_props)] if product_props else []

    return node('device-tree', [
        ph(),
        ('compatible', cstr('D22AP') + cstr('iPhone10,3') + cstr('infinix,x676c') + cstr('xiaomi,sweet')
         + cstr('mediatek,mt6789') + cstr('AppleARM')),
        ('model', cstr('iPhone10,3')),
        ('target-type', cstr('D22')),
        ('target-sub-type', cstr('D22AP')),
        ('manufacturer', cstr('Apple Inc.')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
    ], [chosen, defaults, cpus, arm_io, fbcon, ufs, buttons] + product)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--dump', action='store_true')
    ap.add_argument('--d22ap', default=D22AP_ADT, help='iPhone X DeviceTree (for /product)')
    ap.add_argument('--device-identity', help='private JSON containing Android factory hardware metadata')
    ap.add_argument('--static-trust-cache', help='private raw payload of the pinned 20H392 static trust cache')
    a = ap.parse_args()
    try:
        blob = adt.serialize(build(d22ap=a.d22ap, device_identity=a.device_identity,
                                   static_trust_cache=a.static_trust_cache))
    except ValueError as error:
        ap.error(str(error))
    open(a.out, 'wb').write(blob)
    if a.dump:
        displayed = adt.parse(blob)[0]
        if a.device_identity is not None or a.static_trust_cache is not None:
            chosen = next(child for child in displayed.children if child.name == 'chosen')
            redacted = []
            for name, flags, value in chosen.props:
                if name in ('mac-address-wifi0', 'mac-address-bluetooth0'):
                    value = cstr('<private factory identity>')
                elif name == 'sweet-static-trust-cache':
                    value = cstr(f'<pinned static trust cache: {len(value)} bytes, SHA256 {hashlib.sha256(value).hexdigest()}>')
                redacted.append((name, flags, value))
            chosen.props = redacted
        adt.dump(displayed)
    print(f'{a.out}: {len(blob)} bytes', file=sys.stderr)
