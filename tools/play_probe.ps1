# Time-plan play probe. Launches the game, forces the window to the foreground,
# then executes a plan of actions at given seconds and captures the window.
#
# Plan format (comma separated, t:action):
#   focus ret x a b start back up down left right space cap w s
# Example:
#   play_probe.ps1 -Tag p1 -Plan "2:focus,3:x,5:cap,8:cap,11:cap,14:start,16:cap,20:cap"
param(
  [string]$Tag = "play",
  [string]$Plan = "2:focus,3:x,5:cap"
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
using System.Text;
public class PP {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr p);
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
  public static string ForceFocus(IntPtr hwnd) {
    ShowWindow(hwnd, 9);
    IntPtr fg = GetForegroundWindow();
    uint fgPid; uint fgThread = GetWindowThreadProcessId(fg, out fgPid);
    uint me = GetCurrentThreadId();
    bool attached = AttachThreadInput(me, fgThread, true);
    BringWindowToTop(hwnd);
    bool sfw = SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    AttachThreadInput(me, fgThread, false);
    return String.Format("attach={0} setfg={1} fg=0x{2:X}", attached, sfw, GetForegroundWindow().ToInt64());
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
$args = @("--game_data_root=`"$gameDir`"", "--log_level=debug", "--log_file=`"$log`"", "--mnk_mode=true")
$p = Start-Process -FilePath "$exeDir\tenchuz.exe" -ArgumentList $args `
     -WorkingDirectory $exeDir -PassThru
Write-Output "pid=$($p.Id)"
Start-Sleep -Seconds 2
[PP]::TargetPid = [uint32]$p.Id
[PP]::Found.Clear()
[PP]::EnumWindows([PP+EnumWindowsProc]{ param($h,$l) [PP]::Cb($h,$l) }, [IntPtr]::Zero) | Out-Null
if ([PP]::Found.Count -eq 0) { Write-Output "no visible window"; Stop-Process -Id $p.Id -Force; return }
$hwnd = [PP]::Found[0]
$r = New-Object PP+RECT
[PP]::GetWindowRect($hwnd, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
Write-Output (("window 0x{0:X} ${w}x${h}") -f $hwnd.ToInt64())

function Capture([string]$name) {
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [PP]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $file = "$root\logs\${Tag}_$name.png"
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $sum=0.0; $sum2=0.0; $n=0
  for ($y=0; $y -lt $h; $y+=16) {
    for ($x=0; $x -lt $w; $x+=16) {
      $c=$bmp.GetPixel($x,$y); $l=0.299*$c.R+0.587*$c.G+0.114*$c.B
      $sum += $l; $sum2 += $l*$l; $n++
    }
  }
  $bmp.Dispose()
  $mean=$sum/$n; $sd=[Math]::Sqrt([Math]::Max(0,$sum2/$n-$mean*$mean))
  $txt = & powershell -ExecutionPolicy Bypass -File "$root\tools\ocr.ps1" $file 2>$null
  $txt = (($txt | Where-Object { $_ -notmatch "=== OCR|=== END OCR" }) -join " | ").Trim()
  Write-Output ("[{0}] PrintWindow={1} meanL={2:N1} sd={3:N1} OCR: {4}" -f $name,$ok,$mean,$sd,$txt)
}

$keys = @{
  "ret"=0x0D; "return"=0x0D; "x"=0x58; "a"=0x41; "b"=0x42; "start"=0x0D;
  "back"=0x08; "space"=0x20; "up"=0x26; "down"=0x28; "left"=0x25; "right"=0x27;
  "esc"=0x1B; "tab"=0x09; "w"=0x57; "s"=0x53; "d"=0x44
}

$t0 = Get-Date
$items = $Plan.Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ }
foreach ($item in $items) {
  $parts = $item.Split(":")
  $at = [double]$parts[0]
  $act = $parts[1].Trim().ToLower()
  $elapsed = ((Get-Date) - $t0).TotalSeconds
  if ($elapsed -lt $at) { Start-Sleep -Milliseconds ([int](($at - $elapsed)*1000)) }
  if ($p.HasExited) { Write-Output "EXITED code=0x$('{0:X8}' -f $p.ExitCode)"; break }
  $now = [int]((Get-Date) - $t0).TotalSeconds
  if ($act -eq "focus") {
    Write-Output ("t=${now}s focus: " + [PP]::ForceFocus($hwnd))
  } elseif ($act -eq "cap") {
    Capture ("t$now")
  } elseif ($keys.ContainsKey($act)) {
    [PP]::ForceFocus($hwnd) | Out-Null
    Start-Sleep -Milliseconds 100
    [PP]::Key($keys[$act])
    Write-Output "t=${now}s key $act (vk=0x$('{0:X}' -f $keys[$act]))"
  } else {
    Write-Output "t=${now}s unknown action '$act'"
  }
}

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
