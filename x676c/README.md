# x676c: ios-topaz on the Infinix Note 12 2023 (X676C, MT6789)

Port of this repository's XNU bring-up from the Redmi Note 12 4G (Qualcomm
SM6225) to the Infinix X676C: MediaTek Helio G99 (MT6789), 6x Cortex-A55 +
2x Cortex-A76, 8 GB, SK hynix UFS, ili7835 DSI command-mode panel.

## Status

- **Milestone 1 (2026-10-21): own code runs and paints the screen.**
  `shim/` boots in place of the kernel, logs to pstore and pushes
  red/green/blue frames to the command-mode panel.
- **Milestone 2 (2026-10-21): xnu loader verified on hardware** with a
  stand-in kernel (`tools/stub`): payload found in LK's initrd, segments
  placed at 0x140000000+, ADT + boot_args (rev 2) handed over, entry
  reached with x0 = boot_args. Display pump runs on CPU 0x700.

- **Milestone 3 (2026-10-21): xnu boots to the root wait.** CI kernel with
  `xnu/patches/0140-x676c-mt6789-bringup.patch`: early console on the
  MediaTek splash layer, every console byte mirrored into pstore, Qualcomm
  MMIO users off on `infinix,x676c`. The log runs through IOKit (platform
  expert, AppleKeyStore, HID, CPU0 + GIC SGI), BSD (AMFI, sandbox, HFS,
  networking) and ends in `Waiting on ... IOMedia ... Apple_HFS`.

## UFS host as LK leaves it

Dumped by the shim before entering xnu: CAP 0x3587071f (32 transfer slots,
8 task slots, 64-bit DMA), HCE 1, HCS 0x10f (device present, UTRL/UTMRL/UIC
ready), LK's list at UTRLBA 0x51252c00 idle (DBR 0, RSR 1), last UIC command
DME_SET PA_PWRMode (0x1571), REFCLK_CTRL 0x3 (requested and acked), MTK IP
version 0x20160706. Clocks, regulators and the HS link are up.

## xnu boot flow

```
python tools/mkadt.py shim/out/x676c.adt [--d22ap research/d22ap.adt]
python tools/mkpayload.py KERNEL shim/out/x676c.adt shim/out/payload.bin --cmdline "..."
python shim/build.py shim/out/payload.bin      # payload rides as the boot.img ramdisk
```

- Kernel RAM: **0x140000000 + 0xffff0000** (rank 1, between the dramc
  training pages 0x13ffff000 / 0x23ffff000; every other carve-out is
  below 4 GiB). Same values in `mkadt.py` and `shim/loader.c`.
- GICv3: GICD 0x0c000000, GICR 0x0c040000; virtual timer PPI 11 = INTID 27;
  CNTFRQ 13 MHz; PSCI 1.0 over SMC.
- CPUs in the ADT: the six A55 (MPIDR 0x000-0x500). 0x600 (A76) is held
  back until the AMP topology is set up; **0x700 (A76) runs the display
  pump** and must never be given to xnu.
- Display pump (`shim/display.c`): pushes a frame every 33 ms and kicks
  the MTK watchdog (xnu does not know it), then resets after 120 s so a
  hung boot falls back to Android.
- `tools/stub`: kernel-shaped Mach-O that logs the boot_args/ADT it got
  to pstore and resets. Use it to test loader changes without a kernel.

## Boot path facts

- Stock Transsion LK. No `fastboot boot` ("unknown command"). The kernel in
  boot.img v4 must be a **gzip** Image; a raw Image is rejected.
- LK enters at **EL1**, MMU off, Image at **0x40000000**, x0 = DTB
  (0x47c80000 seen).
- CNTFRQ 13 MHz. ID_AA64MMFR0 = 0x101122: **16K granule supported** on the
  A55/A76 cores.
- Escape hatch: Vol-Up + Power at power-on opens LK's "Select Boot Mode"
  menu (Recovery / Fastboot / Normal).

## Test loop without fastboot

Fastboot works, but Windows re-binds the shared USB ID 0E8D:201C to MTP
whenever Android boots, so WinUSB must be re-installed each time. Instead,
from rooted Android:

1. `dd` the shim image into the inactive `boot_a`.
2. Make slot A active by writing the AOSP `bootloader_control` block at
   `misc + 2048` with **1 try** (`tools/bootctl_block.py set-active a <hex> out.bin 1`).
   The bootctl HAL is not reachable on the GSI.
3. Reboot. The shim runs once, its reset uses up the try, and LK falls back
   to slot B (Android) by itself (~1 min round trip).
4. Read the shim log from `/sys/fs/pstore/console-ramoops-0`.

## Debug channel: ramoops

mem_address 0x48090000, size 0xe0000, record 0x1000, console 0x40000,
ftrace 0x1000, pmsg 0x10000, ecc 0 -> console zone at **0x4811f000**
(header `sig 0x43474244, start, size`). DRAM survives the warm reset, so
the next Android boot exposes it. See `shim/plog.c`.

## Display (command mode)

| Block | Base | State left by LK |
|---|---|---|
| DSI0 | 0x14013000 | MODE_CTRL 0 (CMD), PSCTRL 0x2c030ca8, CMDQ0 0x002c3909 (DCS 0x2c) |
| DISP_MUTEX | 0x14001000 | MUTEX0 MOD 0x147b7, EN 0 |
| OVL0_2L L0 | 0x14006000 | addr 0xfe0c8000, pitch 1088 px |
| OVL0 L1 (top, opaque, logo) | 0x14005000 | addr 0xfeabe000, pitch 1080 px |

One frame: `MUTEX0_EN = 1`, `DSI_START = 0 -> 1`; INTSTA reads 0x16 and
MUTEX0_EN self-clears. Pixels are ARGB8888 (0xffff0000 is red).

## Build

`python shim/build.py` (toolchain: `pip install ziglang`; links with
`zig ld.lld`) -> `shim/out/shim-boot.img`.

`tools/unpack_vendor_boot.py` extracts the vendor ramdisk (lz4 legacy) to
get the stock MediaTek kernel modules.
