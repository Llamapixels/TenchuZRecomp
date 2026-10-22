# Tenchu Z - System Link test with two copies of the game on one PC.
#
#   Copy A  127.0.0.1   the normal build folder
#   Copy B  127.0.0.2   out\build\second-copy, with its own settings and a
#                       copy of your saves
#
# Either copy can host. Copy the host's join code from F6 > Online into the
# other copy's "Join a friend" box and press Join, then Search in System
# Link. (A copy on a loopback address cannot broadcast, so search needs the
# code.) Add -PresetJoin to have each copy search the other without codes,
# -NameA / -NameB to give the copies player names for this run, and
# -RenderScale N to run both at that render scale (default 1: two copies at
# a high scale fight over the GPU, and the copy you are not playing in drops
# to a few frames a second, so enemies stutter for the other one; 0 = each
# copy's saved setting).
# Both copies log every network call (net_trace). Run from anywhere:
#   powershell -ExecutionPolicy Bypass -File tools\systemlink_test.ps1
# Add -Refresh to recopy the game files and saves into the second copy.

param([switch]$Refresh, [switch]$PresetJoin, [string]$NameA = "", [string]$NameB = "",
      [int]$RenderScale = 1)

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "tenchuz-recomp\out\build\win-amd64-release"
$second = Join-Path $root "tenchuz-recomp\out\build\second-copy"
$gameData = Join-Path $root "extracted"
$userData = Join-Path $env:USERPROFILE "Documents\tenchuz"
$secondUser = Join-Path $second "userdata"

if ($Refresh -or -not (Test-Path (Join-Path $second "tenchuz.exe"))) {
  New-Item -ItemType Directory -Force $second | Out-Null
  foreach ($f in "tenchuz.exe", "rexgpu-xenos.dll", "rexruntime.dll", "tenchuz.toml",
                 "shader_settings.ini") {
    $src = Join-Path $build $f
    if (Test-Path $src) { Copy-Item $src $second -Force }
  }
  # Not the join address: copy A's would point copy B at itself.
  $toml = Join-Path $second "tenchuz.toml"
  if (Test-Path $toml) {
    (Get-Content $toml) | Where-Object { $_ -notmatch '^net_join_ip\s*=' } | Set-Content $toml
  }
  foreach ($d in "shader_overrides", "mods") {
    $src = Join-Path $build $d
    if (Test-Path $src) { Copy-Item $src $second -Recurse -Force }
  }
  if (Test-Path $userData) {
    New-Item -ItemType Directory -Force $secondUser | Out-Null
    Copy-Item (Join-Path $userData "*") $secondUser -Recurse -Force
  }
}

$argsA = @("--net_trace=true", "--net_bind_ip=127.0.0.1")
$argsB = @("--net_trace=true", "--net_bind_ip=127.0.0.2",
           "--game_data_root=`"$gameData`"", "--user_data_root=`"$secondUser`"")
if ($NameA) { $argsA += "--user_name=$NameA" }
if ($NameB) { $argsB += "--user_name=$NameB" }
if ($RenderScale -gt 0) {
  $argsA += "--resolution_scale=$RenderScale"
  $argsB += "--resolution_scale=$RenderScale"
}
if ($PresetJoin) {
  $argsA += "--net_join_ip=127.0.0.2"
  $argsB += "--net_join_ip=127.0.0.1"
}
Start-Process -FilePath (Join-Path $build "tenchuz.exe") -WorkingDirectory $build -ArgumentList $argsA
Start-Sleep -Seconds 3
Start-Process -FilePath (Join-Path $second "tenchuz.exe") -WorkingDirectory $second -ArgumentList $argsB
"Started copy A (127.0.0.1) and copy B (127.0.0.2); either can host."
