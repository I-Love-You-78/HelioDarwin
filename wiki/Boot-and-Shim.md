# Boot and Shim

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

Some compatibility strings retain earlier port class names, while product metadata identifies a D22AP/iPhone X profile. These are software compatibility choices; they do not describe the physical SoC. Display and input coordinates must follow the selected logical display profile.

## Returning to Android

One-attempt A/B metadata selects Android **after a reset**; it cannot reset a hung processor. The opt-in `sweetreboot=420` diagnostic thread starts after UFS bring-up, syncs filesystems and requests a TOPRGU software reset. Normal boots have no such deadline. See [Dual-Boot Safety](Dual-Boot-Safety.md) and [Pstore Debugging](Pstore-Debugging-on-MediaTek.md) for the tested boundary.

## Source

- [Shim entry and loader](https://github.com/squeeeezy/HelioDarwin/tree/x676c/x676c/shim)
- [ADT builder](https://github.com/squeeeezy/HelioDarwin/blob/x676c/x676c/tools/mkadt.py)
- [Boot-image deployment helper](https://github.com/squeeeezy/HelioDarwin/blob/x676c/x676c/tools/boot-xnu.ps1)
