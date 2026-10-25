# Boot and Shim

The X676C deployment helper captures factory Wi-Fi and provisioned Bluetooth
metadata from rooted Android slot B before ADT construction. A private JSON file
supplies canonical six-byte `/chosen/mac-address-wifi0` and
`/chosen/mac-address-bluetooth0` properties; dumps redact them. Explicit identity
files must match the connected device, and the helper never substitutes a current
randomized interface address. Values are not embedded in repository source.

The Bluetooth reader validates the reviewed MediaTek vendor libraries, record
layout, checksum and byte order. This is provisioned controller metadata, not an
independent eFuse read. No Wi-Fi/Bluetooth driver, ECID/version change or Apple
authentication follows from these properties. This closes a demonstrated missing
MobileGestalt input; the next run returned from the previously blocked
query, while provider registration exposed a separate signed-cache contract.

`boot-xnu.ps1` also requires `-StaticTrustCache PRIVATE_FILE`, defaulting to
the ignored `x676c/research/20H392-static.trustcache`. Supply the original raw
payload from the D22AP 20H392 restore archive's
`Firmware/087-86698-044.dmg.trustcache` IM4P container. The payload is 66,264 bytes,
version 2 with 2,760 entries; its SHA256 is
`b7ab28e0009900326ae685a88748008a9653e6bcbd70674135dac4eb5231ad64`.
The host validates that exact payload before publishing
`/chosen/sweet-static-trust-cache`; the kernel checks it independently.
Offline construction uses `mkadt.py --static-trust-cache PRIVATE_FILE`.
Without this optional offline argument the prior tree is unchanged. The firmware
payload and generated ADT stay outside Git; dumps summarize the cache instead
of printing its entries. Payload validation does not prove runtime admission.


HelioDarwin boots XNU on the Infinix Note 12 2023 (X676C / MT6789) through the stock MediaTek boot chain. The shim is packaged as an Android boot image v4; it supplies the Apple Device Tree (ADT), boot arguments and kernel entry state that this XNU port expects.

```text
BootROM / platform firmware -> MediaTek LK -> HelioDarwin shim
    -> XNU platform and IOKit startup -> UFS HFS+ root -> launchd
```

## Handoff

The host payload builder resolves kernel fixups and packages kernel segments, the ADT and command line. LK loads the Android image. The shim locates the payload in the initrd, checks its bounds, copies the segments into the selected RAM window, places the ADT and `boot_args` after them, and enters XNU with the MMU and caches off and the physical boot-argument address in `x0`.

The shim records CPU and UFS state in the persistent console and draws the initial splash. GIC and secondary-CPU initialization belong to the XNU platform path; the shim does not bring all eight cores into XNU. The current shim entry also does not start the old secondary-core display pump or its lifetime timer. See [Display Pipeline](Display-Pipeline.md) for panel presentation.

## CPU topology and page size

| Cores | MPIDRs | Current XNU configuration |
| --- | --- | --- |
| Six Cortex-A55 | `0x000`, `0x100`, `0x200`, `0x300`, `0x400`, `0x500` | Published as one homogeneous SMP cluster; normal boot arguments use `cpus=6` |
| Two Cortex-A76 | `0x600`, `0x700` | Excluded from the current ADT; eight-core mixed-topology operation is not validated |

This target is built for **16 KiB pages**. That is a choice of this XNU/iOS configuration, not a universal rule for every Darwin target. The shim logs `ID_AA64MMFR0_EL1`; successful execution on the configured A55 cores is the relevant bring-up evidence. An implemented CPU instruction set or page granule alone does not establish working mixed-core SMP.

## ADT and memory

`x676c/tools/mkadt.py` currently declares:

- Kernel-managed RAM: base `0x140000000`, size `0xffff0000`, exclusive end `0x23fff0000`, within DRAM rank 1.
- GIC distributor: `0x0c000000`, ADT window `0x40000`; redistributors: `0x0c040000`, window `0x200000`.
- Visible LK framebuffer: `0xfeabe000`, 1080 × 2400 pixels, 4320-byte stride; ADT mapping window `0xa00000`.
- UFS host: `0x11270000`, ADT window `0x2300`; root selection uses `rd=ufs` and the named `iosroot` partition.

The loader separately checks its allowed rank-1 bounds, ending at `0x23ffff000`. The ADT's declared RAM size is narrower. These are explicit port limits, not a complete map of all installed DRAM or firmware carveouts. See [Hardware Architecture](Hardware-Architecture.md).

CI and `boot-xnu.ps1` run `verify-startup.py` against the kernel Mach-O. It checks
the assembled SCTLR startup policy, including user cache queries, user cache
maintenance and SPAN. Assembly sources bypass the restored compiler cache after
a build retained the older register constant despite updated source headers.

Some compatibility strings retain earlier port class names, while product metadata identifies a D22AP/iPhone X profile. These are software compatibility choices; they do not describe the physical SoC. Display and input coordinates must follow the selected logical display profile.

## Returning to Android

One-attempt A/B metadata selects Android **after a reset**; it cannot reset a hung processor. The opt-in `sweetreboot=420` diagnostic thread starts after UFS bring-up, syncs filesystems and requests a TOPRGU software reset. Normal boots have no such deadline. See [Dual-Boot Safety](Dual-Boot-Safety.md) and [Pstore Debugging](Pstore-Debugging-on-MediaTek.md) for the tested boundary.

## Source

- [Shim entry and loader](https://github.com/squeeeezy/HelioDarwin/tree/x676c/x676c/shim)
- [ADT builder](https://github.com/squeeeezy/HelioDarwin/blob/x676c/x676c/tools/mkadt.py)
- [Boot-image deployment helper](https://github.com/squeeeezy/HelioDarwin/blob/x676c/x676c/tools/boot-xnu.ps1)
