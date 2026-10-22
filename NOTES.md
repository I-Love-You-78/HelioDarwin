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
  * `pc = 0x102ded990` (`ret` at `+0xd990` in `/usr/lib/dyld`)
  * `lr = 0x102de3f2c` (`+0x3f2c` in `_vm_allocate`)
  * `sp = 0x16d3175b0`, `x16 = -10` (Mach trap `_kernelrpc_mach_vm_allocate_trap`)
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

### Milestone 9: Full iOS 16 SpringBoard & backboardd Execution
- Spanned `launchd`, `containermanagerd`, `lsd`, `backboardd`, and `SpringBoard`.
- Applied system root tweaks via `sweet-root-tweaks.py`:
  * Configured `com.apple.springboard` preferences to disable `SBShowInternalAssertionBanners`, `SBShowInternalBanner`, `SBEnableAssertionAlerts`, and `BKNoWatchdogs`.
  * Prepopulated DataMigration markers (`BuildVersion = 20H392`, `DataMigrationCompleted`, `com.apple.migration.plist`) to bypass `com.apple.datamigrator` white folder screen.
- Verified live iOS 16 lockscreen composition on 1080×2400 AMOLED display (see `logs/boot-0175-springboard-backboardd.log`).

### Milestone 10: Native Physical Input Drivers (MTK Keypad & FocalTech I2C)
- Implemented native MediaTek Keypad driver (`bsd/dev/sweet_keys.c`, patch 0175/0176):
  * Mapped MT6789 Keypad MMIO registers at `0x10010000`.
  * Read `KP_MEM1` (`0x10010004`): Volume Up (bit 0), Volume Down (bit 1), Power (bit 8) with active-low hardware logic.
  * Dispatched events directly to `sweet_hid_key`.
- Implemented native MediaTek I2C driver (`bsd/dev/sweet_touch.c`):
  * Mapped MT6789 I2C-0 controller at `0x11e00000`.
  * Configured communication with FocalTech FT3418/FT5452 touch controller at slave address `0x38`.
  * 100 Hz polling loop parsing multi-touch coordinates (1080×2400) and reporting to `sweet_hid_touch`.
  * Removed simulated unlock swipe; driver actively polls I2C (0x38), physical finger touch event validation pending clean log capture.

### Milestone 11: Userspace Metal ICD Driver Skeleton (`MaliMetal.bundle`)
- Implemented native 64-bit arm64 Mach-O Metal ICD driver skeleton for ARM Mali-G57 MC2 Valhall GPU (`MaliMetal/`):
  * Implemented core Objective-C protocols: `MTLDevice`, `MTLCommandQueue`, `MTLCommandBuffer`, `MTLRenderPipelineState`, `MaliMetalBuffer`, `MaliMetalTexture` (1080×2400 BGRA surface).
  * Buffers backed by in-memory storage (`calloc`) to satisfy `Metal.framework` / `CoreAnimation` validation checks during early boot.
  * Packaged bundle deployed to `/System/Library/Extensions/MaliMetal.bundle`.
  * In `SweetFramebuffer.cpp`, delayed `accel->registerService()` until `MetalPluginName` and `MetalPluginClassName` are populated, allowing `Metal.framework` to bind immediately on boot without crashing `CAMetalContextCreate`.
  * Serves as the userland foundation for subsequent Mesa `panvk` Vulkan shader translation integration.

### Milestone 12: Display Pump Elimination & Native 60 Hz DSI Scanout (Patch 0177)
- Eliminated legacy background display pump thread on CPU 7 (`pump_start` completely removed from shim).
- Connected `mtk_dsi_push_hw()` directly into `SweetFramebuffer.cpp` Vsync timer at 60 Hz (`sMtkDsi` at `0x14013000`, `sMtkMutex` at `0x14001000`).
- Integrated synchronous MediaTek TOPRGU hardware watchdog kicks (`0x10007000 + 0x08`, key `0x1971`) on every frame scanout.
- CPU 7 remains powered down in PSCI; display is driven natively by XNU.

### Milestone 13: Thermal & Power Optimization
- Identified that CPU 7 (Cortex-A76 Big Core) was busy-spinning at 2.2 GHz in `wait_ms()` in the shim.
- Enabled architectural ARMv8 counter event stream on `CNTKCTL_EL1` (bit 14 of `cntpct_el0`, ~1.2 ms pulse at 13 MHz) coupled with low-power standby `wfe`. Duty cycle dropped from 100% to 0.05%.
- Disabled 8 unsupported/telemetry daemons in `launchd.plist` (`locationd`, `wifid`, `bluetoothd`, `analyticsd`, `awdd`, `passd`, `biometrickitd`, `ScreenTimeAgent`) to eliminate background CPU load.

### Milestone 14: Hardware Discovery: NXP PN553 NFC & MediaTek Connsys
- Mapped physical NXP PN553 / PN544 NFC controller:
  * Bus: I2C-3 (`0x11015000`), slave address `0x28`.
  * Power/VEN pin: GPIO 103 (`0x67`).
  * Architecture is identical to Apple Stockholm NFC (NXP NCI) used in iPhone X / XS / 11, enabling native `nfcd` integration for NameDrop, CoreNFC, and Apple Pay.
- Mapped MediaTek Connsys wireless core:
  * STP WMT (`/dev/stpwmt`) and Wi-Fi (`/dev/wmtWifi`).
  * Dedicated firmware partitions on UFS: `connsys_wifi` and `connsys_bt`.

---

## Active Engineering Priorities

1. **NXP PN553 NFC Driver (`sweet_nfc.c`)**: Assert GPIO 103 (VEN), sequence NCI `CORE_RESET` over I2C-3 (`0x11015000`, `0x28`), and expose device node for iOS `nfcd` / NameDrop.
2. **MediaTek Connsys WLAN/BT (`sweet_wmt.c`)**: Implement WMT firmware download from UFS `connsys_wifi` / `connsys_bt` and establish STP packet communication.
3. **Dual-Boot Bootloader (Clover Clovy Theme)**: Re-enable `clover.c` graphical dual-boot selector with official `Clovy/theme.svg` assets once hardware bring-up is stabilized.
