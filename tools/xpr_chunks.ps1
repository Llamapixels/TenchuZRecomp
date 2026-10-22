# Dump the chunk chain of a Tenchu Z .xpr file with correct big-endian field
# interpretation. Chunks are chained by their +0x04 (total size) field.
#
# Usage: powershell -File xpr_chunks.ps1 -Path <file.xpr> [-Max 40]
param(
    [Parameter(Mandatory=$true)][string]$Path,
    [int]$Max = 40
)

$b = [System.IO.File]::ReadAllBytes($Path)
function U32BE($o) { return ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o+1] -shl 16) -bor ([uint32]$b[$o+2] -shl 8) -bor [uint32]$b[$o+3] }
function U16BE($o) { return ([uint16]$b[$o] -shl 8) -bor [uint16]$b[$o+1] }
function Str4($o) { [System.Text.Encoding]::ASCII.GetString($b, $o, 4) }

Write-Host ("file: {0} ({1} bytes)" -f (Split-Path $Path -Leaf), $b.Length)
Write-Host ("hdr : {0} size=0x{1:X8} ver=0x{2:X8} flags=0x{3:X8}" -f (Str4 0), (U32BE 4), (U32BE 8), (U32BE 12))
Write-Host ""

# Chunk layout (big-endian), verified for compactness:
#   +0x00 "TX2D"
#   +0x04 u32 chunk total size
#   +0x08 u32 data offset
#   +0x0C u32 format id
#   +0x10 u32 flags
#   +0x14 u32 width
#   +0x18 u32 height
#   +0x1C u32 depth
#   +0x20 u32 mip count
$o = 0x10
$n = 0
while ($o -lt ($b.Length - 0x34) -and $n -lt $Max) {
    $cm = Str4 $o
    if ($cm -notmatch '^[A-Za-z0-9_]{4}$') {
        Write-Host ("stop at 0x{0:X}: not a chunk magic ({1})" -f $o, $cm)
        break
    }

    $csize = U32BE ($o + 4)
    $doff  = U32BE ($o + 8)
    $fmt   = U32BE ($o + 12)
    $fl    = U32BE ($o + 16)
    $w     = U32BE ($o + 20)
    $h     = U32BE ($o + 24)
    $dep   = U32BE ($o + 28)
    $mip   = U32BE ($o + 32)

    Write-Host ("[{0,3}] @0x{1:X7} {2} size={3,9} dataOff=0x{4:X} fmt=0x{5:X8} flags=0x{6:X8} dims={7}x{8}x{9} mip={10}" -f `
        $n, $o, $cm, $csize, $doff, $fmt, $fl, $w, $h, $dep, $mip)

    if ($csize -le 0 -or $csize -gt $b.Length) {
        Write-Host ("  bad size; stopping")
        break
    }
    $o += $csize
    $n++
}
Write-Host ""
Write-Host ("chunks: {0}; ended at 0x{1:X}; file len 0x{2:X}" -f $n, $o, $b.Length)
