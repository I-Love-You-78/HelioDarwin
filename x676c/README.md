# X676C bring-up

Target: Infinix Note 12 2023, MediaTek Helio G99 / MT6789, six Cortex-A55 and
two Cortex-A76 cores, UFS storage and an ili7835 1080×2400 command-mode panel.
The port builds on the repository's earlier SWEET/Topaz work.

## Current boundary

Physical boots execute launchd, SpringBoard and backboardd. The corrected DSI
sequence has displayed the clock/interface, using system software rendering.
The native Vulkan device has initialized, and physical touch/key events have
reached kernel HID. GPU rendering and the native UIKit metadata adapter still
need device validation; SpringBoard and several services currently encounter a
shared-cache metadata failure under investigation.
See [engineering status](../NOTES.md) and the [wiki](../wiki/Home.md).

The latest diagnostic completed its 600-second window and returned to Android.
Native MPS Common registration and display-context readback succeeded, but no
complete rendered UI is established. LaunchServices recovery waited on
MobileGestalt's device-ID inputs: absent Wi-Fi metadata led to probing an
unimplemented network controller. Boot preparation now captures actual factory
Wi-Fi and provisioned Bluetooth data from rooted Android B into a private file,
then emits their canonical six-byte `/chosen` properties. It rejects randomized
interface addresses, reviewed vendor defaults and unrelated firmware layouts.
This supplies hardware metadata, not working wireless drivers or Apple identity
authentication; existing virtual ECID/version policy remains unchanged.

`boot-xnu.ps1` captures that file automatically before building the ADT. An
explicit `-DeviceIdentity PRIVATE_JSON` must contain both addresses and match
the connected device serial; root and slot B are checked for both routes.
`mkadt.py --device-identity PRIVATE_JSON` can also build an ADT offline; without
that option its prior tree is unchanged. Dumps redact the private properties.
Keep identity files and generated ADTs out of Git; the default `shim/out/` is
ignored. Runtime identity lookup and database seeding still require a device run.

## Boot layout

LK loads a boot image v4 containing the shim and kernel payload. The host builder
resolves kernel fixups; the shim checks/copies segments, places ADT and boot_args,
and enters XNU at EL1 with MMU/caches off and x0 pointing to boot_args.

| Port input | Value |
| --- | --- |
| Kernel-managed RAM | `0x140000000`, size `0xffff0000` |
| GICD / GICR | `0x0c000000` / `0x0c040000` |
| Counter / virtual timer | 13 MHz / PPI 11, INTID 27 |
| CPU topology | Six A55 published; both A76 excluded |
| CPU page size | 16 KiB |
| Visible LK buffer | `0xfeabe000`, 1080×2400, 4320-byte stride |
| UFS host | `0x11270000`; named HFS+ root `iosroot` |

Stock LK enters the shim with MMU off, Image at `0x40000000` and the observed DTB
at `0x47c80000`. The boot image requires a gzip Image. `fastboot boot` was rejected
by this LK, so deployment writes `boot_a` from rooted Android B.

The shim does not start the earlier secondary-core display pump or a lifetime
timer. XNU handles DSI refresh. An opt-in `sweetreboot=N` kernel timer provides
the tested TOPRGU reset path after UFS startup; A/B retry state acts only after
a reset. See [recovery](../RECOVERY.md).

## Build and deploy

```powershell
python x676c/tools/mkadt.py ADT_OUTPUT
python x676c/tools/mkpayload.py KERNEL ADT_OUTPUT PAYLOAD_OUTPUT --cmdline "FULL_BOOT_ARGS"
python x676c/shim/build.py PAYLOAD_OUTPUT
pwsh x676c/tools/boot-xnu.ps1 -Run ACTIONS_RUN_ID -Adb ADB_EXECUTABLE -Serial DEVICE_SERIAL
```

The shim builder needs Python with `ziglang`. The ADT builder uses the separately
supplied D22 metadata when available; proprietary firmware/cache inputs do not
belong in this repository. `boot-xnu.ps1` builds the image, verifies writes to
`boot_a` and boot-control metadata, reboots and captures raw pstore after return.
It does not flash a root volume or arm a reset deadline by default.

`auto-deploy.ps1` requires the CI run as an argument; `-RootImage` is an explicit
opt-in root replacement. `apply_tweaks.sh IMAGE [OPTIONS...]` prepares an offline
HFS+ image, resolves repository tools relative to itself and cleans up its mount
on failure. Supply `HFSPLUS_MODULE` only if the host needs an external module.

## Display and input

Native mode uses the complete 1080×2400 panel at scale 3, with matching touch
coordinates and no crop offsets. The optional context adapter supplies actual
cutout/corner geometry to UIKit; compilation does not establish the visible
result. [Display details](../wiki/Display-Pipeline.md) and
[input contracts](../wiki/Input-and-HID.md) describe remaining checks.

## Console

The ramoops reservation is `0x48090000`, size `0xe0000`; the console zone is
`0x4811f000`, size `0x40000`, including a 12-byte header. XNU keeps early output
when the zone fills. Retention depends on warm-reset behavior and Android import;
capture the original bytes before the next shim boot.
