# Run with Windows PowerShell -STA. Uses a temporary fake installation only.
param([string]$RenderDir)
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ('tenchuz-launcher-test-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path (Join-Path $fixture 'extracted') -Force | Out-Null
[System.IO.File]::WriteAllText((Join-Path $fixture 'tenchuz.exe'), '')
[System.IO.File]::WriteAllText((Join-Path $fixture 'extracted\default.xex'), '')
[System.IO.File]::WriteAllText((Join-Path $fixture 'tenchuz.toml'), @'
# retain this comment
resolution_scale = 3
window_width = 2560
window_height = 1080
fullscreen = false
frame_rate_60 = true
video_mode_refresh_rate = 144.0
vsync = false
d3d12_allow_variable_refresh_rate_and_tearing = false
camera_fov = 62.0
camera_distance = 1.25
mnk_sensitivity = 3.01
menu_ui_scale = 1.5
shader_preset = "Modern Look"
custom_unmanaged_setting = 42
[log.levels]
gpu = "warning"
'@)

$checks = @'
function Assert($Condition, [string]$Message) {
    if (-not $Condition) { throw "FAILED: $Message" }
}
foreach ($entry in $ui.GetEnumerator()) { Assert ($null -ne $entry.Value) "Control $($entry.Key) exists" }
Assert ($ui.RenderScale.SelectedIndex -eq 2) 'existing render scale loads'
Assert ($ui.CameraFov.Text -eq '62.0') 'camera FOV loads'
Assert ($ui.MenuScale.Text -eq '1.5') 'UI scale loads'
Assert ($ui.MouseSensitivity.Text -eq '3.01') 'existing mouse sensitivity loads'
$ui.MouseSensitivity.Text='100.0'
Assert ((Build-Values).mnk_sensitivity -eq 100.0) 'extended sensitivity accepted'
$ui.MouseSensitivity.Text='101.0'
$badSensitivity=$false
try { [void](Build-Values) } catch { $badSensitivity=$true }
Assert $badSensitivity 'out-of-range sensitivity rejected'
$ui.MouseSensitivity.Text='3.01'
Assert ($ui.MouseDirect.IsChecked -eq $true) 'direct mouse camera defaults on'
Assert ($ui.MouseSpeed.Text -eq '1.0') 'direct mouse speed defaults to 1.0'
$ui.MouseSpeed.Text='20.0'
Assert ((Build-Values).camera_mouse_speed -eq 20.0) 'direct mouse speed accepted'
$ui.MouseSpeed.Text='21.0'
$badSpeed=$false
try { [void](Build-Values) } catch { $badSpeed=$true }
Assert $badSpeed 'out-of-range direct mouse speed rejected'
$ui.MouseSpeed.Text='1.0'
$oldWidth=$ui.WinW.Text; $oldHeight=$ui.WinH.Text; $oldScale=$ui.RenderScale.SelectedIndex
$ui.RenderScale.SelectedIndex=0
$ui.OutputResolution.SelectedIndex=2
Assert ($ui.WinW.Text -eq '1920' -and $ui.WinH.Text -eq '1080') '1080p preset selects correct output dimensions'
Assert ($ui.RenderScale.SelectedIndex -eq 1) '1080p preset uses downsampled 2x rendering'
$ui.WinW.Text='2000'
Assert ($ui.OutputResolution.SelectedIndex -eq 0) 'manual dimensions select custom preset'
$ui.WinW.Text=$oldWidth; $ui.WinH.Text=$oldHeight; $ui.RenderScale.SelectedIndex=$oldScale
$deviceExample = '\\.\DISPLAY2'
Assert ((Convert-RootValue (Format-TomlValue -Key display_device -Value $deviceExample)) -eq $deviceExample) 'Windows monitor name escapes round-trip'
$before = Read-RootValues $script:Config
$values = Build-Values
Write-Config $values
$after = Read-RootValues $script:Config
foreach ($key in @('frame_rate_60','video_mode_refresh_rate','vsync','d3d12_allow_variable_refresh_rate_and_tearing')) {
    Assert ($before[$key] -eq $after[$key]) "normal save preserves $key"
    Assert ($script:ProfileKeys -notcontains $key) "profiles exclude $key"
}
$text = [System.IO.File]::ReadAllText($script:Config)
Assert ($text.Contains('# retain this comment')) 'comments survive'
Assert ($text.Contains('custom_unmanaged_setting = 42')) 'unmanaged keys survive'
Assert ($text.Contains('[log.levels]')) 'log table survives'
Assert ($text.IndexOf('camera_fov =') -lt $text.IndexOf('[log.levels]')) 'new settings stay at root'

$ui.GraphicsProfile.Text = 'Ultrawide_Test'
$ui.SaveProfile.RaiseEvent((New-Object System.Windows.RoutedEventArgs([System.Windows.Controls.Button]::ClickEvent)))
$profilePath = Join-Path $script:ProfilesDir 'Ultrawide_Test.toml'
Assert (Test-Path -LiteralPath $profilePath) 'profile saved from button'
$profile = Read-RootValues $profilePath
foreach ($key in $script:ProfileKeys) { Assert ($profile.ContainsKey($key)) "profile includes $key" }
foreach ($key in @('frame_rate_60','vsync','mods_enabled','god_mode')) {
    Assert (-not $profile.ContainsKey($key)) "profile excludes $key"
}
# Unknown and timing keys in an edited profile must never be applied.
Add-Content -LiteralPath $profilePath -Value "frame_rate_60 = false`nvideo_mode_refresh_rate = 30.0`ncustom_unmanaged_setting = 99"
$ui.CameraFov.Text = '75.0'
Write-Config (Build-Values)
function Get-Process { return @() }
$ui.LoadProfile.RaiseEvent((New-Object System.Windows.RoutedEventArgs([System.Windows.Controls.Button]::ClickEvent)))
Assert ($ui.CameraFov.Text -eq '62.0') 'profile restores camera'
$loaded = Read-RootValues $script:Config
Assert ($loaded['frame_rate_60'] -eq 'true') 'edited profile cannot replace fps patch'
Assert ($loaded['video_mode_refresh_rate'] -eq '144.0') 'edited profile cannot replace refresh'
Assert ($loaded['custom_unmanaged_setting'] -eq '42') 'edited profile cannot replace unrelated values'
$configBeforeInvalid = [System.IO.File]::ReadAllText($script:Config)
[System.IO.File]::WriteAllText((Join-Path $script:ProfilesDir 'Invalid.toml'), 'camera_fov = 999.0')
$ui.GraphicsProfile.Text = 'Invalid'
$ui.LoadProfile.RaiseEvent((New-Object System.Windows.RoutedEventArgs([System.Windows.Controls.Button]::ClickEvent)))
Assert ($ui.Status.Text.StartsWith('ERROR:')) 'out-of-range profile rejected'
Assert ([System.IO.File]::ReadAllText($script:Config) -eq $configBeforeInvalid) 'invalid profile leaves config untouched'

foreach ($badName in @('../escape', 'CON', 'NUL', 'LPT1', 'has space')) {
    $ui.GraphicsProfile.Text = $badName
    $rejected = $false
    try { [void](Get-ProfilePath) } catch { $rejected = $true }
    Assert $rejected "unsafe profile name rejected: $badName"
}
$ui.MenuScale.Text = 'NaN'
$rejected = $false
try { [void](Build-Values) } catch { $rejected = $true }
Assert $rejected 'invalid numeric UI setting rejected'
$ui.MenuScale.Text = '1.0'
$ui.GraphicsProfile.Text = 'Ultrawide_Test'

# Intercept process launch and prove recovery does not write the config or
# change the frame timing arguments. No real executable is started.
function Start-Process { param($FilePath, $ArgumentList, $WorkingDirectory, $WindowStyle, [switch]$PassThru)
    $script:CapturedArguments = $ArgumentList
    return [pscustomobject]@{ Id=12345 }
}
$bytesBefore = [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($script:Config))
[void](Start-Game -Recovery)
Assert ([Convert]::ToBase64String([System.IO.File]::ReadAllBytes($script:Config)) -eq $bytesBefore) 'safe launch leaves config byte-identical'
Assert ($script:CapturedArguments -contains '--safe_launch=true') 'recovery flag passed'
Assert ($script:CapturedArguments -contains '--mods_enabled=false') 'mods temporarily disabled'
Assert ($script:CapturedArguments -contains '--shader_overrides_enabled=false') 'shader overrides temporarily disabled'
Assert (-not ($script:CapturedArguments -match '^--(frame_rate_60|video_mode_refresh_rate|vsync|d3d12_allow_variable_refresh_rate_and_tearing)=')) 'safe launch excludes timing overrides'

# Exercise WPF layout at compact and large sizes, without opening a window.
foreach ($width in @(600, 800, 1200)) {
    $window.Width = $width
    $ui.ContentScroll.Measure((New-Object System.Windows.Size($width, 600)))
    $ui.ContentScroll.Arrange((New-Object System.Windows.Rect(0, 0, $width, 600)))
    $ui.ContentScroll.UpdateLayout()
    Assert ($ui.ContentScale.ScaleX -ge 0.75 -and $ui.ContentScale.ScaleX -le 1.8) 'layout scaling is bounded'
    Assert ($ui.ContentScroll.ExtentHeight -gt $ui.ContentScroll.ViewportHeight) 'short window can scroll to all controls'
    if ($RenderDir) {
        New-Item -ItemType Directory -Path $RenderDir -Force | Out-Null
        $ui.ContentScroll.Background = $window.Background
        foreach ($position in @('top', 'bottom')) {
            if ($position -eq 'top') { $ui.ContentScroll.ScrollToTop() } else { $ui.ContentScroll.ScrollToEnd() }
            $ui.ContentScroll.UpdateLayout()
            $bitmap = New-Object System.Windows.Media.Imaging.RenderTargetBitmap($width, 600, 96, 96, [System.Windows.Media.PixelFormats]::Pbgra32)
            $bitmap.Render($ui.ContentScroll)
            $encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
            $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
            $stream = [System.IO.File]::Create((Join-Path $RenderDir "launcher-$width-$position.png"))
            try { $encoder.Save($stream) } finally { $stream.Dispose() }
        }
    }
}
Write-Output 'PASS: launcher initialization, config/profile persistence, frame-rate isolation, safe launch, numeric validation, and resizable WPF layout'
$window.Close()
'@

$source = [System.IO.File]::ReadAllText((Join-Path $projectRoot 'tools\launcher\tenchuz_launcher.ps1'))
if (-not $source.Contains('[void]$window.ShowDialog()')) { throw 'Launcher entrypoint changed; update test harness.' }
$source = $source.Replace('[void]$window.ShowDialog()', $checks)
$harnessPath = Join-Path $fixture 'launcher-test-harness.ps1'
[System.IO.File]::WriteAllText($harnessPath, $source)
& $harnessPath -GameRoot $fixture
# Keep the uniquely named temp fixture for diagnosis; it contains no game data.
