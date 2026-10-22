# Sample tenchuz.exe CPU/thread state over time to distinguish a live process
# from a deadlock. Launches the exe (no debugger), polls every second.
param(
  [int]$Seconds = 20,
  [string]$Tag = "sample"
)

$ErrorActionPreference = "Continue"
$root = "C:\Emulation\360\recomps\tenchu z"
$exeDir = "$root\tenchuz-recomp\out\build\win-amd64-release"
$gameDir = "$root\extracted"
$log = "$root\logs\runtime_$Tag.log"

Remove-Item $log -Force -ErrorAction SilentlyContinue

$args = @("--game_data_root=`"$gameDir`"", "--log_level=debug",
          "--log_file=`"$log`"")
$p = Start-Process -FilePath "$exeDir\tenchuz.exe" -ArgumentList $args `
     -WorkingDirectory $exeDir -PassThru
Write-Output "pid=$($p.Id)"

function Snap {
  param($proc)
  $proc.Refresh()
  $cpu = $proc.TotalProcessorTime.TotalSeconds
  $threads = @($proc.Threads)
  $running = 0; $waiting = 0; $other = 0
  $states = @{}
  foreach ($t in $threads) {
    try { $st = [string]$t.ThreadState } catch { $st = "?" }
    if ($st -match "Running") { $running++ }
    elseif ($st -match "Wait") { $waiting++ }
    else { $other++ }
    $states[$st] = 1 + ($states[$st] -as [int])
  }
  $logLen = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }
  return [pscustomobject]@{
    Cpu = [math]::Round($cpu, 3); Threads = $threads.Count
    Running = $running; Waiting = $waiting; Other = $other
    LogBytes = $logLen; States = $states
  }
}

$base = Snap $p
$prevCpu = $base.Cpu
Write-Output ("t=0  cpu={0:F3} thr={1} run={2} wait={3} log={4}" -f $base.Cpu, $base.Threads, $base.Running, $base.Waiting, $base.LogBytes)

for ($i = 1; $i -le $Seconds; $i++) {
  Start-Sleep -Seconds 1
  if ($p.HasExited) {
    Write-Output ("t={0}  EXITED code=0x{1:X8}" -f $i, $p.ExitCode)
    break
  }
  $s = Snap $p
  $d = [math]::Round($s.Cpu - $prevCpu, 3)
  $prevCpu = $s.Cpu
  Write-Output ("t={0,-3} cpu={1:F3} (+{2:F3}) thr={3} run={4} wait={5} other={6} log={7}" -f `
    $i, $s.Cpu, $d, $s.Threads, $s.Running, $s.Waiting, $s.Other, $s.LogBytes)
}

if (-not $p.HasExited) {
  Write-Output "--- per-thread detail (last snapshot) ---"
  $p.Refresh()
  $rows = @()
  foreach ($t in $p.Threads) {
    $st = "?"; $wr = ""
    try { $st = [string]$t.ThreadState } catch {}
    try { $wr = [string]$t.WaitReason } catch {}
    $rows += [pscustomobject]@{ Tid = $t.Id; State = $st; WaitReason = $wr;
                                Cpu = [math]::Round($t.TotalProcessorTime.TotalSeconds, 3) }
  }
  $rows | Sort-Object Cpu -Descending | Format-Table -AutoSize
  Stop-Process -Id $p.Id -Force
  Start-Sleep -Milliseconds 400
}

Write-Output "--- log tail ---"
if (Test-Path $log) { Get-Content $log | Select-Object -Last 15 }
