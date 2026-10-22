# Launch tenchuz, wait for the title screen, capture and report.
# Prints a signature so two runs (before/after a file edit) can be compared.
# Usage: powershell -File title_probe.ps1 [-Tag name]
param([string]$Tag = "run")

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Cap3 {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$exeDir = "C:\Emulation\360\recomps\tenchu z\tenchuz-recomp\out\build\win-amd64-release"
$outDir = "C:\Emulation\360\recomps\tenchu z\logs\titleprobe_$Tag"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Get-Process tenchuz -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

$log = "C:\Emulation\360\recomps\tenchu z\logs\titleprobe_$Tag.log"
$a = @("--log_level=info", "--log_file=`"$log`"", "--log_flush_interval=1", "--mnk_mode=true")
$p = Start-Process -FilePath "$exeDir\tenchuz.exe" -ArgumentList $a -WorkingDirectory $exeDir -PassThru
Write-Host "launched pid=$($p.Id), waiting for title..."

# Wait for a window, then settle.
$wnd = $null
for ($i = 0; $i -lt 120; $i++) {
    Start-Sleep -Seconds 1
    $wnd = Get-Process -Id $p.Id -ErrorAction SilentlyContinue |
           Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($wnd) { break }
}
if (-not $wnd) { Write-Host "NO WINDOW"; exit 1 }
Write-Host "window up after ${i}s"

# Extra settle time so the title screen is fully drawn.
Start-Sleep -Seconds 45

$h = $wnd.MainWindowHandle
$r = New-Object Cap3+RECT
[Cap3]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top

# Average the centre region, which on the title screen holds the logo/menu.
for ($shot = 0; $shot -lt 3; $shot++) {
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $g.Dispose()

    $counts = @{}
    $rs = 0; $gs = 0; $bs = 0; $n = 0
    for ($y = 0; $y -lt $hh; $y += 16) {
        for ($x = 0; $x -lt $w; $x += 16) {
            $c = $bmp.GetPixel($x, $y); $n++
            $rs += $c.R; $gs += $c.G; $bs += $c.B
            $key = "{0},{1},{2}" -f $c.R, $c.G, $c.B
            if ($counts.ContainsKey($key)) { $counts[$key]++ } else { $counts[$key] = 1 }
        }
    }
    $ar = [Math]::Round($rs / $n, 1); $ag = [Math]::Round($gs / $n, 1); $ab = [Math]::Round($bs / $n, 1)
    $top = ($counts.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 4 |
            ForEach-Object { "$($_.Key):$($_.Value)" }) -join "  "
    Write-Host ("[shot {0}] avg=({1},{2},{3}) distinct={4}  top: {5}" -f $shot, $ar, $ag, $ab, $counts.Count, $top)

    $bmp.Save("$outDir\shot$shot.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Start-Sleep -Seconds 4
}
Write-Host "saved to $outDir"
