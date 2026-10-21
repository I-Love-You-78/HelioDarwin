# Dual-Boot Architecture and Safety Mechanisms

## 1. Overview in Plain English

One of the greatest fears when porting an operating system to a smartphone is "bricking" the device — leaving it in an unbootable state where user data is lost or the bootloader locks up.

HelioDarwin is engineered with a **non-destructive, zero-risk dual-boot architecture**. The user's primary Android installation, apps, personal data, and system partitions remain 100% untouched.

---

## 2. The 1-Try Watchdog Mechanism

Modern Android devices use **A/B seamless updates**. The flash storage has two sets of boot partitions:
* **Slot A**: `boot_a`, `vendor_boot_a`
* **Slot B**: `boot_b`, `vendor_boot_b`

Android normally boots and runs on **Slot B**. When we test a new HelioDarwin kernel:
1. `boot-xnu.ps1` writes our new shim image **only to `boot_a`**.
2. It modifies the hardware Boot Control Block (`bctl`) partition, setting Slot A with **exactly 1 try** (`tries = 1, successful = 0`):
   ```text
   [{'slot': 'A', 'priority': 15, 'tries': 1, 'successful': 0},
    {'slot': 'B', 'priority': 14, 'tries': 1, 'successful': 1}]
   ```
3. The phone reboots into Slot A and starts HelioDarwin.
4. If the kernel encounters any issue or finishes its session, the hardware bootloader automatically decrements `tries` to 0, marks Slot A unbootable, and **instantly falls back to Android on Slot B**!

It is physically impossible for a bad kernel build to brick the phone.

---

## 3. Persistent Logging via `pstore`

When Darwin runs, hardware UART serial logs can be difficult to capture without opening the phone and soldering debug wires. 

Instead, HelioDarwin mirrors all kernel `printf` and `IOLog` messages into the SoC's hardware **RAM console (`pstore`)**:
* Address: `0x54000000` (reserved memory carveout).
* When Darwin reboots back into Android, the kernel console buffer is preserved in RAM.
* `boot-xnu.ps1` immediately reads `/sys/fs/pstore/console-ramoops` via ADB and saves the full trace to `xnu-<RunID>.log`.

---

## 4. Flash and Boot Command Reference

```powershell
# Flash kernel artifact to Slot A with 1-try watchdog and collect logs
pwsh x676c/tools/boot-xnu.ps1 -Run <RUN_ID>
```
