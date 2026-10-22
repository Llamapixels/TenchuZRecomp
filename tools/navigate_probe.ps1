# Long navigation with periodic capture + OCR to locate the in-game state.
# Usage: navigate_probe.ps1 -Tag nav1 -Every 5 -Duration 160 -Script "30:start,40:x,..."
param(
  [string]$Tag = "nav",
  [int]$Every = 5,
  [int]$Duration = 160,
  [string]$Script = ""
)
$ErrorActionPreference = "Continue"
$root = "C:\Emulation\360\recomps\tenchu z"
$exeDir = "$root\tenchuz-recomp\out\build\win-amd64-release"
$gameDir = "$root\extracted"
$log = "$root\logs\runtime_$Tag.log"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class NV {
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
    uint me = GetCurrentThreadId();
    AttachThreadInput(me, fgThread, true);
    BringWindowToTop(hwnd); SetForegroundWindow(hwnd); SetFocus(hwnd);
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
Start-Sleep -Seconds 2
[NV]::TargetPid = [uint32]$p.Id
[NV]::Found.Clear()
[NV]::EnumWindows([NV+EnumWindowsProc]{ param($h,$l) [NV]::Cb($h,$l) }, [IntPtr]::Zero) | Out-Null
if ([NV]::Found.Count -eq 0) { Write-Output "no window"; Stop-Process -Id $p.Id -Force; return }
$hwnd = [NV]::Found[0]
$r = New-Object NV+RECT
[NV]::GetWindowRect($hwnd, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T

function Capture([string]$name) {
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc(); $null = [NV]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $file = "$root\logs\${Tag}_$name.png"
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  $txt = & powershell -ExecutionPolicy Bypass -File "$root\tools\ocr.ps1" $file 2>$null
  $txt = (($txt | Where-Object { $_ -notmatch "=== OCR|=== END" }) -join " | ").Trim()
  Write-Output ("[$name] OCR: $txt")
}

$keys = @{ "ret"=0x0D; "return"=0x0D; "x"=0x58; "a"=0x41; "b"=0x42; "start"=0x0D;
  "back"=0x08; "space"=0x20; "up"=0x26; "down"=0x28; "left"=0x25; "right"=0x27;
  "esc"=0x1B; "tab"=0x09; "w"=0x57; "s"=0x53; "d"=0x44 }

# Build time->action map from Script.
$acts = @{}
foreach ($it in ($Script.Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
  $pp = $it.Split(":"); $acts[[int]$pp[0]] = $pp[1].Trim().ToLower()
}

$t0 = Get-Date
$nextCap = $Every
for ($i = 0; $i -lt $Duration; $i++) {
  $elapsed = ((Get-Date) - $t0).TotalSeconds
  if ($p.HasExited) { Write-Output "EXITED"; break }
  if ($acts.ContainsKey($i)) {
    $act = $acts[$i]
    if ($keys.ContainsKey($act)) { [NV]::ForceFocus($hwnd); Start-Sleep -Milliseconds 80; [NV]::Key($keys[$act]); Write-Output "t=$i key $act" }
    elseif ($act -eq "focus") { [NV]::ForceFocus($hwnd); Write-Output "t=$i focus" }
  }
  if ($i -ge $nextCap) { [NV]::ForceFocus($hwnd); Capture ("t$i"); $nextCap += $Every }
  Start-Sleep -Milliseconds 1000
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
