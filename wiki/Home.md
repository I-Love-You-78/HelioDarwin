# HelioDarwin Technical Wiki

Welcome to the **HelioDarwin Documentation & Hardware Architecture Wiki**.

HelioDarwin is an engineering effort to run Apple's Darwin/XNU kernel (`xnu-8796`) and the genuine iOS 16 userland natively on MediaTek ARM64 silicon (MT6789 / Helio G99), alongside the first native **Metal-to-Vulkan** hardware graphics driver (`MaliMetal`) for ARM Mali Valhall GPUs.

---

## Technical Philosophy

In the spirit of Linus Torvalds and classical Unix OS design:
1. **Never Break Userspace**: The kernel must provide exact Mach/BSD/IOKit semantics. Userland binaries (`launchd`, `dyld4`, `backboardd`) are completely unmodified Apple binaries and execute without shims or emulation.
2. **Hardware Reality Over Abstraction**: Every subsystem is documented with exact physical base addresses, register offsets, clock IDs, and bit-level sequence timing.
3. **Clean Drivers Over Wrappers**: No dirty software rasterizers or fake framebuffers. Hardware command-mode DSI scanout and true GPU command buffers.

---

## Architecture Chapters

| Article | Description |
| :--- | :--- |
| [**Hardware Architecture**](Hardware-Architecture.md) | MediaTek MT6789 (Helio G99) silicon specifications, memory layout, SCPSYS power domains, and clock gates. |
| [**Boot and Shim**](Boot-and-Shim.md) | Boot pipeline: Little Kernel (LK) → Shim v4 → PSCI multicore bring-up → 16 KB MMU translation → XNU entry. |
| [**Display Pipeline**](Display-Pipeline.md) | Hardware scanout via MTK `DSI0` (`0x14013000`) and `DISP_MUTEX0` (`0x14001000`) to the ili7835 AMOLED panel at 60 Hz. |
| [**MaliMetal Driver**](MaliMetal-Driver.md) | World-first Metal-to-Vulkan translation driver: IOKit accelerator, MTLDevice SPI, AIR-to-SPIR-V shader translation, Mesa panvk. |
| [**Dual-Boot & Safety**](Dual-Boot-Safety.md) | Non-destructive dual-booting with Android using Slot A/B isolation and the 1-try watchdog mechanism. |

---

## Target Hardware Specifications

* **Target Device:** Infinix Note 12 2023 (`X676C`)
* **SoC:** MediaTek Helio G99 (`MT6789`)
* **Cores:** 2× Cortex-A76 @ 2.2 GHz (Big) + 6× Cortex-A55 @ 2.0 GHz (LITTLE)
* **Instruction Set:** ARMv8.2-A with hardware LSE atomics and native 16 KB page granule support
* **Interrupt Controller:** ARM GIC-600 (GICv3)
* **RAM:** 8 GB LPDDR4X (Physical base `0x40000000`)
* **Storage:** 128 GB UFS 2.2 (`0x11270000`)
* **Display:** 6.7" AMOLED (1080×2400) with ili7835 controller over MIPI DSI
* **GPU:** ARM Mali-G57 MC2 (Valhall architecture, 2 execution cores, ID `0x9093`)
