# A/B probe for the fuzzy-alpha-epsilon fix: runs the same navigation plan twice,
# once with --use_fuzzy_alpha_epsilon=false and once true, capturing frames at
# identical times so the two sets can be diffed.
param(
  [string]$Tag = "ab",
  [string]$Plan = "2:focus,4:start,8:start,12:x,16:start,20:start,24:x,28:start,55:start,65:cap,75:w,80:cap,90:cap,100:cap,115:cap",
  [switch]$NoFuzzy
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
public class AB {
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
$fuzzy = if ($NoFuzzy) { "false" } else { "true" }
$args = @("--game_data_root=`"$gameDir`"", "--log_level=info",
          "--log_file=`"$log`"", "--mnk_mode=true",
          "--use_fuzzy_alpha_epsilon=$fuzzy", "--log_flush_interval=1")
$p = Start-Process -FilePath "$exeDir\tenchuz.exe" -ArgumentList $args `
     -WorkingDirectory $exeDir -PassThru
Write-Output "pid=$($p.Id) fuzzy=$fuzzy"
Start-Sleep -Seconds 2
[AB]::TargetPid = [uint32]$p.Id
[AB]::Found.Clear()
[AB]::EnumWindows([AB+EnumWindowsProc]{ param($h,$l) [AB]::Cb($h,$l) }, [IntPtr]::Zero) | Out-Null
if ([AB]::Found.Count -eq 0) { Write-Output "no window"; Stop-Process -Id $p.Id -Force; return }
$hwnd = [AB]::Found[0]
$r = New-Object AB+RECT
[AB]::GetWindowRect($hwnd, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T

function Capture([string]$name) {
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [AB]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $file = "$root\logs\${Tag}_$name.png"
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  Write-Output "[$name] ok=$ok"
}

$keys = @{ "ret"=0x0D; "return"=0x0D; "x"=0x58; "a"=0x41; "b"=0x42; "start"=0x0D;
  "back"=0x08; "space"=0x20; "up"=0x26; "down"=0x28; "left"=0x25; "right"=0x27;
  "esc"=0x1B; "tab"=0x09; "w"=0x57; "s"=0x53; "d"=0x44 }

$t0 = Get-Date
foreach ($item in ($Plan.Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
  $parts = $item.Split(":"); $at = [double]$parts[0]; $act = $parts[1].Trim().ToLower()
  $elapsed = ((Get-Date) - $t0).TotalSeconds
  if ($elapsed -lt $at) { Start-Sleep -Milliseconds ([int](($at - $elapsed)*1000)) }
  if ($p.HasExited) { Write-Output "EXITED"; break }
  if ($act -eq "focus") { [AB]::ForceFocus($hwnd) }
  elseif ($act -eq "cap") { Capture ("t" + [int]((Get-Date) - $t0).TotalSeconds) }
  elseif ($keys.ContainsKey($act)) { [AB]::ForceFocus($hwnd); Start-Sleep -Milliseconds 100; [AB]::Key($keys[$act]) }
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
