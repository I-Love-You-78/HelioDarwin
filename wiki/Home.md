# HelioDarwin Technical Wiki

Reference notes for running Apple's XNU kernel (`xnu-8796`) and unmodified iOS 16
userland on the MediaTek Helio G99 (`MT6789` / Infinix `X676C`).

## Principles

1. **Don't break userspace.** iOS binaries (`launchd`, `dyld4`, `backboardd`,
   `SpringBoard`) run unmodified; the kernel must provide correct Mach/BSD/IOKit
   semantics rather than patch the binaries.
2. **Document the real hardware.** Every subsystem is recorded with physical base
   addresses, register offsets and the bring-up sequence actually observed on device.
3. **No overclaiming.** Status reflects what boot logs show. Where hardware is not yet
   working (e.g. the GPU is powered but the Metal path is a CPU compositor, not
   accelerated), the docs say so.

## Articles

| Article | Description |
| :--- | :--- |
| [Hardware Architecture](Hardware-Architecture.md) | MT6789 memory map and peripheral MMIO. |
| [MT6789 GPU Power Sequence](MT6789-GPU-Power-Sequence.md) | Reverse-engineered Mali-G57 power/clock bring-up and register map. |
| [Boot and Shim](Boot-and-Shim.md) | LK → shim → PSCI multicore → 16 KB MMU → XNU entry. |
| [Display Pipeline](Display-Pipeline.md) | MTK `DSI0` + `DISP_MUTEX0` scanout to the AMOLED panel. |
| [MaliMetal Driver](MaliMetal-Driver.md) | Metal→Vulkan design proposal (not implemented). |
| [Dual-Boot & Safety](Dual-Boot-Safety.md) | Slot A/B isolation and the one-try watchdog fallback. |

## Target hardware

- Device: Infinix Note 12 2023 (`X676C`)
- SoC: MediaTek Helio G99 (`MT6789`), 2× Cortex-A76 + 6× Cortex-A55, ARMv8.2-A, 16 KB granule
- Interrupts: ARM GICv3 (GIC-600)
- RAM: 8 GB LPDDR4X (base `0x40000000`)
- Storage: UFS 2.2 (`0x11270000`)
- Display: 1080×2400 AMOLED (ili7835) over MIPI DSI
- GPU: ARM Mali-G57 MC2 (Valhall), 2 cores; vendor product ID `0x9093`

## Credits

[dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet),
[MakrSas/ios-topaz](https://github.com/MakrSas/ios-topaz), Apple open-source XNU,
Mesa/Panfrost/PanVK, and Linux MediaTek kernel contributors.
