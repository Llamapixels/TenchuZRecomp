# Seeds the ReXGlue XAM user profile with the title-specific binary settings
# that Tenchu Z (title id 465307D7) reads at the title screen.
#
# Why: the ReXGlue XAM HLE returns an *unset* title-specific binary setting
# with size=0 and data pointer=0. Tenchu Z reads three title-specific settings
# (0x63E83FFF = 20 bytes, 0x63E83FFE = 80 bytes, 0x63E83FFD = 4 bytes) and
# memcpy()s their contents unconditionally, dereferencing the (null) pointer and
# crashing with an access violation on guest address 0.
#
# A real Xbox profile has these settings written by the game itself; seeding the
# same files makes the HLE path return a valid pointer, exactly as it would
# after the game has saved once.
#
# Defaults are the game's own: the 20-byte record is what sub_82282B10 builds
# from the standard XPROFILE settings (vibration=3, yaxis=0, autoaim=1,
# movement=0, sensitivity=0) and the 80-byte record is the constant block at
# guest 0x828336E8.
param(
  [string]$UserDataRoot = "$env:USERPROFILE\Documents\tenchuz",
  [string]$TitleId = "465307D7",
  [string]$UserName = "User"
)

$ErrorActionPreference = "Stop"

$dir = Join-Path $UserDataRoot (Join-Path $TitleId (Join-Path "profile" $UserName))
New-Item -ItemType Directory -Force -Path $dir | Out-Null

# 0x63E83FFF : 20 bytes
$s1 = [byte[]]@(
  0x00,0x00,0x00,0x03, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x01,
  0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00
)
# 0x63E83FFE : 80 bytes (constant block at guest 0x828336E8)
$s2 = [byte[]]@(
  0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x01,
  0x00,0x00,0x00,0x02, 0x00,0x00,0x00,0x03, 0x00,0x00,0x00,0x04,
  0x00,0x00,0x00,0x05, 0x00,0x00,0x00,0x06, 0x00,0x00,0x00,0x07,
  0x00,0x00,0x00,0x01, 0x3F,0x40,0x00,0x00, 0x3F,0x40,0x00,0x00,
  0x3F,0x40,0x00,0x00, 0x00,0x00,0x00,0x01, 0x00,0x00,0x00,0x01,
  0x00,0x00,0x00,0x01, 0x00,0x00,0x01,0x18, 0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x01
)
# 0x63E83FFD : 4 bytes
$s3 = [byte[]]@(0x00,0x00,0x00,0x00)

[IO.File]::WriteAllBytes((Join-Path $dir "63E83FFF"), $s1)
[IO.File]::WriteAllBytes((Join-Path $dir "63E83FFE"), $s2)
[IO.File]::WriteAllBytes((Join-Path $dir "63E83FFD"), $s3)

Write-Output "seeded $dir"
Get-ChildItem $dir | ForEach-Object { "  {0}  {1} bytes" -f $_.Name, $_.Length }
