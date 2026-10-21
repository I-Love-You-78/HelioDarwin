# Boot Pipeline: From Little Kernel to XNU

## 1. Overview in Plain English

When an Android phone boots, it runs a chain of firmware:
1. **BootROM** (burned into silicon by MediaTek).
2. **ATF (ARM Trusted Firmware)** / BL31.
3. **Little Kernel (LK)** / Android Bootloader (ABL).

In standard iOS, the Apple Secure Boot chain boots via `iBoot` directly into XNU. On our Infinix Note 12 2023, LK expects a standard Android `boot.img` containing a Linux kernel.

To bridge this gap cleanly without modifying the stock Little Kernel, HelioDarwin introduces **The Shim (`shim/`)**: an ultra-lightweight bare-metal bootloader packaged as an Android boot image v4. The shim receives control from LK, prepares the CPU and interrupt controller for Darwin's assumptions, unpacks the XNU kernel payload, and jumps straight into Apple's kernel entrypoint.

---

## 2. Boot Flow Diagram

```text
[ Stock MediaTek LK ]
         │ (Loads boot.img v4 into RAM)
         ▼
[ HelioDarwin Shim (start.S) ]
         │
         ├── Detect Boot CPU (Cortex-A76 or Cortex-A55)
         ├── Wake Secondary Cores via PSCI CPU_ON
         ├── Setup GICv3 Distributor & SGI1 IPIs
         ├── Verify 16 KB Page Granule Support (ID_AA64MMFR0)
         ├── Relocate Apple Device Tree (ADT) and Boot Args
         └── Unpack XNU Kernel Payload from RAM
         │
         ▼
[ Apple XNU Kernel (start_first_cpu) ]
         │
         ├── ml_arm_init() -> Setup 16K Translation Tables
         ├── PE_init_platform() -> Parse ADT device nodes
         ├── IOKit Startup -> Probes SweetUFS, AppleCLCD, SweetMali
         ├── Mount Root -> Mounts HFS+ volume over UFS 2.2
         └── bsd_init() -> Spawns PID 1 (/sbin/launchd)
```

---

## 3. Key Challenges and Solutions

### The 16 KB Page Granule Requirement
Darwin virtual memory (`vm_map`, `pmap`, `malloc`, `dyld_shared_cache`) is strictly architected around **16 KB pages**.
* On older SoCs (such as Qualcomm Snapdragon with Cortex-A53 cores), the CPU MMU does not support 16 KB page granules (`ID_AA64MMFR0_EL1.TGran16 == 0`), forcing painful core parking or emulation.
* On the **MediaTek Helio G99 (MT6789)**, both the big Cortex-A76 cores and the LITTLE Cortex-A55 cores natively support 16 KB translation tables in hardware! The shim verifies this register before jumping into XNU:
  ```assembly
  mrs   x0, id_aa64mmfr0_el1
  ubfx  x1, x0, #20, #4        /* TGran16 field */
  cbz   x1, unsupported_core   /* Must be non-zero */
  ```

### SMP Topology and Core Allocation
The MT6789 features 8 cores:
* Cores 0–1: Cortex-A76 @ 2.2 GHz (`MPIDR 0x000`, `0x100`)
* Cores 2–7: Cortex-A55 @ 2.0 GHz (`MPIDR 0x200`–`0x700`)

The shim configures the boot CPU and boots all 8 cores via standard ARM PSCI `CPU_ON` calls (`0xc4000003`), providing XNU with complete multicore SMP performance.
