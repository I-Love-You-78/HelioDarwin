# Zero-Serial Hardware Debugging on MediaTek via Pstore (Ramoops)

One of the greatest hurdles when porting an operating system (such as Darwin / XNU) to modern smartphone hardware is the **complete absence of an exposed UART / serial console**. 

Modern MediaTek platforms do not expose physical TX/RX test pads without high-risk board desoldering. If early kernel initialization panics, hangs, or faults, the device simply goes black, leaving developers in total darkness.

HelioDarwin solves this with a **zero-hardware-modification debug cycle** utilizing the SoC's persistent RAM console (`ramoops` / `pstore`), a 1-try bootloader fallback mechanism, and Android host recovery.

---

## 1. The Core Concept

When DRAM retains power across a warm reboot, physical memory contents survive unaltered. 

MediaTek Little Kernel (LK) reserves a dedicated slice of physical RAM for persistent kernel logs (`ramoops`):
* **Physical Base Address:** `0x4811f000` (MT6789 / Helio G99)
* **Buffer Size:** `0x100000` (1 MiB)
* **Structure:** Circular ASCII text ringbuffer with an atomic write pointer.

```
+-------------------------------------------------------------+
|                      Physical DRAM                          |
|                                                             |
|  [0x40000000] Kernel Payload / Shim                         |
|  [0x4811f000] Ramoops Persistent Buffer (1 MiB) <---------+ |
|               - Survives warm reboot                      | |
|               - Written atomically by XNU console         | |
|                                                           | |
+-----------------------------------------------------------+-+
                            |                               |
       Warm Reboot (Watchdog / Timer / Panic)               |
                            |                               |
                            v                               |
+-------------------------------------------------------+   |
|               Android Slot B Boots                    |   |
|                                                       |   |
|  Linux kernel mounts ramoops:                         |   |
|  /sys/fs/pstore/console-ramoops-0  -------------------+   |
|                                                           |
|  Host pulls log via ADB:                                  |
|  adb shell su -c "cat /sys/fs/pstore/console-ramoops-0"   |
+-------------------------------------------------------+
```

---

## 2. Kernel-Side Implementation (XNU)

In standard XNU, `kprintf()` emits characters to memory buffers or PE serial routines. In HelioDarwin, we bind the early console directly to the MTK ramoops buffer.

In `bsd/dev/sweet_ufs.c` / `SweetConsole.cpp`:

```c
#define PSTORE_RAMOOPS_BASE  0x4811f000ULL
#define PSTORE_RAMOOPS_SIZE  0x00100000ULL  /* 1 MiB */

static volatile char *pstore_buf = NULL;
static volatile uint32_t *pstore_head = NULL;

void
sweet_pstore_init(void)
{
    /* Map the physical ramoops DRAM slice into kernel virtual address space */
    pstore_buf = (volatile char *)ml_io_map(PSTORE_RAMOOPS_BASE, PSTORE_RAMOOPS_SIZE);
}

void
sweet_pstore_append(const char *buf, int len)
{
    if (!pstore_buf || len <= 0) return;
    
    for (int i = 0; i < len; i++) {
        uint32_t pos = __atomic_fetch_add(&global_pos, 1, __ATOMIC_RELAXED);
        pstore_buf[pos % PSTORE_RAMOOPS_SIZE] = buf[i];
    }
}
```

Because writes use lockless atomic index increments, `sweet_pstore_append` never deadlocks, even during multi-core interrupts, double faults, or fatal panics.

---

## 3. Non-Destructive 1-Try Slot A/B Watchdog

To prevent a crashing kernel from soft-bricking the phone into a bootloop, HelioDarwin leverages the MediaTek LK A/B slot metadata in the `misc` partition:

1. Android normally runs on **Slot B**.
2. When flashing a test build to `boot_a` (Slot A), the deployment script sets `slot_a.tries = 1` and `slot_a.successful = 0` in the `boot_control` block (at offset `2048` of `/dev/block/by-name/misc`).
3. LK boots Slot A exactly **once**:
   * Decrements `tries` from 1 to 0.
   * If XNU crashes, hangs, or panics, the SoC hardware watchdog or LK boot counter expires.
   * On reboot, LK inspects `misc`, sees `slot_a.tries == 0`, and **automatically falls back to healthy Android on Slot B**!

---

## 4. Fully Automated Test Pipeline (`boot-xnu.ps1`)

The entire cycle is fully automated end-to-end:

```powershell
# 1. Flash new kernel into Slot A with 1-try watchdog
pwsh x676c/tools/boot-xnu.ps1 -Run <ACTIONS_RUN_ID>

# 2. Script execution sequence:
#    a. Unpacks CI kernel artifact and compiles shim payload.
#    b. Updates misc block on device: slot A active, tries = 1.
#    c. Flashes boot_a and invokes 'adb reboot'.
#    d. Device boots XNU; runs kernel, drivers, launchd, dyld.
#    e. Watchdog triggers automatic reboot back into Android Slot B.
#    f. Script polls 'adb wait-for-device' and extracts:
#       adb shell su -c "cat /sys/fs/pstore/console-ramoops-0" > out/xnu-<Run>.log
#    g. Displays the last 60 lines of the live log on screen.
```

---

## 5. Summary of Benefits

| Traditional UART Approach | HelioDarwin Pstore Approach |
| :--- | :--- |
| Requires physical phone disassembly | Zero physical disassembly; 100% software |
| Risk of permanent hardware damage | Non-destructive; dual-boot protected |
| Hardware logic analyzer / FTDI required | Standard USB-C cable and ADB |
| Risk of unrecoverable BROM bootloops | Automatic LK fallback to Android Slot B |
| Slow manual logging setup | Instant automated CI-to-device test loop (~7 min) |
