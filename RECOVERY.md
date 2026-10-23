# X676C recovery and deployment

The working setup runs rooted Android on slot B and uses `boot_a` for a single
HelioDarwin attempt. `iosroot` is a separate HFS+ volume on the same UFS device.
Slot selection does not isolate shared storage or undo partition writes.

## Before a write

Preserve the current boot image, 32-byte boot-control record and root volume
outside Git. Confirm the selected phone, Android slot B and a successful,
bootable fallback slot. Use explicit artifact/image inputs and verify readback.
Do not overwrite a root image that is mounted or currently backing the running OS.

## One attempt and reset

The boot helper writes `boot_a`, selects A with one try and verifies the image
and boot-control bytes. LK consumes the attempt. A later reset permits fallback
to bootable slot B. **Retry metadata does not reset a hang.**

The opt-in kernel `sweetreboot=N` timer starts after UFS bring-up, waits N seconds,
syncs filesystems and requests TOPRGU software reset. `sweetreboot=420` returned
the phone to Android in run `39945002194`. A longer diagnostic can explicitly
select `sweetreboot=600`. `-BootArgs` replaces the full command line.

This timer depends on kernel scheduling and sync completion. Earlier failures or
a kernel-wide hang can still need a manual reset. There is no unconditional
recovery guarantee. The shim does not run a separate timed display pump.

## Manual return

The hardware Power + Volume Down reset has been used to return this device.
The required hold time can vary; release after the reset indication. If LK's
boot menu or fastboot is available, select Android B with:

```sh
fastboot set_active b
fastboot reboot
```

Power + Volume Up at power-on opens the observed LK boot-mode menu. Windows USB
driver binding can affect fastboot access. Device-specific BROM/authentication
recovery has not been established by this port and is not a guaranteed fallback.

## Explicit deployment helpers

```powershell
pwsh x676c/tools/boot-xnu.ps1 -Run ACTIONS_RUN_ID -Adb ADB_EXECUTABLE -Serial DEVICE_SERIAL
pwsh x676c/tools/auto-deploy.ps1 -Run ACTIONS_RUN_ID -Adb ADB_EXECUTABLE -Serial DEVICE_SERIAL
```

Both require an explicit run. The watcher waits for rooted Android and preserves
the previous raw pstore console. It replaces `iosroot` only when `-RootImage IMAGE`
is supplied. `flash_rootfs.py IMAGE --adb ADB_EXECUTABLE --serial DEVICE_SERIAL`
checks the HFS+ size, selected device/slot, unmounted destination and capacity,
then verifies a SHA-256 readback of the complete written image. These checks do
not replace backups or protect against power/cable loss during a write.

The boot helper leaves `iosroot` unchanged. Its default command line has no reset
deadline. It waits up to 20 minutes for return, then reports failure if Android
does not complete boot; host waiting itself cannot reset the phone.

## Logs

Capture `/sys/fs/pstore/console-ramoops-0` as raw bytes after Android returns and
before another shim boot. The record is bounded and may omit late output.
The boot helper preserves raw bytes under its ignored output directory.
See [Dual-Boot Safety](wiki/Dual-Boot-Safety.md) and
[Pstore Debugging](wiki/Pstore-Debugging-on-MediaTek.md).
