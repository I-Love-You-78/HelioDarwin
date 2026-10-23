# Auto-deploy watcher: waits for phone to connect via ADB, flashes updated rootfs and boots XNU
$adb = Join-Path $env:LOCALAPPDATA "platform-tools-dl\platform-tools\adb.exe"
$tools = $PSScriptRoot
Write-Host "[AutoDeploy] Waiting for phone to connect via ADB..." -ForegroundColor Cyan

while ($true) {
    $devs = & $adb devices | Out-String
    if ($devs -match "\tdevice") {
        Write-Host "`n[AutoDeploy] Phone detected! Checking Android boot state..." -ForegroundColor Green
        
        # Wait for root/su availability
        $ready = $false
        for ($i = 0; $i -lt 30; $i++) {
            $su_test = & $adb shell "su -c 'id -u'" 2>$null
            if ($su_test -match "0") {
                $ready = $true
                break
            }
            Start-Sleep 1
        }
        
        if ($ready) {
            Write-Host "[AutoDeploy] Root shell confirmed! Streaming updated rootfs..." -ForegroundColor Green
            python (Join-Path $tools "flash_rootfs.py")
            if ($LASTEXITCODE -ne 0) {
                Write-Host "[AutoDeploy] Rootfs flash failed!" -ForegroundColor Red
                break
            }
            
            Write-Host "[AutoDeploy] Rootfs flashed! Booting XNU (Run 39872424370)..." -ForegroundColor Green
            & (Join-Path $tools "boot-xnu.ps1") -Run 39872424370
            break
        } else {
            Write-Host "[AutoDeploy] Waiting for su to respond..." -ForegroundColor Yellow
        }
    }
    Start-Sleep 2
}
