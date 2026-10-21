# HelioDarwin

**Native Apple Darwin (XNU) & Metal-to-Vulkan Graphics Architecture on MediaTek Helio G99**

`HelioDarwin` is a standalone, bare-metal operating system project bringing Apple's open-source XNU kernel (`xnu-8796` / Darwin 22.x, iOS 16.7.16 `20H392` from iPhone X `D22AP`) to MediaTek ARM64 silicon, targeting the **MediaTek Helio G99 (MT6789)** on the **Infinix Note 12 2023 (`X676C`)**.

### Lineage & Upstream Roots

HelioDarwin directly branches from and builds upon the pioneering work of the Darwin-on-Android community:
* **[dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet)** by **dipa** — The breakthrough project that proved Apple's XNU kernel can boot on modern Android silicon (Qualcomm Snapdragon 730G / Redmi Note 10 Pro) and reach SpringBoard with display, touch, and Wi-Fi.
* **[MakrSas/ios-topaz](https://github.com/MakrSas/ios-topaz)** by **MakrSas** — Pioneering bring-up scripts, device tree definitions, and driver architecture on Redmi Note 12 (Snapdragon 685).

HelioDarwin takes these architectural lessons into completely uncharted territory: porting XNU to **MediaTek silicon** (MT6789) with command-mode DSI scanout, 4-domain MTCMOS power sequencing, and a proposed native Metal-to-Vulkan translation driver for ARM Mali Valhall GPUs.

---

## Engineering Transparency & Hardware Status

We believe in radical engineering honesty. Every status in the table below is backed by physical hardware boot logs captured via the MT6789 `pstore` RAM console (`0x4811f000`) and committed directly to the [`logs/`](logs/) directory.

| Subsystem | Target Scope | Hardware Status | Log Reference | Physical Verification Details |
| :--- | :--- | :---: | :--- | :--- |
| **Boot & Shim** | EL1 hand-off | **Verified** | [`boot-0146`](logs/boot-0146-launchd-dyld.log) | Shim v4 unpacks kernel payload, sets up GICv3, transfers control at EL1. |
| **CPU & SMP** | 8 cores, 16K pages | **Verified** | [`boot-0146`](logs/boot-0146-launchd-dyld.log) | ARMv8.2-A, 16 KB page granules, LSE atomics, secondary cores up via PSCI. |
| **Storage (UFS)** | UFS 2.2 host | **Verified** | [`boot-0146`](logs/boot-0146-launchd-dyld.log) | MTK UFS (`0x11270000`), SCSI command transport, journaled HFS+ read/write. |
| **Display Scanout**| Native HW scanout | **Verified** | [`boot-0147`](logs/boot-0147-gpu-probe.log) | DSI0 (`0x14013000`) & DISP_MUTEX0 (`0x14001000`) 60 Hz HW scanout to AMOLED. |
| **Userland Execution**| iOS 16 `launchd` | **Active Blocker** | [`boot-0146`](logs/boot-0146-launchd-dyld.log) | `launchd` executed, AMFI verified entitlements, dyld read shared cache from UFS; blocked in `vm_shared_region_start_address()`. |
| **GPU Subsystem** | Mali-G57 MC2 | **Bus Gated (boot-0148)**| [`boot-0148`](logs/boot-0148-gpu-mfg.log) | Issued writes to 4 domains in `mfg_top_config` (`0x13fbf000`); readback `0x0` due to `topckgen` clock gating. |
| **Metal-to-Vulkan**| Userspace ICD | **Research Proposal**| [MaliMetal](wiki/MaliMetal-Driver.md) | Study applying Darling/Indium AIR→SPIR-V translation to Mesa `panvk`. |
| **Touchscreen** | AMOLED Touch IC | **Planned** | — | I2C/SPI bus mapping on MT6789. |
| **Audio & Wireless**| Wi-Fi / BT / Sound | **Roadmap** | — | AudioReach / MT6631 wireless stack. |

---

## Development Notes

HelioDarwin is developed by **[@squeeeezy](https://github.com/squeeeezy)**. AI assistance is utilized strictly for boilerplate generation, build script orchestration, and documentation formatting. All kernel architecture, register reverse engineering, driver implementations, and hardware test validation are conducted directly on physical Infinix Note 12 2023 (`X676C`) test hardware.

---

---

## Verified Hardware Boot Logs

All milestone achievements are backed by physical hardware logs captured via the SoC's hardware `pstore` RAM console (`0x4811f000`):

* [**`logs/boot-0146-launchd-dyld.log`**](logs/boot-0146-launchd-dyld.log) — **Live iOS 16 Userland Execution**: Contains 59 consecutive SMP sampling traces of `launchd` and `dyld4` executing on physical Infinix hardware. Demonstrates Mach traps (`x16 = -10`), `vm_allocate` heap expansion, and Objective-C class registration.
* [**`logs/boot-0147-gpu-probe.log`**](logs/boot-0147-gpu-probe.log) — **Mali-G57 & DSI0 HW Probe**: Verifies MT6789 DSI0 / DISP_MUTEX0 scanout, MTCMOS SCPSYS power probe, and `IOAcceleratorES` registration.
* [**`logs/boot-0148-gpu-mfg.log`**](logs/boot-0148-gpu-mfg.log) — **MFG_TOP_CONFIG 4-Domain Sequencing Attempt**: Issued register writes to MT6789 MFG_TOP_CONFIG at `0x13fbf000`. Hardware log confirms register readback remains dead (`0x0`) due to `topckgen` (`0x10000000`) bus clock gating. Hardware sequencing logic is implemented directly in the XNU kernel (`SweetFramebuffer.cpp`, patch 0148), while the boot shim handles purely EL1 and GIC hand-off.

---

## Metal-to-Vulkan Architecture (`MaliMetal`) — Research & Design Notes

> [!NOTE]
> `MaliMetal` is an architectural design and research proposal adapting the prior art of **[darlinghq/indium](https://github.com/darlinghq/indium)** and **Iridium** (`mtl2spv` AIR-to-SPIR-V translation) to native Darwin/XNU for ARM Mali Valhall GPUs via Mesa's `panvk`. This is a long-term research effort (estimated 6–12 engineering months after kernel GPU bring-up) and is currently in early conceptual stages.

Standard iOS compositing (`CoreAnimation`, `backboardd`, `RenderServer`) strictly requires a Metal-capable hardware device. On non-Apple silicon, this has historically prevented native GUI execution.

HelioDarwin explores applying the Darling/Indium translation model within native XNU:

1. **Kernel Driver (`SweetMaliAccelerator` & `SweetMaliUserClient`)**:
   * Sequences MTCMOS power domains in MediaTek SCPSYS (`0x10006000`).
   * Un-gates graphics clock distribution via `MFGCFG` (`0x13fbf000`).
   * Exposes physical memory mapping and GPU command ringbuffers to user space.
   * Registers `IOAcceleratorES` in the IORegistry with:
     ```text
     MetalPluginName = "MaliMetal"
     MetalPluginClassName = "MaliMetalDevice"
     vendor-id = 0x13b5 (ARM)
     device-id = 0x9003 (Mali-G57 Valhall)
     ```

2. **Userspace ICD (`MaliMetal.bundle`)**:
   * Dynamically loaded by Apple's `Metal.framework` via `CFBundle`.
   * Implements the `MTLDevice` protocol.
   * Bridges Metal state, command buffers, and render passes directly into **Vulkan 1.3** primitives.
   * Compiles Apple Intermediate Representation (AIR / LLVM bitcode) and Metal Shading Language (MSL) into SPIR-V shaders executed by Mesa's `panvk` Valhall driver.
   * Backs framebuffers with zero-copy `IOSurface` memory mapped directly into the MediaTek DSI scanout hardware.

---

## Technical Reference & Wiki

Detailed hardware documentation and register maps are maintained in the [`wiki/`](wiki/Home.md):

* [**Wiki Home**](wiki/Home.md) — Technical reference index.
* [**Hardware Architecture**](wiki/Hardware-Architecture.md) — MediaTek MT6789 registers, SCPSYS power domains, memory maps.
* [**Boot & Shim**](wiki/Boot-and-Shim.md) — Little Kernel hand-off, PSCI core boot, 16K translation granule initialization.
* [**Display Pipeline**](wiki/Display-Pipeline.md) — MTK DSI0 and DISP_MUTEX0 hardware command-mode scanout to AMOLED.
* [**MaliMetal Driver**](wiki/MaliMetal-Driver.md) — Metal-to-Vulkan architectural design notes.
* [**Pstore Debugging**](wiki/Pstore-Debugging-on-MediaTek.md) — Zero-serial persistent RAM console debugging on MediaTek.
* [**Dual-Boot Safety**](wiki/Dual-Boot-Safety.md) — Slot A/B isolation and non-destructive deployment.

---

## Building and Flashing

### Requirements
* LLVM / Clang 15+ with AArch64 target support.
* Python 3.10+ and Zig toolchain.
* Android Platform Tools (`adb`, `fastboot`) with root permissions (Magisk).

### Building the Kernel
```powershell
# Compile the HelioDarwin XNU kernel locally
python tools/xnu-cc.py
```

### Deploying to Hardware
```powershell
# Flash kernel artifact to Slot A with automatic Android fallback
pwsh x676c/tools/boot-xnu.ps1 -Run <CI_RUN_ID>
```

---

## Credits and Acknowledgments

HelioDarwin stands on the shoulders of giants in the low-level open-source community:

* **[dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet)** — The pioneering project that proved Apple's XNU kernel can boot on modern Android silicon (Qualcomm Snapdragon 730G / Redmi Note 10 Pro).
* **[MakrSas/ios-topaz](https://github.com/MakrSas/ios-topaz) & [MakrSas/topaz-woa](https://github.com/MakrSas/topaz-woa)** — Groundbreaking hardware reverse engineering, bring-up scripts, and device drivers for the Redmi Note 12 platform.
* **[darlinghq/indium](https://github.com/darlinghq/indium)** & **Iridium** — Pioneering open-source Metal-over-Vulkan implementation and AIR-to-SPIR-V shader translation (`mtl2spv`), establishing the primary blueprint for Metal translation layers.
* **[darlinghq/darling-metal](https://github.com/darlinghq/darling-metal)** & **[steelbrain/metal2vulkan](https://github.com/steelbrain/metal2vulkan)** — Foundational open-source research and ABI compatibility for Metal emulation on Vulkan.
* **Apple Open Source** — Apple's release of the Darwin / XNU kernel source code (`xnu-8796`), `IOHIDFamily`, `libpthread`, and Apple Device Tree formats.
* **Mesa 3D Project & Panfrost / PanVK Contributors** — For pioneering open-source Vulkan support for the ARM Mali Valhall architecture.
* **Linux MediaTek Kernel Contributors** — Upstream device tree definitions, power domain architecture, and clock distribution documentation.

---

## Ethics and Legal Notice

HelioDarwin is an independent, non-commercial research project developed for architectural exploration and hardware interoperability. This repository contains only open-source kernel modifications, boot shims, and custom device drivers. It does **not** host, redistribute, or license proprietary Apple operating system files, firmware, or copyright-protected assets. All trademarks belong to their respective owners.
