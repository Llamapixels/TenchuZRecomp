# Tenchu Z PC - first-time setup.
#
#   1. Extracts the game files from the player's own Tenchu Z Xbox 360 disc
#      image (.iso) into the "extracted" folder next to tenchuz.exe.
#   2. Creates the three profile settings files the game reads at the title
#      screen (Documents\tenchuz\465307D7\profile\User). Without them the game
#      crashes there until it has saved once. Existing files are kept.
#
# Nothing is downloaded and nothing outside this folder and Documents\tenchuz
# is changed. Run it with "Setup Tenchu Z.cmd", or:
#   powershell -ExecutionPolicy Bypass -File setup.ps1 [-Iso <path>]
#
# The disc reader is a port of tools\extract_xdvdfs.py (XDVDFS: little-endian
# directory trees of 16-bit links, 2048-byte sectors).

param([string]$Iso = "")

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $PSCommandPath
$out = Join-Path $here "extracted"

function Pause-Exit([int]$code) {
    Write-Host ""
    Read-Host "Press Enter to close" | Out-Null
    exit $code
}

function Fail([string]$message) {
    Write-Host ""
    Write-Host "Setup failed: $message" -ForegroundColor Red
    Pause-Exit 1
}

# --- 1. Game files -----------------------------------------------------------

$Magic = [System.Text.Encoding]::ASCII.GetBytes("MICROSOFT*XBOX*MEDIA")
$Sector = 2048

function Read-Bytes($fs, [long]$offset, [int]$count) {
    $buffer = New-Object byte[] $count
    [void]$fs.Seek($offset, [System.IO.SeekOrigin]::Begin)
    $done = 0
    while ($done -lt $count) {
        $read = $fs.Read($buffer, $done, $count - $done)
        if ($read -le 0) { break }
        $done += $read
    }
    return ,$buffer
}

# SHA-1 of default.xex of Tenchu Z (USA, Europe) (En,Fr,De,Es,It), the version
# this port is recompiled from.
$SupportedXexSha1 = "DDE36F938FFB0B3226AF32D4D9FF10E6750C5A0B"

function Get-RangeSha1($fs, [long]$offset, [long]$length) {
    $sha = [System.Security.Cryptography.SHA1]::Create()
    $buffer = New-Object byte[] (1MB)
    [void]$fs.Seek($offset, [System.IO.SeekOrigin]::Begin)
    $remaining = $length
    while ($remaining -gt 0) {
        $read = $fs.Read($buffer, 0, [int][Math]::Min([long]$buffer.Length, $remaining))
        if ($read -le 0) { break }
        [void]$sha.TransformBlock($buffer, 0, $read, $null, 0)
        $remaining -= $read
    }
    [void]$sha.TransformFinalBlock($buffer, 0, 0)
    return ([BitConverter]::ToString($sha.Hash) -replace '-', '')
}

function Test-Magic($fs, [long]$offset) {
    if ($offset + 0x800 -gt $fs.Length) { return $false }
    $bytes = Read-Bytes $fs $offset 20
    for ($i = 0; $i -lt 20; $i++) {
        if ($bytes[$i] -ne $Magic[$i]) { return $false }
    }
    return $true
}

# Entries of the directory occupying [base + sector*2048, +size):
# (name, start sector, size, is directory).
function Read-Directory($fs, [long]$base, [uint32]$sectorIndex, [uint32]$size) {
    $block = Read-Bytes $fs ($base + [long]$sectorIndex * $Sector) ([int]$size)
    $entries = New-Object System.Collections.ArrayList
    $visited = @{}
    $stack = New-Object System.Collections.Stack
    $stack.Push(0)
    while ($stack.Count -gt 0) {
        $offset = [int]$stack.Pop()
        if ($offset + 14 -gt $block.Length -or $visited.ContainsKey($offset)) { continue }
        $visited[$offset] = $true
        $left = [BitConverter]::ToUInt16($block, $offset)
        $right = [BitConverter]::ToUInt16($block, $offset + 2)
        $start = [BitConverter]::ToUInt32($block, $offset + 4)
        $length = [BitConverter]::ToUInt32($block, $offset + 8)
        $attrs = $block[$offset + 12]
        $nameLength = $block[$offset + 13]
        if ($nameLength -gt 0 -and $offset + 14 + $nameLength -le $block.Length) {
            $name = [System.Text.Encoding]::ASCII.GetString($block, $offset + 14, $nameLength)
            $name = ($name -replace '[\x00-\x1F\x7F/\\:*?"<>|]', '').Trim()
            if ($name -and $name -ne "." -and $name -ne "..") {
                [void]$entries.Add([pscustomobject]@{
                    Name = $name; Start = $start; Size = $length
                    IsDir = (($attrs -band 0x10) -ne 0)
                })
            }
        }
        if ($right) { $stack.Push([int]$right * 4) }
        if ($left) { $stack.Push([int]$left * 4) }
    }
    return ,$entries
}

# Every file under a directory: (relative path, start sector, size).
function Get-Files($fs, [long]$base, [uint32]$sectorIndex, [uint32]$size, [string]$relative) {
    $files = New-Object System.Collections.ArrayList
    foreach ($entry in (Read-Directory $fs $base $sectorIndex $size)) {
        $path = if ($relative) { Join-Path $relative $entry.Name } else { $entry.Name }
        if ($entry.IsDir) {
            if (-not $relative -and $entry.Name -eq '$SystemUpdate') { continue }
            foreach ($f in (Get-Files $fs $base $entry.Start $entry.Size $path)) { [void]$files.Add($f) }
        } else {
            [void]$files.Add([pscustomobject]@{ Path = $path; Start = $entry.Start; Size = $entry.Size })
        }
    }
    return ,$files
}

function Extract-Iso([string]$isoPath) {
    $fs = [System.IO.File]::Open($isoPath, 'Open', 'Read', 'Read')
    try {
        # Where the game partition's volume descriptor sits: plain XDVDFS
        # images, original discs (XGD2, XGD3).
        $descriptor = -1
        foreach ($candidate in @(0x10000, 0xFDA0000, 0x2090000)) {
            if ((Test-Magic $fs $candidate) -and (Test-Magic $fs ($candidate + 0x7EC))) {
                $descriptor = $candidate
                break
            }
        }
        if ($descriptor -lt 0) {
            Fail "This file is not an Xbox 360 disc image (no game partition found)."
        }
        $base = [long]$descriptor - 0x10000
        $header = Read-Bytes $fs ($descriptor + 0x14) 8
        $rootSector = [BitConverter]::ToUInt32($header, 0)
        $rootSize = [BitConverter]::ToUInt32($header, 4)

        Write-Host "Reading the disc's file list..."
        $files = Get-Files $fs $base $rootSector $rootSize ""
        $xex = $files | Where-Object { $_.Path -eq 'default.xex' } | Select-Object -First 1
        if (-not $xex) {
            Fail "The disc image has no default.xex. Is it really Tenchu Z?"
        }
        # This port is built from one version of the game's code; another
        # version would not run.
        if ((Get-RangeSha1 $fs ($base + [long]$xex.Start * $Sector) $xex.Size) -ne $SupportedXexSha1) {
            Fail ("This disc image is a different version of Tenchu Z. This port needs the " +
                  "Xbox 360 USA/Europe release (En, Fr, De, Es, It).")
        }
        Write-Host "Version: Tenchu Z (USA, Europe) - supported."
        $total = [long]0
        foreach ($f in $files) { $total += $f.Size }
        Write-Host ("{0} files, {1:N2} GB" -f $files.Count, ($total / 1GB))

        $buffer = New-Object byte[] (8MB)
        $copied = [long]0
        $count = 0
        foreach ($f in $files) {
            $target = Join-Path $out $f.Path
            [void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $target))
            $dst = [System.IO.File]::Create($target)
            try {
                [void]$fs.Seek($base + [long]$f.Start * $Sector, [System.IO.SeekOrigin]::Begin)
                $remaining = [long]$f.Size
                while ($remaining -gt 0) {
                    $want = [int][Math]::Min([long]$buffer.Length, $remaining)
                    $read = $fs.Read($buffer, 0, $want)
                    if ($read -le 0) { throw "The disc image ended early (is the download complete?)" }
                    $dst.Write($buffer, 0, $read)
                    $remaining -= $read
                    $copied += $read
                }
            } finally {
                $dst.Dispose()
            }
            $count++
            if ($count % 20 -eq 0 -or $count -eq $files.Count) {
                $percent = if ($total) { [int](100 * $copied / $total) } else { 100 }
                Write-Progress -Activity "Extracting Tenchu Z" `
                    -Status ("{0} of {1} files ({2:N2} GB)" -f $count, $files.Count, ($copied / 1GB)) `
                    -PercentComplete $percent
            }
        }
        Write-Progress -Activity "Extracting Tenchu Z" -Completed
    } finally {
        $fs.Dispose()
    }
}

Write-Host "Tenchu Z PC setup" -ForegroundColor Cyan
Write-Host ""

if (Test-Path -LiteralPath (Join-Path $out "default.xex")) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $out "default.xex") -Algorithm SHA1).Hash
    if ($hash -ne $SupportedXexSha1) {
        Fail ("extracted\ holds a different version of Tenchu Z. This port needs the Xbox 360 " +
              "USA/Europe release. Delete extracted\ and run setup with that disc image.")
    }
    Write-Host "Game files: already in place (extracted\)."
} else {
    if (-not $Iso) {
        $found = @(Get-ChildItem -LiteralPath $here -Filter *.iso -File)
        if ($found.Count -eq 1) {
            $Iso = $found[0].FullName
        } else {
            Add-Type -AssemblyName System.Windows.Forms
            $dialog = New-Object System.Windows.Forms.OpenFileDialog
            $dialog.Title = "Choose your Tenchu Z Xbox 360 disc image"
            $dialog.Filter = "Xbox 360 disc image (*.iso)|*.iso|All files (*.*)|*.*"
            $dialog.InitialDirectory = $here
            if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
                Fail "No disc image chosen. Put your Tenchu Z .iso in this folder and run setup again."
            }
            $Iso = $dialog.FileName
        }
    }
    Write-Host "Disc image: $Iso"
    try {
        Extract-Iso $Iso
    } catch {
        if (Test-Path -LiteralPath $out) {
            Remove-Item -LiteralPath $out -Recurse -Force -ErrorAction SilentlyContinue
        }
        Fail $_.Exception.Message
    }
    Write-Host "Game files extracted to extracted\." -ForegroundColor Green
    Write-Host "(You can delete or move the .iso now; the game only needs extracted\.)"
}

# --- 2. Profile settings ------------------------------------------------------

# Defaults are the game's own (see tools\seed_profile.ps1 in the source).
$profileDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "tenchuz\465307D7\profile\User"
[void][System.IO.Directory]::CreateDirectory($profileDir)
$seeds = @{
    "63E83FFF" = [byte[]]@(
        0x00,0x00,0x00,0x03, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x01,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00)
    "63E83FFE" = [byte[]]@(
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x01,
        0x00,0x00,0x00,0x02, 0x00,0x00,0x00,0x03, 0x00,0x00,0x00,0x04,
        0x00,0x00,0x00,0x05, 0x00,0x00,0x00,0x06, 0x00,0x00,0x00,0x07,
        0x00,0x00,0x00,0x01, 0x3F,0x40,0x00,0x00, 0x3F,0x40,0x00,0x00,
        0x3F,0x40,0x00,0x00, 0x00,0x00,0x00,0x01, 0x00,0x00,0x00,0x01,
        0x00,0x00,0x00,0x01, 0x00,0x00,0x01,0x18, 0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x01)
    "63E83FFD" = [byte[]]@(0x00,0x00,0x00,0x00)
}
$created = 0
foreach ($name in $seeds.Keys) {
    $path = Join-Path $profileDir $name
    if (-not (Test-Path -LiteralPath $path)) {
        [System.IO.File]::WriteAllBytes($path, $seeds[$name])
        $created++
    }
}
if ($created) {
    Write-Host "Profile settings created in $profileDir."
} else {
    Write-Host "Profile settings: already in place."
}

Write-Host ""
Write-Host "Setup done. Start the game with 'Tenchu Z Launcher.cmd' (settings) or tenchuz.exe." -ForegroundColor Green
Pause-Exit 0
