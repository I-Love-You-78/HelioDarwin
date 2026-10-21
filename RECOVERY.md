# HelioDarwin: Recovery and Hardware Runbook

Target: **Infinix Note 12 2023 (`X676C`)**  
SoC: **MediaTek Helio G99 (`MT6789`)**  
Storage: **128 GB UFS 2.2 (MediaTek UFS Controller at `0x11270000`)**

---

## 1. Dual-Boot Slot Architecture

HelioDarwin utilizes Android's native A/B partition structure to guarantee complete isolation from the primary Android installation:

* **Slot B (Primary OS)**: Stock Android 12 / 13 (XOS) or GSI with Magisk root (`boot_b`, `vendor_boot_b`).
* **Slot A (Experimental OS)**: HelioDarwin kernel shim (`boot_a`).
* **Storage Partitioning**:
  * The UFS 2.2 storage (LU 2) GPT partition table contains 128 entries.
  * A 20 GiB partition (`iosroot`) was carved from the tail of `userdata`, leaving Android with over 87 GiB of personal storage.
  * Android user data and applications remain 100% untouched.

---

## 2. Bootloader 1-Try Watchdog (Zero-Brick Protection)

Every test kernel deployment via `boot-xnu.ps1` programs the Bootloader Control Block (`bctl`) partition at offset `misc + 2048` with **exactly 1 try**:

```text
Slot A: priority 15, tries 1, successful 0
Slot B: priority 14, tries 1, successful 1
```

* When the phone reboots, Little Kernel (LK) attempts to boot Slot A.
* If the kernel encounters any issue, panics, or completes its execution window (7 minutes), LK decrements `tries` to 0.
* On the next reset, LK automatically marks Slot A unbootable and **instantly boots Android on Slot B**!
* It is physically impossible for a bad kernel build to brick the device.

---

## 3. Manual Recovery & Bootloader Modes

If the device ever hangs or fails to respond:

### Method A: Hardware Forced Reset
* Hold **Power + Volume Down** for 10 seconds until the phone vibrates and resets.

### Method B: Little Kernel (LK) Boot Mode Menu
1. Power off the phone completely.
2. Hold **Power + Volume Up** simultaneously.
3. The screen will display the MediaTek LK Boot Mode Menu:
   ```text
   [Select Boot Mode]
   [Recovery Mode]
   [Fastboot Mode]
   [Normal Boot]  <=
   ```
4. Use Volume Up to navigate to `[Fastboot Mode]` and Volume Down to select.
5. In Fastboot mode, you can flash or switch active slots via standard fastboot commands:
   ```bash
   fastboot set_active b
   fastboot reboot
   ```

### Method C: MediaTek BROM Mode (Deep Recovery)
If fastboot is inaccessible, the MT6789 BootROM is hardcoded into silicon and cannot be corrupted:
1. Disconnect the USB cable and power off the device.
2. Hold **Volume Up + Volume Down**.
3. Connect the USB-C cable to a PC running `mtkclient` or SP Flash Tool.
4. The SoC will enter BROM mode (USB ID `0e8d:0003`), allowing direct reading/writing of any partition.

---

## 4. Reading Persistent Crash Logs (`pstore`)

HelioDarwin mirrors all kernel console output, panic dumps, and userland registers into physical RAM carveout at **`0x4811f000`** (`pstore ramoops`):

When the phone falls back into Android, pull the persistent trace via ADB:
```bash
adb shell su -c "cat /sys/fs/pstore/console-ramoops-0" > xnu-crash.log
```
Or use the automated deployment script which collects and saves it automatically:
```powershell
pwsh x676c/tools/boot-xnu.ps1 -Run <RunID>
```
