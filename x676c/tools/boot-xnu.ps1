<#
One xnu boot attempt on the X676C, end to end:
  CI kernel -> ADT + payload -> shim boot.img -> boot_a (slot A, 1 try)
  -> reboot -> LK falls back to Android -> pstore log to out/xnu-<run>.log

usage: tools/boot-xnu.ps1 -Run <actions run id> [-BootArgs "..."]
Needs: gh (repo squeeeezy/ios-x676c), adb with root (Magisk), Python + ziglang.
#>
param(
    [Parameter(Mandatory)] [string]$Run,
    [string]$BootArgs = "debug=0xc serial=3 cpus=6 keepsyms=1 -v -enable_kprintf_spam wdt=-1 wdt_test=no_user_checkin rd=ufs ufsroot=iosroot ufsrw=iosroot unentitled_ios_sim_launch=1 ignition_level=1 launchd_unsecure_cache=1 sweetsample=- ufsbeat=64 sweetscan=backboardd,SpringBoard,PosterBoard sweetnull=launchd,dyld,init,backboardd,SpringBoard,lsd,containermanagerd sweetfb_noconsole=1 sweetfb_native=1 sweetfb_stayon=1"
)
$ErrorActionPreference = "Stop"
$repo = "squeeeezy/HelioDarwin"
$x676c = Split-Path -Parent $PSScriptRoot
$out = Join-Path $x676c "shim\out"
$adb = Join-Path $env:LOCALAPPDATA "platform-tools-dl\platform-tools\adb.exe"
New-Item -ItemType Directory -Force $out | Out-Null

function Phone([string]$cmd) { & $adb shell su -c "$cmd" }
function Push-Lf([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, ($text -replace "`r`n", "`n"))
}

# 1. kernel from CI
$k = Join-Path $out "k-$Run"
if (-not (Test-Path "$k\kernel.development.sweet")) {
    gh run watch $Run -R $repo --exit-status --interval 30 *> $null
    if ($LASTEXITCODE) { throw "build $Run failed: gh run view $Run -R $repo --log-failed" }
    gh run download $Run -R $repo -n xnu-kernel -D $k
}

# 2. ADT, payload, boot image
python -I (Join-Path $x676c "tools\mkadt.py") "$out\x676c.adt"
python -I (Join-Path $x676c "tools\mkpayload.py") "$k\kernel.development.sweet" "$out\x676c.adt" "$out\payload.bin" --cmdline $BootArgs
python -I (Join-Path $x676c "shim\build.py") "$out\payload.bin"

# 3. boot_a + slot A with one try (misc boot_control, see tools/bootctl_block.py)
& $adb wait-for-device
Phone "dd if=/dev/block/by-name/misc of=/data/local/tmp/bctl.bin bs=1 skip=2048 count=32 2>/dev/null"
& $adb pull /data/local/tmp/bctl.bin "$out\bctl_now.bin" | Out-Null
$hex = [BitConverter]::ToString([IO.File]::ReadAllBytes("$out\bctl_now.bin")).Replace("-", "")
python -I (Join-Path $x676c "tools\bootctl_block.py") set-active a $hex "$out\bctl_slot_a.bin" 1
if ($LASTEXITCODE) { throw "bad boot_control block" }
& $adb push "$out\shim-boot.img" /data/local/tmp/shim-boot.img | Out-Null
& $adb push "$out\bctl_slot_a.bin" /data/local/tmp/bctl_slot_a.bin | Out-Null
$flash = Join-Path $out "flash_a.sh"
Push-Lf $flash @'
set -e
T=/data/local/tmp
sz=$(stat -c %s $T/shim-boot.img)
want=$(sha1sum $T/shim-boot.img | cut -d' ' -f1)
dd if=$T/shim-boot.img of=/dev/block/by-name/boot_a bs=4096 conv=fsync 2>/dev/null
[ "$(head -c $sz /dev/block/by-name/boot_a | sha1sum | cut -d' ' -f1)" = "$want" ] || { echo "boot_a VERIFY FAILED"; exit 1; }
dd if=$T/bctl_slot_a.bin of=/dev/block/by-name/misc bs=1 seek=2048 count=32 conv=notrunc,fsync 2>/dev/null
[ "$(dd if=/dev/block/by-name/misc bs=1 skip=2048 count=32 2>/dev/null | sha1sum | cut -d' ' -f1)" = "$(sha1sum $T/bctl_slot_a.bin | cut -d' ' -f1)" ] || { echo "misc VERIFY FAILED"; exit 1; }
rm -f $T/bctl.bin $T/flash_a.sh
sync
echo "flashed boot_a ($sz bytes), slot A 1 try"
'@
& $adb push $flash /data/local/tmp/flash_a.sh | Out-Null
$r = Phone "sh /data/local/tmp/flash_a.sh"
$r
if (-not ($r -match "flashed")) { throw "flash failed" }

# 4. boot it, wait for the fallback to Android, collect pstore
& $adb reboot
"$(Get-Date -f HH:mm:ss) rebooted into the shim"
Start-Sleep 20
$deadline = (Get-Date).AddMinutes(12)
while ((Get-Date) -lt $deadline) {
    $devs = try { & $adb devices 2>$null | Out-String } catch { "" }
    if ($devs -match "`tdevice") { break }
    Start-Sleep 3
}
for ($i = 0; $i -lt 60; $i++) {
    $bc = try { & $adb shell getprop sys.boot_completed 2>$null } catch { "" }
    if ($bc -match '1') { break }
    Start-Sleep 2
}
$log = Join-Path $out "xnu-$Run.log"
Phone "cat /sys/fs/pstore/console-ramoops-0" | Set-Content $log
"$(Get-Date -f HH:mm:ss) back in Android (slot $(& $adb shell getprop ro.boot.slot_suffix 2>$null)), log: $log"
Get-Content $log -Tail 60
