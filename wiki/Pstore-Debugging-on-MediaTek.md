# Pstore Debugging on MediaTek

The X676C port mirrors shim and XNU console output into the existing Android ramoops console zone. When DRAM contents survive a reset and Android imports the record, it appears as `/sys/fs/pstore/console-ramoops-0`. This provides a debug channel without an attached serial adapter; it does not guarantee log retention across every reset or power loss.

## Region and format

| Item | Value |
| --- | --- |
| Whole ramoops reservation | Base `0x48090000`, size `0xe0000` |
| Console zone | Base `0x4811f000`, size `0x40000` (256 KiB) |
| Console header | 12 bytes: 32-bit `sig`, `start`, `size` |
| Signature | `0x43474244` (`DBGC`) |
| Maximum console data | `0x40000 - 12 = 262132` bytes |

The whole reservation also contains other ramoops records; it is not all available to the console. The header follows Linux `persistent_ram` layout: `start` is the next write offset and `size` is the recorded byte count. The shim initializes a fresh header and writes its diagnostics; XNU continues behind that output.

## Current retention policy

Since patch 0185, XNU uses a **keep-head** policy: once the recorded size reaches capacity, later appends stop so that early boot messages are retained. This is not a generic 1 MiB rolling console.

The append path reserves offsets with compare/exchange and updates the recorded size separately. It avoids a console lock, allowing early-boot and panic-context output, but reservation, byte writes and size publication are not one ordered transaction. Concurrent writers can interleave or lose bytes, including near the capacity boundary. A missing late message does not prove that execution stopped there.

The implementation is `osfmk/arm64/sweet_early.c`, with console-zone constants supplied by the board configuration; the shim writer is `x676c/shim/plog.c`.

## Reading a previous attempt

After Android returns and root access is available, the console can be read with:

```text
adb shell su -c 'cat /sys/fs/pstore/console-ramoops-0'
```

Save it before another shim boot initializes the region. The deployment helper preserves original console bytes under `x676c/shim/out/xnu-<run>.log`, despite the `.log` suffix. The auto-deploy watcher also preserves the previous record as a `.bin` file. Do not pass binary thumbnails through a text decoder or PowerShell `Set-Content`.

Capture as soon as a root shell is available. Another Android restart can replace
the imported XNU console with Android output. Check the record's contents before
attributing it to an iOS attempt; its filename alone does not identify the OS.

Useful boundaries are the shim's CPU/payload messages, XNU early-boot markers, UFS initialization, HID service opens and the first userspace failures. Console output is evidence of that path executing; a rendered frame in a log is not proof that the physical panel presented it. See [Display Pipeline](Display-Pipeline.md) and [Input and HID](Input-and-HID.md).

## Opt-in read and fault samples

Patch 0197 adds bounded snapshots for investigating file-data mismatches.
`ufsreadtrace=PARTITION_BYTE_OFFSET` samples 64 bytes from a completed read
covering that range; `ufsreadpart=PARTITION_NAME` selects the partition and
defaults to `iosroot`. `UFSREAD` records include the I/O result and buffer's
physical address.

`sweetfaultaddr=UNSLID_USER_ADDRESS` samples the matching 64-byte neighborhood
on SIGBUS/SIGSEGV, accounting for the process's shared-cache slide.
`FAULTMAP` reports the VM region; `FAULTBYTES` records the copy result and
physical page numbers before and after the read. Each path emits at most eight
samples and is disabled without its boot argument. These diagnostics do not
change cache maintenance, memory protections or fault handling.

## Reset and fallback are separate

One-attempt slot A metadata causes fallback on the **next reset**. It does not reset a hang. The current shim has no active lifetime timer. The opt-in XNU `sweetreboot=420` path syncs filesystems and uses TOPRGU software reset after UFS startup; it returned automatically to Android B in run `39945002194`. The earlier PSCI reset attempt in run `39941252453` returned without a reset.

Neither the software timer nor a host ADB timeout is a guarantee against every kernel hang. See [Dual-Boot Safety](Dual-Boot-Safety.md) for the tested behavior and the distinction between boot-only and root-image deployment helpers.

## Source

- [Shim console writer](https://github.com/squeeeezy/HelioDarwin/blob/x676c/x676c/shim/plog.c)
- [Keep-head console patch](https://github.com/squeeeezy/HelioDarwin/blob/x676c/xnu/patches/0185-x676c-pstore-keep-head.patch)
- [Boot/log collection helper](https://github.com/squeeeezy/HelioDarwin/blob/x676c/x676c/tools/boot-xnu.ps1)
