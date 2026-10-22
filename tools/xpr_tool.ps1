# Tenchu Z .xpr archive tool - list and extract textures.
#
# Format verified byte-for-byte against extracted\DATA\TEXTURE\ENGLISH\title.xpr
# and ...\ENGLISH\ames.xpr. ALL multi-byte values are BIG-ENDIAN (Xbox 360
# native byte order); BitConverter is little-endian and must NOT be used here.
#
# Header (0x10 bytes)
#   +0x00  "XPR2"
#   +0x04  u32be  length of the (index + name) section, in bytes (e.g. 0x800).
#                 NOT an absolute file offset - the name table follows the index.
#   +0x08  u32be  version
#   +0x0C  u32be  flags
#
# Index: begins at 0x10, 16 bytes per entry, count = indexLength/16 in practice
# but the true end is where the "TX2D" magic stops. Each entry:
#   +0x00  "TX2D"
#   +0x04  u32be  entry data size (bytes of pixel payload)
#   +0x08  u32be  offset of the payload, relative to the index section start
#                 (0x34 in the samples seen)
#   +0x0C  u32be  texture id
#
# Name table: immediately after the index, NUL-separated ASCII, one name per
# index entry, in the same order.
#
# Pixel payloads start at 0x10 + entry.DataOff + (sum of preceding data sizes).
#
# Usage:
#   powershell -File xpr_tool.ps1 -Path <file.xpr> -List
#   powershell -File xpr_tool.ps1 -Path <file.xpr> -Extract -OutDir <dir>
param(
    [Parameter(Mandatory=$true)][string]$Path,
    [switch]$List,
    [switch]$Extract,
    [string]$OutDir = "."
)

$b = [System.IO.File]::ReadAllBytes($Path)
function U32BE($o) { return ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o+1] -shl 16) -bor ([uint32]$b[$o+2] -shl 8) -bor [uint32]$b[$o+3] }
function Str4($o) { [System.Text.Encoding]::ASCII.GetString($b, $o, 4) }

if ((Str4 0) -ne 'XPR2') { Write-Error "not an XPR2 file"; exit 1 }

$indexLen = U32BE 4
$version  = U32BE 8
$flags    = U32BE 12

# --- Index ---------------------------------------------------------------
$index = @()
$o = 0x10
while ($o + 16 -le $b.Length) {
    if ((Str4 $o) -ne 'TX2D') { break }
    $index += [pscustomobject]@{
        IndexOffset = $o
        DataSize    = U32BE ($o + 4)
        DataOff     = U32BE ($o + 8)
        Id          = U32BE ($o + 12)
    }
    $o += 16
}
$nameTableOff = $o

# --- Names ---------------------------------------------------------------
$names = @()
$cur = New-Object System.Text.StringBuilder
for ($i = $nameTableOff; $i -lt $b.Length -and $names.Count -lt $index.Count; $i++) {
    if ($b[$i] -eq 0) {
        if ($cur.Length -gt 0) { $names += $cur.ToString(); $cur.Clear() | Out-Null }
    } else {
        [void]$cur.Append([char]$b[$i])
    }
}

# --- Payloads ------------------------------------------------------------
# Payloads follow the index section; derive each start by accumulating sizes.
# The first payload starts at 0x10 + index[0].DataOff.
$payloadBase = 0x10 + $index[0].DataOff
$cursor = $payloadBase
for ($i = 0; $i -lt $index.Count; $i++) {
    $index[$i] | Add-Member -NotePropertyName PayloadOffset -NotePropertyValue $cursor -Force
    $cursor += $index[$i].DataSize
}

Write-Host ("file      : {0} ({1} bytes)" -f (Split-Path $Path -Leaf), $b.Length)
Write-Host ("version   : 0x{0:X8}  flags=0x{1:X8}  indexLen=0x{2:X}" -f $version, $flags, $indexLen)
Write-Host ("entries   : {0}  names={1}  nameTable@0x{2:X}  payloads@0x{3:X}" -f `
    $index.Count, $names.Count, $nameTableOff, $payloadBase)
Write-Host ""

if ($List -or -not $Extract) {
    for ($i = 0; $i -lt $index.Count; $i++) {
        $e = $index[$i]
        $nm = if ($i -lt $names.Count) { $names[$i] } else { "?" }
        Write-Host ("[{0,3}] {1,-16} id=0x{2:X4} dataSize={3,9} payloadOff=0x{4:X}" -f `
            $i, $nm, $e.Id, $e.DataSize, $e.PayloadOffset)
    }
    $total = ($index | Measure-Object -Property DataSize -Sum).Sum
    Write-Host ""
    Write-Host ("total entries: {0}  payload bytes: {1}" -f $index.Count, $total)
}

if ($Extract) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $base = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    $written = 0
    for ($i = 0; $i -lt $index.Count; $i++) {
        $e = $index[$i]
        $nm = if ($i -lt $names.Count) { $names[$i] } else { "tex$i" }
        $safe = ($nm -replace '[^A-Za-z0-9_\-]', '_')
        $start = $e.PayloadOffset
        $len = $e.DataSize
        if ($len -le 0 -or $start + $len -gt $b.Length) { continue }
        $dest = Join-Path $OutDir ("{0}_{1:D3}_{2}.bin" -f $base, $i, $safe)
        $slice = New-Object byte[] $len
        [Array]::Copy($b, $start, $slice, 0, $len)
        [System.IO.File]::WriteAllBytes($dest, $slice)
        $written++
    }
    Write-Host ("extracted {0} payloads to {1}" -f $written, $OutDir)
}
