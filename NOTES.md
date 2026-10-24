# HelioDarwin engineering status

Target: Infinix Note 12 2023 (`X676C`), MediaTek Helio G99 (`MT6789`).
Kernel baseline: XNU `xnu-8796`; userspace: iOS 16.7.16 (`20H392`, D22AP).
This page summarizes the current implementation and observed milestones.

## Observed milestones

- LK enters the shim at EL1 with MMU off. The payload loader places XNU and its
  ADT/boot arguments in the configured rank-1 memory window.
- XNU reaches IOKit/BSD startup, mounts the named HFS+ root over the native UFS
  driver and executes launchd. Captures include SpringBoard and backboardd.
- XNU has read Mali `GPU_ID=0x90930010` and availability registers. Register
  access and verified PMIC/MFG power-on establish the GPU power-on path.
  Shader execution remains unverified.
- Following patch 0190, the physical panel displayed the clock/interface.
  A bounded diagnostic boot also returned automatically to Android B using
  TOPRGU. The observed UI used system software rendering.

## Current source

The ADT publishes six A55 cores, GICR base `0x0c040000`, 16 KiB CPU pages and
RAM base `0x140000000` with size `0xffff0000`. Both A76 cores remain excluded.
The old secondary-core display pump and its lifetime timer are not started.

The GPU source uses the shared split-SWINF PMIC interface, preserves inherited
voltage selectors and implements a versioned JM memory/submission/completion
transport. Rail-enable readbacks and native Vulkan device creation have been
observed on the phone. GPU page walks, execution and recovery still require
validation. Full DVFS and dynamic GPU power management remain open.

Input source targets the confirmed FT5452J controller over I2C0/APDMA, the PMIC
power key and keypad volume keys. UFS startup starts the readers; HID receives
contact collections and consumer-button events. Kernel logs contain physical
touch and power/volume-key press/release events, and touch interaction was
confirmed. Multi-touch, volume behavior and display sleep/wake remain unvalidated.

MaliMetal includes native AIR translation, Vulkan commands and an iOS PanVK build.
Selected real CoreAnimation render/compute shaders pass offline checks. Complete
Metal coverage and assembled GPU rendering remain unfinished. PosterBoard and
native UIKit geometry have explicit, build-gated compatibility tools awaiting
device validation. The latest diagnostic reached CoreAnimation shader loading;
a function-descriptor API gap passed a phone check, exposing missing MRT/blending
support, followed by native-profile schema and buffer-window failures. The
integrated ABI 4/compute ABI 2 candidate now passes strict native linking and
structural planning for both observed pipelines. GPU execution and the expanded
API/helper paths still require device validation. A separate shared-cache
metadata failure affecting SpringBoard and several services is under investigation.

## Priorities

1. Validate the assembled native display/input/MaliMetal candidate, recording
   actual failures and separating loading, rendering and scanout.
2. Resolve shader/API coverage required by the observed system calls; then extend
   graphics features and application compatibility.
3. Restore services as their keybag, storage and hardware dependencies become
   available. Wi-Fi, Bluetooth, NFC, cellular and audio need separate bring-up.

No NFC, payment, biometric, modem or wireless functionality follows merely from
a controller identification or a preserved daemon definition.

Detailed contracts: [wiki home](wiki/Home.md), [display](wiki/Display-Pipeline.md),
[GPU](wiki/MT6789-GPU-Power-Sequence.md), [Metal](wiki/MaliMetal-Driver.md),
[input](wiki/Input-and-HID.md), [services](wiki/System-Services-and-Posters.md).
