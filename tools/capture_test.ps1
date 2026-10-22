# Capture the tenchuz window repeatedly and report dominant colour.
# Usage: powershell -File capture_test.ps1
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Cap {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$outDir = "C:\Emulation\360\recomps\tenchu z\logs\modtest"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

for ($n = 0; $n -lt 8; $n++) {
    $proc = Get-Process tenchuz -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $proc) { Write-Host "no window"; Start-Sleep -Seconds 3; continue }

    $h = $proc.MainWindowHandle
    $r = New-Object Cap+RECT
    [Cap]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left
    $hh = $r.Bottom - $r.Top
    if ($w -le 0 -or $hh -le 0) { Write-Host "bad rect"; Start-Sleep -Seconds 3; continue }

    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $g.Dispose()

    # Sample a grid and tally colours.
    $counts = @{}
    $magenta = 0
    $total = 0
    for ($y = 0; $y -lt $hh; $y += 16) {
        for ($x = 0; $x -lt $w; $x += 16) {
            $c = $bmp.GetPixel($x, $y)
            $total++
            $key = "{0},{1},{2}" -f $c.R, $c.G, $c.B
            if ($counts.ContainsKey($key)) { $counts[$key]++ } else { $counts[$key] = 1 }
            # magenta = high R, low G, high B
            if ($c.R -gt 200 -and $c.G -lt 60 -and $c.B -gt 200) { $magenta++ }
        }
    }
    $top = $counts.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 3
    $topStr = ($top | ForEach-Object { "$($_.Key):$($_.Value)" }) -join "  "
    $pct = if ($total -gt 0) { [Math]::Round(100.0 * $magenta / $total, 1) } else { 0 }

    Write-Host ("[{0}] {1}x{2} distinct={3} magenta={4}%  top: {5}" -f $n, $w, $hh, $counts.Count, $pct, $topStr)

    $bmp.Save("$outDir\frame$n.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Start-Sleep -Seconds 3
}
Write-Host "done; frames in $outDir"
