# Find and capture the recompiled game's own window (not the whole desktop).
param(
  [int]$Seconds = 8,
  [string]$Tag = "win"
)

$ErrorActionPreference = "Continue"
$root = "C:\Emulation\360\recomps\tenchu z"
$exeDir = "$root\tenchuz-recomp\out\build\win-amd64-release"
$gameDir = "$root\extracted"
$log = "$root\logs\runtime_$Tag.log"
$shot = "$root\logs\$Tag.png"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static System.Collections.Generic.List<string> All = new System.Collections.Generic.List<string>();
  public static System.Collections.Generic.List<IntPtr> Handles = new System.Collections.Generic.List<IntPtr>();
  public static bool Cb(IntPtr h, IntPtr l) {
    uint pid; GetWindowThreadProcessId(h, out pid);
    var t = new StringBuilder(512); GetWindowTextW(h, t, 512);
    var c = new StringBuilder(256); GetClassNameW(h, c, 256);
    RECT r; GetWindowRect(h, out r);
    All.Add(String.Format("hwnd=0x{0:X} pid={1} vis={2} icon={3} rect={4},{5},{6},{7} class='{8}' title='{9}'",
      h.ToInt64(), pid, IsWindowVisible(h), IsIconic(h), r.L, r.T, r.R, r.B, c, t));
    Handles.Add(h);
    return true;
  }
}
"@

Remove-Item $log -Force -ErrorAction SilentlyContinue
$args = @("--game_data_root=`"$gameDir`"", "--log_level=debug", "--log_file=`"$log`"")
$p = Start-Process -FilePath "$exeDir\tenchuz.exe" -ArgumentList $args `
     -WorkingDirectory $exeDir -PassThru
Write-Output "pid=$($p.Id)"
Start-Sleep -Seconds $Seconds

[W]::All.Clear(); [W]::Handles.Clear()
[W]::EnumWindows([W+EnumWindowsProc]{ param($h,$l) [W]::Cb($h,$l) }, [IntPtr]::Zero) | Out-Null
Write-Output "--- windows for pid $($p.Id) ---"
$idx = @()
for ($i = 0; $i -lt [W]::All.Count; $i++) {
  if ([W]::All[$i] -match "pid=$($p.Id) ") { Write-Output ("  " + [W]::All[$i]); $idx += $i }
}
Write-Output "--- foreground ---"
$fg = [W]::GetForegroundWindow()
for ($i = 0; $i -lt [W]::Handles.Count; $i++) {
  if ([W]::Handles[$i] -eq $fg) { Write-Output ("  " + [W]::All[$i]) }
}

# Capture the largest visible game window with PrintWindow.
$best = [IntPtr]::Zero; $bestArea = 0
foreach ($i in $idx) {
  $m = [regex]::Match([W]::All[$i], "vis=True.*rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)")
  if ($m.Success) {
    $a = ([int]$m.Groups[3].Value - [int]$m.Groups[1].Value) * ([int]$m.Groups[4].Value - [int]$m.Groups[2].Value)
    if ($a -gt $bestArea) { $bestArea = $a; $best = [W]::Handles[$i] }
  }
}
Write-Output "capture hwnd=0x$('{0:X}' -f $best.ToInt64()) area=$bestArea"

if ($best -ne [IntPtr]::Zero) {
  [W]::ShowWindow($best, 5) | Out-Null   # SW_SHOW
  [W]::SetForegroundWindow($best) | Out-Null
  Start-Sleep -Milliseconds 700
  $r = New-Object W+RECT
  [W]::GetWindowRect($best, [ref]$r) | Out-Null
  $w = [Math]::Max(1, $r.R - $r.L); $h = [Math]::Max(1, $r.B - $r.T)
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [W]::PrintWindow($best, $hdc, 2)   # PW_RENDERFULLCONTENT
  $g.ReleaseHdc($hdc); $g.Dispose()
  $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
  Write-Output "PrintWindow=$ok size=${w}x${h} -> $shot"
  $bmp.Dispose()

  # Also grab the desktop crop of the window region as a fallback.
  $shot2 = "$root\logs\${Tag}_screen.png"
  $b2 = New-Object System.Drawing.Bitmap($w, $h)
  $g2 = [System.Drawing.Graphics]::FromImage($b2)
  $g2.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $h)))
  $g2.Dispose()
  $b2.Save($shot2, [System.Drawing.Imaging.ImageFormat]::Png)
  $b2.Dispose()
  Write-Output "screen crop -> $shot2"
}

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
