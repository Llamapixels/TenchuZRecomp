# State-driven navigation probe.
#
# Instead of pressing keys on a fixed clock (the game's boot time varies run to
# run), this probe polls the window with OCR and only sends a key once the
# expected on-screen prompt is visible.  That makes reaching gameplay
# reproducible, which the fixed-time play_probe could not do.
#
# Usage: nav_state.ps1 -Tag cs1 -Goal character
#   Goal 'gamestart'  : advance title -> in-game (until no prompt text remains)
#   Goal 'character'  : same, then keep going to the character/menu screens
param(
  [string]$Tag = "navstate",
  [string]$Goal = "gamestart",
  [int]$Timeout = 300
)
$ErrorActionPreference = "Continue"
$root = "$(Split-Path -Parent $PSScriptRoot)"
$exeDir = "$root\tenchuz-recomp\out\build\win-amd64-release"
$gameDir = "$root\extracted"
$log = "$root\logs\runtime_$Tag.log"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class NS {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  public static System.Collections.Generic.List<IntPtr> Found = new System.Collections.Generic.List<IntPtr>();
  public static uint TargetPid = 0;
  public static bool Cb(IntPtr h, IntPtr l) {
    uint pid; GetWindowThreadProcessId(h, out pid);
    if (pid == TargetPid && IsWindowVisible(h)) Found.Add(h);
    return true;
  }
  public static void ForceFocus(IntPtr hwnd) {
    ShowWindow(hwnd, 9);
    IntPtr fg = GetForegroundWindow();
    uint fgPid; uint fgThread = GetWindowThreadProcessId(fg, out fgPid);
    uint gameThread = GetWindowThreadProcessId(hwnd, out fgPid);
    uint me = GetCurrentThreadId();
    AttachThreadInput(me, fgThread, true);
    AttachThreadInput(me, gameThread, true);
    BringWindowToTop(hwnd); SetForegroundWindow(hwnd); SetFocus(hwnd);
    AttachThreadInput(me, gameThread, false);
    AttachThreadInput(me, fgThread, false);
  }
  public static void Key(int vk) {
    INPUT[] a = new INPUT[2];
    a[0].type = 1; a[0].ki.wVk = (ushort)vk;
    a[1].type = 1; a[1].ki.wVk = (ushort)vk; a[1].ki.dwFlags = 2;
    SendInput(2, a, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@

Remove-Item $log -Force -ErrorAction SilentlyContinue
$args = @("--game_data_root=`"$gameDir`"", "--log_level=info",
          "--log_file=`"$log`"", "--mnk_mode=true", "--log_flush_interval=1")
$p = Start-Process -FilePath "$exeDir\tenchuz.exe" -ArgumentList $args -WorkingDirectory $exeDir -PassThru
Write-Output "pid=$($p.Id)"
Start-Sleep -Seconds 3
[NS]::TargetPid = [uint32]$p.Id
[NS]::Found.Clear()
[NS]::EnumWindows([NS+EnumWindowsProc]{ param($h,$l) [NS]::Cb($h,$l) }, [IntPtr]::Zero) | Out-Null
if ([NS]::Found.Count -eq 0) { Write-Output "no window"; Stop-Process -Id $p.Id -Force; return }
$hwnd = [NS]::Found[0]
$r = New-Object NS+RECT
[NS]::GetWindowRect($hwnd, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T

$script:lastOcr = ""
function Snapshot([string]$name) {
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc(); $null = [NS]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $file = "$root\logs\${Tag}_$name.png"
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  $txt = & powershell -ExecutionPolicy Bypass -File "$root\tools\ocr.ps1" $file 2>$null
  $script:lastOcr = (($txt | Where-Object { $_ -notmatch "=== OCR|=== END" }) -join " ").Trim()
  return $file
}
function Press([int]$vk) { [NS]::ForceFocus($hwnd); Start-Sleep -Milliseconds 120; [NS]::Key($vk) }

$VK = @{ ret=0x0D; space=0x20; esc=0x1B; semicolon=0xBA; w=0x57; a=0x41; s=0x53; d=0x44;
         left=0x25; right=0x27; up=0x26; down=0x28; x=0x58 }

$t0 = Get-Date
$shot = 0
Write-Output "waiting for a screen with text (title)..."
while (((Get-Date) - $t0).TotalSeconds -lt $Timeout) {
  if ($p.HasExited) { Write-Output "EXITED"; break }
  $null = Snapshot ("s{0:D3}" -f $shot); $shot++
  $el = [int]((Get-Date) - $t0).TotalSeconds
  Write-Output ("t=$el OCR: " + $script:lastOcr)
  if ($script:lastOcr -match "PRESS\s*[Ss]TART|FromSoft") {
    Write-Output "title detected -> pressing Start"
    Press $VK.ret
    Start-Sleep -Seconds 2
    break
  }
  Start-Sleep -Seconds 2
}

# Advance through whatever menus follow, pressing A/Space or Start, logging OCR,
# and stop when the screen has no Latin UI text for a few consecutive samples
# (indicating a 3D gameplay scene).
$blank = 0
for ($i = 0; $i -lt 90; $i++) {
  if ($p.HasExited) { Write-Output "EXITED"; break }
  $null = Snapshot ("m{0:D3}" -f $i)
  $el = [int]((Get-Date) - $t0).TotalSeconds
  Write-Output ("t=$el OCR: " + $script:lastOcr)
  if ($script:lastOcr -match "Load game") { Press $VK.ret; Start-Sleep -Milliseconds 800; continue }
  if ($script:lastOcr -match "Yes")        { Press $VK.ret; Start-Sleep -Milliseconds 800; continue }
  if ($script:lastOcr -match "PRESS\s*[Ss]TART|FromSoft") { Press $VK.ret; Start-Sleep -Milliseconds 800; continue }
  if ($script:lastOcr -match "Press any|START|Select|Continue|New game|Mission") { Press $VK.ret; Start-Sleep -Milliseconds 800; continue }
  if ([string]::IsNullOrWhiteSpace($script:lastOcr)) {
    $blank++
    if ($blank -ge 3) { Write-Output "no UI text x3 -> likely in-game scene"; break }
  } else { $blank = 0 }
  Start-Sleep -Seconds 2
}
Write-Output "final OCR: $script:lastOcr"
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
