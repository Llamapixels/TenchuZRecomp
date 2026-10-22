# Bring a process's main window to the foreground, wait, then capture its rect.
# Usage: powershell -WindowStyle Hidden -File focus_shot.ps1 <proc> <out.png>
param([string]$Proc = "tenchuz", [string]$Out = "focus.png")
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Fg {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$p = Get-Process -Name $Proc -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Output "NO PROCESS"; exit 1 }
$h = $p.MainWindowHandle
[Fg]::ShowWindow($h, 9) | Out-Null      # SW_RESTORE
[Fg]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 1500
$r = New-Object Fg+RECT
[Fg]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $h2 = $r.Bottom - $r.Top
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap $w, $h2
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h2))
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "focused+faved $Out (${w}x${h2})"
