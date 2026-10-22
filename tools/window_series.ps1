# Launch the game and capture its window repeatedly, reporting luminance stats
# and OCR text at each sample. Optionally send a key before each capture.
# Usage:
#   window_series.ps1 -Tag title1 -Samples 4,8,12,16,20,25 [-Key SPACE]
param(
  [string]$Tag = "series",
  [int[]]$Samples = @(4, 8, 12, 16, 20, 25),
  [string]$Key = ""
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
public class WS {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static System.Collections.Generic.List<IntPtr> Found = new System.Collections.Generic.List<IntPtr>();
  public static uint TargetPid = 0;
  public static bool Cb(IntPtr h, IntPtr l) {
    uint pid; GetWindowThreadProcessId(h, out pid);
    if (pid == TargetPid && IsWindowVisible(h)) Found.Add(h);
    return true;
  }
  public static void SendVk(IntPtr h, int vk) {
    PostMessageW(h, 0x0100, (IntPtr)vk, (IntPtr)0x001E0001);       // WM_KEYDOWN
    System.Threading.Thread.Sleep(60);
    PostMessageW(h, 0x0101, (IntPtr)vk, unchecked((IntPtr)(int)0xC01E0001)); // WM_KEYUP
  }
}
"@

Remove-Item $log -Force -ErrorAction SilentlyContinue
$args = @("--game_data_root=`"$gameDir`"", "--log_level=debug", "--log_file=`"$log`"")
$p = Start-Process -FilePath "$exeDir\tenchuz.exe" -ArgumentList $args `
     -WorkingDirectory $exeDir -PassThru
Write-Output "pid=$($p.Id)"
Start-Sleep -Seconds 2

[WS]::TargetPid = [uint32]$p.Id
[WS]::Found.Clear()
[WS]::EnumWindows([WS+EnumWindowsProc]{ param($h,$l) [WS]::Cb($h,$l) }, [IntPtr]::Zero) | Out-Null
if ([WS]::Found.Count -eq 0) { Write-Output "no visible window found"; Stop-Process -Id $p.Id -Force; return }
$hwnd = [WS]::Found[0]
[WS]::ShowWindow($hwnd, 5) | Out-Null
[WS]::SetForegroundWindow($hwnd) | Out-Null
Write-Output ("window hwnd=0x{0:X}" -f $hwnd.ToInt64())

$vk = 0
if ($Key -eq "SPACE") { $vk = 0x20 }
elseif ($Key -eq "RETURN") { $vk = 0x0D }
elseif ($Key -eq "ESCAPE") { $vk = 0x1B }
if ($vk -ne 0) { Start-Sleep -Milliseconds 300; [WS]::SendVk($hwnd, $vk); Write-Output "sent key $Key" }

$r = New-Object WS+RECT
[WS]::GetWindowRect($hwnd, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
Write-Output "window size ${w}x${h}"

$t0 = Get-Date
foreach ($s in $Samples) {
  $elapsed = ((Get-Date) - $t0).TotalSeconds
  if ($elapsed -lt $s) { Start-Sleep -Milliseconds ([int](($s - $elapsed) * 1000)) }
  if ($p.HasExited) { Write-Output "t=${s}s EXITED code=0x$('{0:X8}' -f $p.ExitCode)"; break }

  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [WS]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()

  $sum=0.0; $sum2=0.0; $n=0; $bright=0; $minx=99999; $miny=99999; $maxx=-1; $maxy=-1
  for ($y=0; $y -lt $h; $y+=8) {
    for ($x=0; $x -lt $w; $x+=8) {
      $c = $bmp.GetPixel($x,$y)
      $l = 0.299*$c.R + 0.587*$c.G + 0.114*$c.B
      $sum += $l; $sum2 += $l*$l; $n++
      if ($l -gt 40) { $bright++; if($x -lt $minx){$minx=$x}; if($x -gt $maxx){$maxx=$x}; if($y -lt $miny){$miny=$y}; if($y -gt $maxy){$maxy=$y} }
    }
  }
  $mean = $sum/$n; $sd = [Math]::Sqrt([Math]::Max(0, $sum2/$n - $mean*$mean))
  $file = "$root\logs\${Tag}_t$s.png"
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  $bbox = if ($maxx -ge 0) { "($minx,$miny)-($maxx,$maxy)" } else { "none" }
  Write-Output ("t={0,2}s PrintWindow={1} meanL={2:N2} sd={3:N2} brightPx={4}/{5} bbox={6} file={7}" -f $s,$ok,$mean,$sd,$bright,$n,$bbox,(Split-Path $file -Leaf))
  $txt = & powershell -ExecutionPolicy Bypass -File "$root\tools\ocr.ps1" $file 2>$null
  $txt = ($txt | Where-Object { $_ -notmatch "=== OCR|=== END OCR" }) -join " | "
  if ($txt.Trim().Length -gt 0) { Write-Output "   OCR: $txt" }
}

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
