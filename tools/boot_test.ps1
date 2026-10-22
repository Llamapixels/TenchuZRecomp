# Boot test for the recompiled Tenchu Z.
#
# Runs tenchuz.exe for a fixed number of seconds, captures a desktop
# screenshot, OCRs it, then reports the process exit code and any FATAL lines
# from the runtime log.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File boot_test.ps1 [-Seconds 25] [-Tag boot2]
param(
  [int]$Seconds = 25,
  [string]$Tag = "boot"
)

$ErrorActionPreference = "Continue"
$root = "$(Split-Path -Parent $PSScriptRoot)"
$exeDir = "$root\tenchuz-recomp\out\build\win-amd64-release"
$gameDir = "$root\extracted"
$log = "$root\logs\runtime_$Tag.log"
$shot = "$root\logs\$Tag.png"

Remove-Item $log -Force -ErrorAction SilentlyContinue

$env:TENCHUZ_DUMP_IMAGE = ""   # make sure the dump hook stays off
$args = @("--game_data_root=`"$gameDir`"", "--log_level=debug",
          "--log_file=`"$log`"")
$p = Start-Process -FilePath "$exeDir\tenchuz.exe" -ArgumentList $args `
     -WorkingDirectory $exeDir -PassThru
Write-Output "started PID $($p.Id)"

Start-Sleep -Seconds $Seconds

$exited = $p.HasExited
if (-not $exited) {
  Write-Output "still running after ${Seconds}s (healthy)"
} else {
  Write-Output "EXITED early, code=$($p.ExitCode) (0xC0000409 = fatal abort)"
}

# Screenshot + OCR while the window is still up.
& powershell -ExecutionPolicy Bypass -File "$root\tools\shot.ps1" $shot | Out-Null
if (-not $exited) { Stop-Process -Id $p.Id -Force; Start-Sleep -Milliseconds 500 }

Write-Output "--- OCR ($shot) ---"
& powershell -ExecutionPolicy Bypass -File "$root\tools\ocr.ps1" $shot

Write-Output "--- log: FATAL / errors ---"
if (Test-Path $log) {
  Select-String -Path $log -Pattern "FATAL|\[error\]|\[critical\]" |
    ForEach-Object { $_.Line } | Select-Object -First 15
  Write-Output "--- log: GPU / plugin ---"
  Select-String -Path $log -Pattern "gpu|xenos|D3D12|pipeline" |
    ForEach-Object { $_.Line } | Select-Object -First 10
} else {
  Write-Output "log not created"
}
