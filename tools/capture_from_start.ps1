# Launch tenchuz and capture the window from t=0, hunting for an expected colour.
# Usage: powershell -File capture_from_start.ps1
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Cap2 {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$exeDir = "$(Split-Path -Parent $PSScriptRoot)\tenchuz-recomp\out\build\win-amd64-release"
$outDir = "$(Split-Path -Parent $PSScriptRoot)\logs\modtest2"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Get-Process tenchuz -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

$log = "$(Split-Path -Parent $PSScriptRoot)\logs\modtest2.log"
Remove-Item $log -Force -ErrorAction SilentlyContinue

$a = @("--log_level=info", "--log_file=`"$log`"", "--log_flush_interval=1", "--mnk_mode=true")
$p = Start-Process -FilePath "$exeDir\tenchuz.exe" -ArgumentList $a -WorkingDirectory $exeDir -PassThru
Write-Host "launched pid=$($p.Id) at $(Get-Date -Format HH:mm:ss.fff)"

$seenMagenta = $false
for ($n = 0; $n -lt 200; $n++) {
    $proc = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
    if (-not $proc) { Write-Host "exited early"; break }

    $wnd = Get-Process tenchuz -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($wnd) {
        $h = $wnd.MainWindowHandle
        $r = New-Object Cap2+RECT
        [Cap2]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
        if ($w -gt 0 -and $hh -gt 0) {
            $bmp = New-Object System.Drawing.Bitmap $w, $hh
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
            $g.Dispose()

            $magenta = 0; $total = 0; $counts = @{}
            for ($y = 0; $y -lt $hh; $y += 24) {
                for ($x = 0; $x -lt $w; $x += 24) {
                    $c = $bmp.GetPixel($x, $y); $total++
                    $key = "{0},{1},{2}" -f $c.R, $c.G, $c.B
                    if ($counts.ContainsKey($key)) { $counts[$key]++ } else { $counts[$key] = 1 }
                    if ($c.R -gt 200 -and $c.G -lt 60 -and $c.B -gt 200) { $magenta++ }
                }
            }
            $pct = if ($total -gt 0) { [Math]::Round(100.0 * $magenta / $total, 1) } else { 0 }
            $top = ($counts.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 2 | ForEach-Object { "$($_.Key):$($_.Value)" }) -join " "

            Write-Host ("[{0,3}] {1} distinct={2} magenta={3}%  top: {4}" -f $n, (Get-Date -Format HH:mm:ss), $counts.Count, $pct, $top)

            if ($pct -gt 5) {
                $bmp.Save("$outDir\magenta_$n.png", [System.Drawing.Imaging.ImageFormat]::Png)
                Write-Host "  *** MAGENTA FOUND ***"
                $seenMagenta = $true
            }
            $bmp.Dispose()
        }
    }
    if ($seenMagenta) { break }
    Start-Sleep -Milliseconds 400
}

Write-Host ""
Write-Host "RESULT: magenta_seen=$seenMagenta"
