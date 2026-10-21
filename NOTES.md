# HelioDarwin: Engineering Log & Milestone History

Target: **Infinix Note 12 2023 (`X676C`)**  
Platform: **MediaTek Helio G99 (`MT6789`)**  
Cores: **2× Cortex-A76 + 6× Cortex-A55 (ARMv8.2-A, 16 KB page granules)**  
Baseline: Apple XNU `xnu-8796` (Darwin 22.x, iOS 16.7.16 `20H392` / iPhone X `D22AP`)

---

## Hardware Bring-up Milestones

### Milestone 1: Bare-Metal Shim & Panel Validation
- Replaced standard Android kernel with HelioDarwin bare-metal shim (`shim/`, boot image v4).
- Verified Little Kernel (LK) hand-off: entered at EL1, MMU off, Image at `0x40000000`, DTB at `0x47c80000`.
- Verified 16 KB page granule support in hardware MMU (`ID_AA64MMFR0_EL1` reports `TGran16` supported on all 8 cores).
- Configured MIPI DSI0 (`0x14013000`) and DISP_MUTEX0 (`0x14001000`) to paint RGB test patterns onto the ili7835 1080×2400 AMOLED panel.
- Enabled ramoops console buffer at `0x4811f000` to capture early boot logs through Android warm reset.

### Milestone 2: Kernel Payload Loader & ADT Handover
- Packed Apple Device Tree (ADT generated via `tools/mkadt.py`) and XNU Mach-O payload into LK initrd ramdisk.
- Relocated kernel segments to physical RAM above 4 GiB (`0x140000000`+).
- Configured ARM GIC-600 (GICv3): GICD at `0x0c000000`, GICR at `0x0c080000` (stride `0x20000` per core), virtual timer PPI 11 (INTID 27).
- Woke secondary cores via PSCI `CPU_ON` (`0xc4000003`).

### Milestone 3: XNU Kernel Initialization & Console Mirroring
- Booted Apple XNU kernel (`xnu/patches/0140-x676c-mt6789-bringup.patch`).
- Initialized Mach VM with native 16 KB page tables across 8 GB DRAM.
- Disabled Qualcomm-specific peripherals; hooked console output into MT6789 pstore memory carveout.
- Boot reached root filesystem wait: `Waiting on ... IOMedia ... Apple_HFS`.

### Milestone 4: Native MediaTek UFS 2.2 Host Driver
- Implemented native kernel UFS host driver (`bsd/dev/ufs/`, patch 0141) targeting MT6789 UFS controller (`0x11270000`).
- Configured HS-G3 x2 rate B link training, negotiated UFS 2.2 protocol.
- Enumerated Logical Units: LU 0/1 (4 MiB boot LUs), LU 2 (128 GB User Data LU).
- Verified GPT partition table with 128 entries. Carved dedicated 20 GiB `iosroot` partition from storage without altering Android system partitions.
- Formatted `iosroot` as journaled HFS+ volume; verified block read/write operations and journal replay on physical hardware.

### Milestone 5: iOS 16 Userland Execution (launchd & dyld)
- Deployed unmodified iPhone X iOS 16.7.16 (`20H392`) root filesystem to `iosroot`.
- Boot arguments configured: `rd=ufs ufsroot=iosroot ufsrw=iosroot unentitled_ios_sim_launch=1 ignition_level=1 cs_debug=1`.
- Kernel successfully executed PID 1: `/sbin/launchd`.
- `launchd` invoked `/usr/lib/dyld` to link system libraries and shared cache.

### Milestone 6: Native Hardware DSI Scanout (AppleCLCD Integration)
- Replaced background CPU display pump thread with direct register integration in `SweetFramebuffer.cpp` (patch 0146).
- Mapped MT6789 `DISP_MUTEX0` (`0x14001000`) and `DSI0` (`0x14013000`).
- Hooked hardware frame push (`mtkDsiPush()`) to 60 Hz VSYNC timer tick and console flip routines, enabling fluid display scanout to the ili7835 AMOLED panel without CPU overhead.

### Milestone 7: Extended Execution & Userland State Sampling
- In `x676c/shim/main.c`, set `PUMP_LIFETIME_S = 420` (7-minute execution window) to allow dyld uninterrupted time to map the multi-hundred-megabyte `dyld_shared_cache` over UFS.
- Eliminated early sampling deadlock in `osfmk/kern/exception.c`.
- Captured 59 consecutive live user execution samples on physical device (see `logs/boot-0146-launchd-dyld.log`):
  * `pc = 0x104941990` (`ret` at `+0x11990` in `/usr/lib/dyld`)
  * `lr = 0x104937f2c` (`+0x7f2c` in `_vm_allocate`)
  * `x16 = -10` (Mach trap `_kernelrpc_mach_vm_allocate_trap`)
  * `x0 = 0x203` (task_self), `x2 = 0x44000` (272 KB allocation), `x3 = 0x3c000001` (`VM_FLAGS_ANYWHERE | VM_MAKE_TAG(VM_MEMORY_DYLD)`)
  * Call stack: `lsl::MemoryManager::vm_allocate_bytes` -> `dyld4::APIs::_dyld_for_each_objc_class`.
  * **Result**: `dyld` initialized its memory heaps and is actively mapping Objective-C classes.

### Milestone 8: Mali-G57 MC2 Valhall GPU Reverse Engineering & Bring-up
- Disassembled MediaTek GPU power driver (`mtk_gpufreq_mt6789.ko`) using Capstone:
  * Identified GPU clock configuration base: `mfg_top_config` at physical address `0x13fbf000` (size `0x1000`).
  * Identified core power control registers: Core 0 at `0x13fbf000 + 0x308`, Core 1 at `0x13fbf000 + 0x30c`.
  * Identified status registers: `0x13fbf000 + 0x16c` and `+ 0x170`.
  * Confirmed live GPU identity via device sysfs: `Mali-G57 2 cores r0p1 0x9093`.
  * Identified PMIC voltage rails: `vgpu` (regulator 8, target ~0.75V) and `vsram_gpu` (regulator 23, target ~0.75V).
- Published `IOAcceleratorES` in IORegistry with properties `MetalPluginName = "MaliMetal"` and `MetalPluginClassName = "MaliMetalDevice"`.

---

## Active Engineering Priorities

1. **GPU Power Rail Assertion**: Sequence `mfg_top_config` (`0x13fbf000 + 0x308/0x30c`) and PMIC regulators to ensure Mali registers at `0x13000000` respond with live hardware ID `0x9093`.
2. **Userspace Metal ICD (`MaliMetal.bundle`)**: Implement `MTLDevice` SPI backed by Mesa `panvk` (Vulkan 1.3) for Mali Valhall architecture.
3. **SpringBoard Compositing**: Establish zero-copy `IOSurface` framebuffering into `AppleCLCD` to render the iOS 16 lockscreen UI.
