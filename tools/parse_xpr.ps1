# Parse a Tenchu Z .xpr file (XPR2 container, TX2D texture chunks).
#
# ALL multi-byte fields are BIG-ENDIAN (Xbox 360 native order). BitConverter is
# little-endian and MUST NOT be used here.
#
# Verified layout from extracted\DATA\TEXTURE\ames.xpr:
#   0x00  "XPR2"
#   0x04  u32be  size field (0x00000800 here)
#   0x08  u32be  version
#   0x0C  u32be  flags
#   0x10  first chunk; chunks form a chain, each +0x04 is its total size
#
# TX2D chunk (offsets relative to chunk start):
#   +0x00  "TX2D"
#   +0x04  u32be  chunk total size
#   +0x08  u32be  data offset (0x34)
#   +0x0C  u32be  format id (0x28 / 0x2F / 0x268 observed - D3DFMT-like)
#   +0x10..0x2C   additional header words
#
# The 16-byte ASCII texture name sits at data_off+... region; observed reading
# "msesse"/"tyuui" style remnants at +0x34 in this file, so the name is NOT at a
# fixed offset across files and is reported only as a hint.
#
# Usage: powershell -File parse_xpr.ps1 -Path <file.xpr>
param([Parameter(Mandatory=$true)][string]$Path)

$b = [System.IO.File]::ReadAllBytes($Path)

function U32BE($o) {
    return ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o+1] -shl 16) -bor `
           ([uint32]$b[$o+2] -shl 8) -bor [uint32]$b[$o+3]
}
function Str4($o) { [System.Text.Encoding]::ASCII.GetString($b, $o, 4) }

Write-Host ("file     : {0} ({1} bytes)" -f (Split-Path $Path -Leaf), $b.Length)
Write-Host ("magic    : {0}  sizeField=0x{1:X8} version=0x{2:X8} flags=0x{3:X8}" -f `
    (Str4 0), (U32BE 4), (U32BE 8), (U32BE 12))
Write-Host ""

$o = 0x10
$n = 0
while ($o -lt ($b.Length - 0x34) -and $n -lt 64) {
    $cm = Str4 $o
    if ($cm -notmatch '^[A-Za-z0-9_]{4}$') { break }

    $csize = U32BE ($o + 4)
    $doff  = U32BE ($o + 8)
    $fmt   = U32BE ($o + 12)

    # Raw header words, for format work.
    $w0 = U32BE ($o + 0x10)
    $w1 = U32BE ($o + 0x14)
    $w2 = U32BE ($o + 0x18)
    $w3 = U32BE ($o + 0x1C)

    Write-Host ("chunk[{0}] @0x{1:X6} {2} size={3} dataOff=0x{4:X} fmt=0x{5:X}" -f `
        $n, $o, $cm, $csize, $doff, $fmt)
    Write-Host ("         hdrWords: 0x{0:X8} 0x{1:X8} 0x{2:X8} 0x{3:X8}" -f $w0,$w1,$w2,$w3)

    if ($csize -le 0) { break }
    $o += $csize
    $n++
}
Write-Host ""
Write-Host ("chunks parsed: {0}" -f $n)
