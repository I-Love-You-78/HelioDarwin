<#
Wait for a selected Android device, optionally flash an explicit root image,
then deploy an explicit CI run. Each successful invocation performs one attempt.
Example: auto-deploy.ps1 -Run RUN_ID -Adb PATH_TO_ADB -Serial DEVICE_SERIAL
Add -RootImage IMAGE only when replacing iosroot is intended.
#>
param(
    [Parameter(Mandatory)] [ValidatePattern('^[0-9]+$')] [string]$Run,
    [string]$RootImage = "",
    [string]$Adb = "adb",
    [string]$Serial = "",
    [ValidatePattern('^(?:/[A-Za-z0-9_.-]+)+$|^[A-Za-z0-9_.-]+$')] [string]$Su = "su",
    [string]$BootArgs = "",
    [ValidateRange(1, 3600)] [int]$WaitSeconds = 600
)
$ErrorActionPreference = 'Stop'
$adb = (Get-Command $Adb -CommandType Application -ErrorAction Stop).Source
$adbArguments = @()
if ($Serial) { $adbArguments = @('-s', $Serial) }
if ($RootImage) {
    $RootImage = (Resolve-Path -LiteralPath $RootImage -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath $RootImage -PathType Leaf)) { throw 'RootImage must be a file' }
}
$out = Join-Path $PSScriptRoot '..\shim\out'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$deadline = (Get-Date).AddSeconds($WaitSeconds)
$ready = $false
Write-Host 'Waiting for Android and its root shell...'
while ((Get-Date) -lt $deadline) {
    $uid = & $adb @adbArguments shell $Su -c 'id -u' 2>$null
    if ($LASTEXITCODE -eq 0 -and "$uid".Trim() -eq '0') {
        $ready = $true
        break
    }
    Start-Sleep -Seconds 2
}
if (-not $ready) { throw 'Timed out waiting for one selected rooted Android device' }
if (-not $Serial) {
    $Serial = (& $adb get-serialno).Trim()
    if ($LASTEXITCODE -or -not $Serial -or $Serial -eq 'unknown') { throw 'Select one device with -Serial' }
    $adbArguments = @('-s', $Serial)
}
$previousLog = Join-Path $out "before-$Run-console.bin"
if (Test-Path -LiteralPath $previousLog) { throw "Previous log already exists: $previousLog" }
python -c 'import pathlib, subprocess, sys; pathlib.Path(sys.argv[1]).write_bytes(subprocess.check_output(sys.argv[2:]))' $previousLog $adb @adbArguments exec-out $Su -c 'cat /sys/fs/pstore/console-ramoops-0'
if ($LASTEXITCODE) { throw 'Could not preserve the previous pstore console' }
if ($RootImage) {
    python (Join-Path $PSScriptRoot 'flash_rootfs.py') $RootImage --adb $adb --serial $Serial --su $Su
    if ($LASTEXITCODE) { throw 'Root image flash or full readback failed' }
}
$boot = @{ Run = $Run; Adb = $adb; Serial = $Serial; Su = $Su }
if ($BootArgs) { $boot.BootArgs = $BootArgs }
& (Join-Path $PSScriptRoot 'boot-xnu.ps1') @boot
