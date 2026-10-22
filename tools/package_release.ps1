# Tenchu Z - HoloCatt Recomp Project: builds the release folder and zip.
#
# Output: release\TenchuZ-HoloCattRecomp\ and
# release\TenchuZ-HoloCattRecomp-<version>-windows.zip in the project root.
# The release holds the port (exe, runtime DLLs, Visual C++ runtime), setup
# and launcher scripts, settings, shader presets, docs and licenses.
# It holds NO game content: players add their own disc image and run
# "Setup Tenchu Z.cmd" (tools\release\setup.ps1).
#
# Build first (tools\build_sdk.bat, tools\build_tenchuz.bat), then:
#   powershell -ExecutionPolicy Bypass -File tools\package_release.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "tenchuz-recomp\out\build\win-amd64-release"
# The ReXGlue SDK clone: REXGLUE_SDK, or rexglue-sdk next to this project's folder.
$sdk = if ($env:REXGLUE_SDK) { $env:REXGLUE_SDK } else { Join-Path (Split-Path -Parent $root) "rexglue-sdk" }
$releaseRoot = Join-Path $root "release"
$out = Join-Path $releaseRoot "TenchuZ-HoloCattRecomp"

function Copy-Into([string]$source, [string]$destinationDir, [string]$name = "") {
    if (-not (Test-Path -LiteralPath $source)) { throw "missing: $source" }
    [void][System.IO.Directory]::CreateDirectory($destinationDir)
    $target = if ($name) { Join-Path $destinationDir $name } else { $destinationDir }
    Copy-Item -LiteralPath $source -Destination $target -Recurse -Force
}

if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
[void][System.IO.Directory]::CreateDirectory($out)

# The port.
foreach ($f in "tenchuz.exe", "rexruntime.dll", "rexgpu-xenos.dll") {
    Copy-Into (Join-Path $build $f) $out
}

# Visual C++ runtime, app-local (Microsoft's redistributable files), so
# players do not need to install the VC++ redistributable.
$crt = Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" -Directory |
    Sort-Object FullName | Select-Object -Last 1
foreach ($f in "msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll") {
    Copy-Into (Join-Path $crt.FullName $f) $out
}

# Setup, launcher, settings.
foreach ($f in "setup.ps1", "Setup Tenchu Z.cmd", "README.txt", "tenchuz.toml") {
    Copy-Into (Join-Path $root "tools\release\$f") $out
}
foreach ($f in "Tenchu Z Launcher.cmd", "tenchuz_launcher.ps1") {
    Copy-Into (Join-Path $root "tools\launcher\$f") $out
}

# Shader presets (the preset folders themselves go in shader_overrides\)
# and an empty mods folder.
[void][System.IO.Directory]::CreateDirectory((Join-Path $out "shader_overrides"))
Copy-Item -Path (Join-Path $root "extras\shader_overrides\*") -Destination (Join-Path $out "shader_overrides") -Recurse -Force
[void][System.IO.Directory]::CreateDirectory((Join-Path $out "mods"))
Set-Content -LiteralPath (Join-Path $out "mods\PUT MODS HERE.txt") -Encoding ASCII -Value @(
    "Each mod goes in its own folder here, mirroring the game folder,",
    "e.g. mods\MyMod\DATA\TEXTURE\ENGLISH\title.xpr. See docs\modding.md.")

# Docs.
Copy-Into (Join-Path $root "FEATURES.txt") $out
foreach ($f in "multiplayer.md", "modding.md", "shader_modding.md", "launcher_options.md", "reshade.md") {
    Copy-Into (Join-Path $root "docs\$f") (Join-Path $out "docs")
}

# Licenses: the ReXGlue runtime and every third-party library in its tree.
$licenses = Join-Path $out "licenses"
Copy-Into (Join-Path $sdk "LICENSE") $licenses "ReXGlue-LICENSE.txt"
foreach ($dir in Get-ChildItem (Join-Path $sdk "thirdparty") -Directory) {
    $files = Get-ChildItem -LiteralPath $dir.FullName -File |
        Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING|NOTICE)' -and $_.Name -notmatch 'checker' }
    foreach ($file in $files) {
        Copy-Into $file.FullName $licenses ("{0}-{1}.txt" -f $dir.Name, $file.BaseName)
    }
}

# Never ship game content.
$forbidden = Get-ChildItem -LiteralPath $out -Recurse -File |
    Where-Object { $_.Extension -in ".xex", ".xpr", ".iso", ".xwb", ".wmv", ".bin" -or $_.FullName -match '\\extracted\\' }
if ($forbidden) { throw "game content found in the release: $($forbidden.FullName -join ', ')" }

# Zip, named after the version in tenchuz-recomp\src\version.h.
$versionLine = Select-String -LiteralPath (Join-Path $root "tenchuz-recomp\src\version.h") -Pattern 'TENCHUZ_VERSION "([^"]+)"'
$version = $versionLine.Matches[0].Groups[1].Value
$zip = Join-Path $releaseRoot ("TenchuZ-HoloCattRecomp-{0}-windows.zip" -f $version)
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path $out -DestinationPath $zip -CompressionLevel Optimal

$size = (Get-ChildItem -LiteralPath $out -Recurse -File | Measure-Object Length -Sum).Sum
"Release folder: $out ({0:N1} MB)" -f ($size / 1MB)
"Zip:            $zip ({0:N1} MB)" -f ((Get-Item -LiteralPath $zip).Length / 1MB)
