# HelioDarwin

Running Apple's open-source XNU kernel (`xnu-8796`, Darwin 22 / iOS 16.7.16 `20H392`
from iPhone X `D22AP`) and unmodified iOS 16 userland on the **MediaTek Helio G99
(`MT6789`)**, target device **Infinix Note 12 2023 (`X676C`)**. Forked from
[MakrSas/ios-topaz](https://github.com/MakrSas/ios-topaz).

This is experimental bring-up on physical hardware. Many subsystems are partial or
in progress. The table below reflects what the committed boot logs actually show —
nothing is listed as working that a log does not back.

## Hardware status

| Subsystem | Status | Log | Notes |
| :--- | :--- | :--- | :--- |
| Boot & shim | Working | [`boot-0146`](logs/boot-0146-launchd-dyld.log) | Shim v4 unpacks the kernel payload, sets up GICv3, enters XNU at EL1. |
| CPU & SMP | Working | [`boot-0146`](logs/boot-0146-launchd-dyld.log) | ARMv8.2-A, 16 KB granules, secondary cores up via PSCI. |
| Storage (UFS) | Working | [`boot-0146`](logs/boot-0146-launchd-dyld.log) | Native MTK UFS 2.2 (`0x11270000`), journaled HFS+ read/write on a 20 GB `iosroot`. |
| Display scanout | Working | [`boot-0176`](logs/boot-0176-mtk-input-malimetal.log) | Native DSI0 (`0x14013000`) + DISP_MUTEX0 (`0x14001000`) 60 Hz scanout from XNU; CPU display pump removed (patch 0177). |
| Userland (launchd→SpringBoard) | Partial | [`boot-0175`](logs/boot-0175-springboard-backboardd.log) | `launchd` spawns `lsd`, `backboardd`, `SpringBoard`. `containermanagerd` reliability has been a blocker; a fully composed, interactive lockscreen is not independently verified. |
| GPU power-on | In progress | [`boot-0148`](logs/boot-0148-gpu-mfg.log) | MFG power sequence reverse-engineered from stock `mtk_gpufreq_mt6789.ko` (SPM MTCMOS `0x10006308+`) and rewritten; the Mali bank still reads `0x0` — not yet confirmed responding. See [GPU power sequence](wiki/MT6789-GPU-Power-Sequence.md). |
| Metal driver | CPU 2D only | [MaliMetal](wiki/MaliMetal-Driver.md) | `MaliMetal.bundle` exposes `MTLDevice`/etc. with a CPU 2D compositor. No GPU-accelerated 3D, no Vulkan/panvk — those are a design proposal, not code. |
| Touchscreen | Driver polling | [`boot-0176`](logs/boot-0176-mtk-input-malimetal.log) | MT6789 I2C (`0x11e00000`, addr `0x38`), FocalTech FT3418/FT5452, 100 Hz poll; physical-touch capture not yet shown in a log. |
| Physical keys | Working | [`boot-0176`](logs/boot-0176-mtk-input-malimetal.log) | MT6789 keypad (`0x10010000`): Vol±, Power. |
| Thermal/power | Optimized | — | CPU 7 pump removed; `wfe`/event-stream idle in the shim. |
| NFC | Address mapped | — | NXP PN553 on I2C-3 (`0x11015000`, addr `0x28`); no driver yet. |
| Wi-Fi / BT / audio | Not started | — | MediaTek Connsys partitions located; nothing implemented. |

## Repository layout

- `x676c/` — device shim, ADT/payload/boot/flash tooling.
- `xnu/patches/` — numbered XNU source patches applied by CI before the kernel build.
- `MaliMetal/` — userspace Metal ICD bundle (CPU 2D compositor today).
- `wiki/` — hardware notes and reverse-engineered register maps.
- `logs/` — captured `pstore` boot logs that back the status table.

## Building and flashing

Requirements: LLVM/Clang 15+ (AArch64), Python 3.10+ with Zig, Android platform-tools
(`adb`) with root (Magisk). The kernel is built in CI; `boot-xnu.ps1` downloads the
artifact, builds the ADT/payload/boot image, flashes `boot_a` as slot A with a one-try
watchdog fallback to Android, then collects the `pstore` log.

```bash
pwsh x676c/tools/boot-xnu.ps1 -Run <CI_RUN_ID>
```

## GPU / Metal status

The kernel GPU power-on is being brought up; the reverse-engineered MT6789 sequence is
in [`wiki/MT6789-GPU-Power-Sequence.md`](wiki/MT6789-GPU-Power-Sequence.md). Userspace
`MaliMetal` is a CPU 2D compositor that satisfies `Metal.framework` during boot. A
Metal→Vulkan path over Mesa `panvk` is a long-term **design proposal**
([`wiki/MaliMetal-Driver.md`](wiki/MaliMetal-Driver.md)), not implemented.

## Development

HelioDarwin is developed by **[@squeeeezy](https://github.com/squeeeezy)**. AI assistance is utilized strictly for boilerplate generation, build script orchestration, and documentation formatting. All kernel architecture, register reverse engineering, driver implementations, and hardware test validation are conducted directly on physical Infinix Note 12 2023 (`X676C`) test hardware.

## Credits

- [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet) — XNU booting on Android silicon (Snapdragon / Redmi Note 10 Pro).
- [MakrSas/ios-topaz](https://github.com/MakrSas/ios-topaz), [topaz-woa](https://github.com/MakrSas/topaz-woa) — upstream bring-up scripts and drivers for Redmi Note 12.
- Apple — open-source XNU (`xnu-8796`) and Apple Device Tree formats.
- Mesa / Panfrost / PanVK — open-source Vulkan for Mali Valhall.
- Linux MediaTek contributors — device trees, power domains, clock drivers.

## Legal

Independent, non-commercial research project. This repository contains original code
(boot shim, drivers, tooling), reverse-engineering notes, and source patches against
Apple's open-source XNU. It does **not** include or redistribute Apple or MediaTek
proprietary binaries, firmware, or operating-system images; those must be supplied by
the user from their own device/licences. Apple's XNU source is used under the Apple
Public Source License 2.0. All trademarks belong to their respective owners.
See [`LICENSE`](LICENSE).
