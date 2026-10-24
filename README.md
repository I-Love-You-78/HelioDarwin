# HelioDarwin

Running Apple's open-source XNU kernel (`xnu-8796`, Darwin 22 / iOS 16.7.16 `20H392`
from iPhone X `D22AP`) and iOS 16 userland on the **MediaTek Helio G99
(`MT6789`)**, target device **Infinix Note 12 2023 (`X676C`)**. Forked from
[MakrSas/ios-topaz](https://github.com/MakrSas/ios-topaz).

This is experimental bring-up on physical hardware. Booting the system, displaying
its interface, and executing graphics on the GPU are separate milestones. The
latest observed clock/interface uses the system's software rendering fallback;
the new MaliMetal GPU execution path has not yet been validated on the phone.
Userspace configuration changes and optional compatibility adapters are described
in the [system services documentation](wiki/System-Services-and-Posters.md).

## Hardware status

| Subsystem | Evidence and current limits |
| :--- | :--- |
| Boot & CPU | Physical boots reach iOS userland. The ADT publishes six Cortex-A55 cores with 16 KiB pages; the two A76 cores are excluded. [Boot and Shim](wiki/Boot-and-Shim.md). |
| Storage | Native UFS reads the `iosroot` HFS+ volume and has performed writes and journal replay. Root-volume backups must be kept separately. |
| Display | Patch 0190 serializes the vendor DSI refresh sequence. A subsequent phone boot showed the clock/interface and no sampled DSI busy condition. A 60 Hz software timer is not a measured frame-rate guarantee. [Display Pipeline](wiki/Display-Pipeline.md). |
| UIKit geometry | Physical scanout is 1080×2400. Native cutout, safe-area and status-bar compatibility work is separate from scanout and requires runtime validation. |
| GPU | XNU has read GPU ID `0x90930010`. The integrated boot verified PMIC enable/readback and loaded PanVK. GPU memory, jobs and rendering remain unvalidated. [GPU power sequence](wiki/MT6789-GPU-Power-Sequence.md). |
| MaliMetal | Metal objects, native AIR translation, a Vulkan backend and an arm64 iOS PanVK port are implemented for a bounded feature set. Selected real CoreAnimation shaders pass offline translation checks. End-to-end GPU execution and broader Metal support remain unverified/incomplete. [Driver details](wiki/MaliMetal-Driver.md). |
| Touch & keys | The replacement FT5452J, keypad and PMIC-key paths are compile-checked against extracted vendor information. Physical events through iOS HID still require phone validation. [Input and HID](wiki/Input-and-HID.md). |
| System services | PosterBoard startup preparation and reversible job management are available. Disabled services are not considered working merely because their definitions are restored. [Services and posters](wiki/System-Services-and-Posters.md). |
| Reset & fallback | The opt-in X676C TOPRGU reset returned a diagnostic boot to Android slot B. A/B retry metadata alone cannot reset a hung system. [Dual-boot safety](wiki/Dual-Boot-Safety.md). |
| Wi-Fi / BT / NFC / cellular / audio | Functional MT6789 support is not established. Hardware addresses or discovered vendor files do not constitute working drivers. |

## Repository layout

- `x676c/` — device shim, ADT/payload/boot/flash tooling.
- `xnu/patches/` — numbered XNU source patches applied by CI before the kernel build.
- `MaliMetal/` — userspace Metal implementation, AIR compiler adapter and Vulkan/XNU backend.
- `tools/` — offline root preparation and explicit compatibility adapters.
- `wiki/` — public hardware and implementation documentation, mirrored to the separate wiki.
- `logs/` — selected historical `pstore` captures; not every local observation is published here.

## Building and flashing

Requirements: LLVM/Clang 15+ (AArch64), Python 3.10+ with Zig, Android platform-tools
(`adb`) with root (Magisk). The kernel is built in CI; `boot-xnu.ps1` downloads the
artifact, builds the ADT/payload/boot image, flashes `boot_a` and selects slot A with
one try. The next reset can return to Android slot B. Arrange a reset deadline or
manual recovery explicitly; the default command is not a hang watchdog.

```bash
pwsh x676c/tools/boot-xnu.ps1 -Run <CI_RUN_ID>
```

## GPU / Metal status

The current source implements Vulkan resource and command paths. Build matching
iOS artifacts from source, including the pinned AIR compiler and PanVK dependency.
Generated binaries are kept outside version control.
See [MaliMetal build instructions](MaliMetal/README.md).

Run `40083985422` passed the earlier user cache-register trap after rebuilding
assembly from the current headers. CI and deployment now check the assembled
startup policy. The kernel also reported real touch press/release events.
PanVK then failed during logical-device creation: that path still treated an
IOKit connection as a Unix descriptor. The current source opens a separate
IOKit client for each logical device. GPU execution and iOS gestures still
require successful device validation.
The following retry exposed a missing kernel `getService` implementation;
patch 0195 supplies the GPU provider for IOKit connection discovery.

Compilation and offline shader validation do not prove that the stock Metal loader,
PanVK, kernel GPU transport and presentation work together on the phone. Full
Metal or Vulkan conformance is not claimed. Framebuffer fetch, blending, broader
texture variants, depth/stencil and multisampling remain among the unsupported
paths.

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
