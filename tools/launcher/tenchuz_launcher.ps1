<#
    Tenchu Z (ReXGlue) Launcher
    ==========================
    A small GUI front-end for the ReXGlue runtime configuration of Tenchu Z.

    What it does
    ------------
    * Edits the runtime config file (tenchuz.toml) that lives next to tenchuz.exe.
    * Lets you pick:
        - Rendering resolution  (internal draw resolution scaler, 1x .. 8x)
        - Output resolution     (guest video mode: 720p / 1080p / 1440p / 4K / custom)
        - Window size           (independent of the guest video mode)
        - Fullscreen
        - Frame rate / refresh  (guest video mode refresh rate, 24..240 Hz)
        - Vertical sync         (on/off) and tearing
        - Cheats                (god mode, free shop purchases) and mods
    * Starts tenchuz.exe with the correct --game_data_root quoting.

    Works in two layouts:
      * Packaged: this script sits next to tenchuz.exe, with the game files in
        an "extracted" folder beside them.
      * Development: tools\launcher\ inside the game root, with the exe in
        tenchuz-recomp\out\build\win-amd64-release.

    Every option here is a runtime cvar verified against the ReXGlue SDK source.
    Keys written by the launcher are documented in docs/launcher_options.md.

    Usage
    -----
      pwsh -File tools\launcher\tenchuz_launcher.ps1
      powershell -ExecutionPolicy Bypass -File tools\launcher\tenchuz_launcher.ps1
#>

[CmdletBinding()]
param(
    # Override auto-detection of the game root (the folder containing tenchuz-recomp\)
    [string]$GameRoot,
    # Just write the config and exit, without showing the UI (useful for scripts)
    [switch]$Headless,
    # Render scale to apply in headless mode, e.g. -Preset 3x
    [string]$Preset,
    # Recovery uses command-line overrides for one run, without saving config.
    [switch]$SafeLaunch
)

Set-StrictMode -Version Latest

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

function Show-Fatal {
    param([string]$Message)
    # When started by double-click the console closes immediately, so a thrown
    # error would never be seen. Show it in a dialog as well.
    if (-not $Headless) {
        Add-Type -AssemblyName PresentationFramework
        [void][System.Windows.MessageBox]::Show($Message, 'Tenchu Z Launcher', 'OK', 'Error')
    }
    throw $Message
}

# Returns the folders holding tenchuz.exe (with tenchuz.toml and mods\) and the
# game root (holding extracted\).
function Find-Layout {
    param([string]$Explicit)

    $here = Split-Path -Parent $PSCommandPath
    if ($Explicit) {
        if (-not (Test-Path -LiteralPath $Explicit)) { Show-Fatal "GameRoot not found: $Explicit" }
        $roots = @((Resolve-Path -LiteralPath $Explicit).Path)
    } else {
        $roots = @($here, (Join-Path $here '..\..'), (Join-Path $here '..'), $PWD.Path)
    }

    foreach ($c in $roots) {
        $full = [System.IO.Path]::GetFullPath($c)
        # Packaged layout: exe right here.
        if (Test-Path -LiteralPath (Join-Path $full 'tenchuz.exe')) {
            return @{ ExeDir = $full; GameRoot = $full }
        }
        # Development layout: game root containing tenchuz-recomp\.
        $dev = Join-Path $full 'tenchuz-recomp\out\build\win-amd64-release'
        if (Test-Path -LiteralPath (Join-Path $dev 'tenchuz.exe')) {
            return @{ ExeDir = $dev; GameRoot = $full }
        }
    }
    Show-Fatal "Could not find tenchuz.exe. Put this launcher next to tenchuz.exe, or pass -GameRoot <path>."
}

$script:Layout   = Find-Layout -Explicit $GameRoot
$script:GameRoot = $script:Layout.GameRoot
$script:ExeDir   = $script:Layout.ExeDir
$script:Exe      = Join-Path $script:ExeDir   'tenchuz.exe'
$script:Config   = Join-Path $script:ExeDir   'tenchuz.toml'
$script:DataRoot = Join-Path $script:GameRoot 'extracted'
$script:ModsDir  = Join-Path $script:ExeDir   'mods'
$script:LogDir   = Join-Path $script:GameRoot 'logs'
$script:LogFile  = Join-Path $script:LogDir   'tenchuz.log'

if (-not (Test-Path -LiteralPath (Join-Path $script:DataRoot 'default.xex'))) {
    Show-Fatal "Game files not found. Extract your own copy of Tenchu Z so that this file exists:`n$(Join-Path $script:DataRoot 'default.xex')"
}

# ---------------------------------------------------------------------------
# cvar knowledge (verified against rexglue-sdk source)
# ---------------------------------------------------------------------------

# Keys this launcher owns. Anything else in tenchuz.toml (comments, other cvars
# such as use_fuzzy_alpha_epsilon) is preserved untouched.
$script:ManagedKeys = @(
    'resolution_scale',
    'draw_resolution_scale_x',
    'draw_resolution_scale_y',
    'resolution',
    'video_mode_width',
    'video_mode_height',
    'video_mode_refresh_rate',
    'window_width',
    'window_height',
    'fullscreen',
    'vsync',
    'd3d12_allow_variable_refresh_rate_and_tearing',
    'god_mode',
    'free_shopping',
    'mods_enabled',
    'skip_intro_movies',
    'mnk_mouse',
    'mnk_sensitivity',
    'camera_mouse_direct',
    'camera_mouse_speed',
    'dump_shaders',
    'gpu_backend',
    'frame_rate_60',
    'display_device', 'present_letterbox', 'widescreen', 'menu_ui_scale',
    'camera_fov', 'camera_distance', 'shader_preset', 'shader_effects',
    'shader_overrides_enabled', 'widescreen_hud_correct', 'game_hud_scale',
    'game_hud_offset_x', 'game_hud_offset_y'
)

# Keep in sync with display_settings.cpp. Profiles never alter frame timing.
$script:ProfileKeys = @('gpu_backend', 'resolution_scale', 'window_width',
    'window_height', 'fullscreen', 'display_device', 'present_letterbox',
    'widescreen', 'menu_ui_scale', 'camera_fov', 'camera_distance',
    'shader_preset', 'shader_effects', 'shader_overrides_enabled',
    'widescreen_hud_correct', 'game_hud_scale', 'game_hud_offset_x', 'game_hud_offset_y')
$script:ProfilesDir = Join-Path $script:ExeDir 'graphics_profiles'

# Tenchu Z always draws a 1280x720 frame, whatever video mode it is given:
# measured by hooking the game's Swap (gameplay.cpp logs "game frame size"),
# which reports 1280x720 at both a 1280x720 and a 2560x1440 video mode. A
# larger video mode only makes the runtime stretch that frame, so the launcher
# pins the video mode to native and uses resolution_scale for sharpness.
$script:GameFrame = @{ w = 1280; h = 720 }

# Render scales offered. The SDK clamps resolution_scale to 1..7
# (TextureCache::kMaxDrawResolutionScaleAlongAxis) and lowers it further if the
# GPU cannot address the scaled EDRAM copy.
$script:RenderScales = @(1, 2, 3, 4, 5, 6, 7)

# Refresh presets in Hz. The guest video mode refresh rate is what the game's
# own frame pacing sees; the range accepted by the SDK is 24..240.
$script:RefreshPresets = @('30', '50', '60', '72', '90', '120', '144', '165', '240')

# ---------------------------------------------------------------------------
# Config file read / write
# ---------------------------------------------------------------------------

function Read-ConfigLines {
    if (Test-Path -LiteralPath $script:Config) {
        return ,@(Get-Content -LiteralPath $script:Config)
    }
    return ,@()
}

function Format-TomlValue {
    param([string]$Key, $Value)
    if ($Value -is [bool]) { if ($Value) { return 'true' } else { return 'false' } }
    if ($Value -is [int] -or $Value -is [long]) { return "$Value" }
    if ($Value -is [double] -or $Value -is [float]) {
        # Keep a decimal point so TOML does not read it as an integer.
        return ([double]$Value).ToString('0.0###', [System.Globalization.CultureInfo]::InvariantCulture)
    }
    # Strings must be quoted.
    $escaped = ([string]$Value).Replace('\', '\\').Replace('"', '\"')
    return '"' + $escaped + '"'
}

<#
    Merge the managed keys into the config file.

    - Existing key lines are replaced in place (preserving surrounding comments).
    - Missing keys are appended in a clearly-marked block.
    - Keys with $null values are removed from the managed block (so optional
      overrides such as video_mode_width can be left to the resolution preset).
#>
function Write-Config {
    param([hashtable]$Values, [string]$Path = $script:Config,
          [string[]]$Keys = $script:ManagedKeys)

    $lines = @()
    if (Test-Path -LiteralPath $Path) { $lines = @(Get-Content -LiteralPath $Path) }
    $out = New-Object System.Collections.Generic.List[string]
    $seen = @{}

    # Runtime cvars are read from the root of the TOML file only. Anything
    # after the first [table] header (such as [log.levels]) belongs to that
    # table, so managed keys are matched - and missing ones inserted - only
    # above it. Appending them at the end of the file would silently turn
    # them into log.levels.<key> and the runtime would ignore them.
    $keyRegex = '^\s*([A-Za-z0-9_]+)\s*='
    $inRoot = $true
    $firstTable = -1
    foreach ($line in $lines) {
        if ($line -match '^\s*\[') {
            if ($inRoot) {
                # Insert above the comment block that introduces the table.
                $firstTable = $out.Count
                while ($firstTable -gt 0 -and $out[$firstTable - 1] -match '^\s*#') { $firstTable-- }
            }
            $inRoot = $false
        }
        $m = [regex]::Match($line, $keyRegex)
        if ($inRoot -and $m.Success) {
            $key = $m.Groups[1].Value
            if ($Keys -contains $key) {
                $seen[$key] = $true
                if ($null -ne $Values[$key]) {
                    $out.Add(("$key = " + (Format-TomlValue -Key $key -Value $Values[$key])))
                }
                # $null value => drop the line (revert to SDK default)
                continue
            }
        }
        $out.Add($line)
    }

    # Insert any managed keys that were not present in the file at all.
    $missing = New-Object System.Collections.Generic.List[string]
    foreach ($key in $Keys) {
        if ($null -ne $Values[$key] -and -not $seen.ContainsKey($key)) {
            $missing.Add(("$key = " + (Format-TomlValue -Key $key -Value $Values[$key])))
        }
    }
    if ($missing.Count -gt 0) {
        $block = New-Object System.Collections.Generic.List[string]
        $block.Add('# --- Tenchu Z Launcher managed settings ---')
        $block.AddRange($missing)
        $block.Add('')
        if ($firstTable -lt 0) {
            if ($out.Count -gt 0 -and $out[$out.Count - 1].Trim() -ne '') { $out.Add('') }
            $out.AddRange($block)
        } else {
            $out.InsertRange($firstTable, $block)
        }
    }

    $text = ($out -join [Environment]::NewLine).TrimEnd() + [Environment]::NewLine

    # Back up once per session so a bad edit is easy to undo.
    if (Test-Path -LiteralPath $script:Config) {
        $bak = "$($script:Config).launcher.bak"
        if (-not (Test-Path -LiteralPath $bak)) {
            Copy-Item -LiteralPath $script:Config -Destination $bak -Force
        }
    }

    # Windows PowerShell's -Encoding UTF8 writes a byte-order mark; write plain
    # UTF-8 instead so the file stays a clean TOML document.
    [System.IO.File]::WriteAllText($Path, $text, (New-Object System.Text.UTF8Encoding($false)))
}

# ---------------------------------------------------------------------------
# Launch
# ---------------------------------------------------------------------------

function Start-Game {
    param([switch]$WaitForExit, [switch]$Recovery)

    $running = @(Get-Process tenchuz -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        # Never kill a running game silently: it may be mid-mission.
        $answer = 'Yes'
        if (-not $Headless) {
            $answer = [System.Windows.MessageBox]::Show(
                'Tenchu Z is already running. Close it and start again? Unsaved progress will be lost.',
                'Tenchu Z Launcher', 'YesNo', 'Warning')
        }
        if ([string]$answer -ne 'Yes') { return $null }
        $running | Stop-Process -Force -ErrorAction SilentlyContinue
        $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
    }

    if (-not (Test-Path -LiteralPath $script:LogDir)) {
        New-Item -ItemType Directory -Path $script:LogDir -Force | Out-Null
    }

    # Paths must be quoted in-place: they may contain spaces. ($args is a
    # PowerShell automatic variable, hence the different name.)
    $gameArgs = @(
        "--game_data_root=`"$($script:DataRoot)`"",
        '--log_level=info',
        "--log_file=`"$($script:LogFile)`"",
        '--log_flush_interval=1',
        '--mnk_mode=true'
    )
    if ($Recovery) {
        # CLI outranks TOML. Neither the launcher nor the recovery game saves
        # these overrides. Preserve all framerate/refresh/vsync/tearing values.
        $gameArgs += @('--safe_launch=true', '--gpu_backend=d3d12',
            '--resolution_scale=1', '--draw_resolution_scale_x=1', '--draw_resolution_scale_y=1',
            '--window_width=1280', '--window_height=720', '--fullscreen=false',
            '--monitor=0', '--display_device=""', '--widescreen=false', '--present_letterbox=true',
            '--menu_ui_scale=1.0', '--mods_enabled=false', '--shader_overrides_enabled=false',
            '--shader_override_dir=""',
            '--shader_effects=false', '--camera_fov=48.0', '--camera_distance=1.0')
        $gameArgs += @('--game_hud_scale=1.0', '--game_hud_offset_x=0.0', '--game_hud_offset_y=0.0')
    }

    $p = Start-Process -FilePath $script:Exe -ArgumentList $gameArgs `
                       -WorkingDirectory $script:ExeDir -WindowStyle Hidden -PassThru
    if ($WaitForExit) { $p.WaitForExit() }
    return $p
}

if ($SafeLaunch) {
    [void](Start-Game -Recovery)
    return
}

# ---------------------------------------------------------------------------
# WPF UI
# ---------------------------------------------------------------------------

Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

[xml]$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Tenchu Z - ReXGlue Launcher"
        Width="800" Height="900" MinWidth="600" MinHeight="480"
        WindowStartupLocation="CenterScreen"
        ResizeMode="CanResize"
        Background="#1E1E1E">
  <Window.Resources>
    <Style TargetType="TextBlock">
      <Setter Property="Foreground" Value="#E8E8E8"/>
      <Setter Property="VerticalAlignment" Value="Center"/>
    </Style>
    <Style TargetType="Label">
      <Setter Property="Foreground" Value="#E8E8E8"/>
      <Setter Property="VerticalAlignment" Value="Center"/>
    </Style>
    <Style TargetType="CheckBox">
      <Setter Property="Foreground" Value="#E8E8E8"/>
      <Setter Property="VerticalAlignment" Value="Center"/>
      <Setter Property="Margin" Value="0,4,0,4"/>
    </Style>
    <Style TargetType="GroupBox">
      <Setter Property="Foreground" Value="#9CDCFE"/>
      <Setter Property="Margin" Value="0,0,0,10"/>
      <Setter Property="Padding" Value="8"/>
    </Style>
    <Style TargetType="ComboBox">
      <Setter Property="Margin" Value="0,2,0,2"/>
    </Style>
    <Style TargetType="TextBox">
      <Setter Property="Margin" Value="0,2,0,2"/>
      <Setter Property="Background" Value="#2D2D30"/>
      <Setter Property="Foreground" Value="#E8E8E8"/>
      <Setter Property="BorderBrush" Value="#3F3F46"/>
      <Setter Property="Padding" Value="3"/>
    </Style>
  </Window.Resources>

  <ScrollViewer x:Name="ContentScroll" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled">
  <Grid Margin="14" Width="530" HorizontalAlignment="Center">
    <Grid.LayoutTransform><ScaleTransform x:Name="ContentScale" ScaleX="1" ScaleY="1"/></Grid.LayoutTransform>
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
    </Grid.RowDefinitions>

    <!-- Rendering resolution -->
    <GroupBox Grid.Row="0" Header="Graphics">
      <StackPanel>
        <TextBlock Text="Graphics profiles" Margin="0,0,0,4"/>
        <ComboBox x:Name="GraphicsProfile" IsEditable="True" ToolTip="Name: up to 48 letters, numbers, - or _"/>
        <WrapPanel Margin="0,4,0,10">
          <Button x:Name="SaveProfile" Content="Save current graphics" Padding="8,3" Margin="0,0,8,0"/>
          <Button x:Name="LoadProfile" Content="Load profile" Padding="8,3"/>
        </WrapPanel>
        <TextBlock Text="Profiles include display, camera and shader settings. Frame rate, controls, cheats and mods stay as you set them." TextWrapping="Wrap" Foreground="#A0A0A0" Margin="0,0,0,8"/>
      <Grid>
        <Grid.ColumnDefinitions>
          <ColumnDefinition Width="150"/>
          <ColumnDefinition Width="*"/>
        </Grid.ColumnDefinitions>
        <Grid.RowDefinitions>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <TextBlock Grid.Row="0" Grid.Column="0" Text="Render scale:" Margin="0,2"/>
        <ComboBox Grid.Row="0" Grid.Column="1" x:Name="RenderScale">
          <ComboBoxItem Content="1x  - 1280 x 720 (original)"/>
          <ComboBoxItem Content="2x  - 2560 x 1440"/>
          <ComboBoxItem Content="3x  - 3840 x 2160 (4K)"/>
          <ComboBoxItem Content="4x  - 5120 x 2880 (heavy)"/>
          <ComboBoxItem Content="5x  - 6400 x 3600 (very heavy)"/>
          <ComboBoxItem Content="6x  - 7680 x 4320 (8K, extreme)"/>
          <ComboBoxItem Content="7x  - 8960 x 5040 (maximum)"/>
        </ComboBox>

        <TextBlock Grid.Row="1" Grid.Column="0" Text="Internal resolution:" Margin="0,2"/>
        <TextBlock Grid.Row="1" Grid.Column="1" x:Name="EffectiveDraw" Text="-" Foreground="#9CDCFE" Margin="0,2"/>

        <TextBlock Grid.Row="2" Grid.Column="0" Grid.ColumnSpan="2"
                   TextWrapping="Wrap" Margin="0,6,0,0" Foreground="#A0A0A0"
                   Text="The game always draws a 1280x720 frame; this multiplies the resolution it is rendered at. It is the only setting that makes the image sharper - the window size only stretches the result. If the GPU cannot handle a scale, the game lowers it automatically."/>

        <TextBlock Grid.Row="3" Grid.Column="0" Text="Renderer:" Margin="0,10,0,2"/>
        <ComboBox Grid.Row="3" Grid.Column="1" x:Name="Renderer" Margin="0,8,0,0">
          <ComboBoxItem Content="Direct3D 12 (default)"/>
          <ComboBoxItem Content="Vulkan"/>
        </ComboBox>
        <TextBlock Grid.Row="4" Grid.Column="0" Grid.ColumnSpan="2"
                   TextWrapping="Wrap" Margin="0,6,0,0" Foreground="#A0A0A0"
                   Text="Try Vulkan if the game runs slowly or does not start with Direct3D 12. If Vulkan cannot start, the game uses Direct3D 12. Shader presets need Direct3D 12."/>
      </Grid>
      </StackPanel>
    </GroupBox>

    <!-- Window -->
    <GroupBox Grid.Row="1" Header="Display and camera">
      <Grid>
        <Grid.ColumnDefinitions>
          <ColumnDefinition Width="150"/>
          <ColumnDefinition Width="*"/>
        </Grid.ColumnDefinitions>
        <Grid.RowDefinitions>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <TextBlock Grid.Row="0" Grid.Column="0" Text="Output resolution:" Margin="0,2"/>
        <StackPanel Grid.Row="0" Grid.Column="1">
          <ComboBox x:Name="OutputResolution" Margin="0,0,0,5">
            <ComboBoxItem Content="Custom" Tag="custom"/>
            <ComboBoxItem Content="1280 x 720 (720p)" Tag="1280x720"/>
            <ComboBoxItem Content="1920 x 1080 (1080p)" Tag="1920x1080"/>
            <ComboBoxItem Content="2560 x 1440 (1440p)" Tag="2560x1440"/>
            <ComboBoxItem Content="3840 x 2160 (4K)" Tag="3840x2160"/>
            <ComboBoxItem Content="2560 x 1080 (ultrawide)" Tag="2560x1080"/>
            <ComboBoxItem Content="3440 x 1440 (ultrawide)" Tag="3440x1440"/>
            <ComboBoxItem Content="5120 x 1440 (32:9)" Tag="5120x1440"/>
          </ComboBox>
          <StackPanel Orientation="Horizontal">
          <TextBox x:Name="WinW" Width="80" Text="1280"/>
          <TextBlock Text=" x " Margin="6,0"/>
          <TextBox x:Name="WinH" Width="80" Text="720"/>
          <Button x:Name="MatchWindow" Content="Match screen" Margin="10,0,0,0" Padding="8,2"/>
          </StackPanel>
        </StackPanel>

        <TextBlock Grid.Row="1" Text="Display mode:"/>
        <ComboBox Grid.Row="1" Grid.Column="1" x:Name="DisplayMode">
          <ComboBoxItem Content="Windowed"/><ComboBoxItem Content="Borderless fullscreen"/>
        </ComboBox>
        <TextBlock Grid.Row="2" Text="Monitor:"/>
        <ComboBox Grid.Row="2" Grid.Column="1" x:Name="Monitor"/>
        <CheckBox Grid.Row="3" Grid.ColumnSpan="2" x:Name="Widescreen" Content="Expand 3D view to window (experimental ultrawide)"/>
        <TextBlock Grid.Row="4" Grid.ColumnSpan="2" TextWrapping="Wrap" Foreground="#A0A0A0" Margin="0,4,0,6"
          Text="Expands gameplay horizontally and corrects HUD proportions. Titles, movies and scripted camera classes keep original framing. HUD position and size are in F6 > Display. Ultrawide remains experimental."/>
        <TextBlock Grid.Row="5" Text="Camera FOV (degrees):"/>
        <TextBox Grid.Row="5" Grid.Column="1" x:Name="CameraFov"/>
        <TextBlock Grid.Row="6" Text="Follow distance:"/>
        <TextBox Grid.Row="6" Grid.Column="1" x:Name="CameraDistance"/>
        <TextBlock Grid.Row="7" Text="F6 menu / overlay size:"/>
        <ComboBox Grid.Row="7" Grid.Column="1" x:Name="MenuScale" IsEditable="True"/>
        <TextBlock Grid.Row="8" Grid.ColumnSpan="2" TextWrapping="Wrap" Foreground="#A0A0A0" Margin="0,4,0,0"
          Text="Camera defaults: 48 degrees, 1.0x distance. Menu scale: 0.75 to 2.0. Borderless uses the selected monitor's desktop resolution. Window dimensions apply to windowed mode."/>
      </Grid>
    </GroupBox>

    <!-- Game speed (console refresh rate) -->
    <GroupBox Grid.Row="2" Header="Game speed and frame rate">
      <Grid>
        <Grid.ColumnDefinitions>
          <ColumnDefinition Width="150"/>
          <ColumnDefinition Width="*"/>
        </Grid.ColumnDefinitions>
        <Grid.RowDefinitions>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
          <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <TextBlock Grid.Row="0" Grid.Column="0" Text="Console refresh (Hz):" Margin="0,2"/>
        <ComboBox Grid.Row="0" Grid.Column="1" x:Name="Refresh"/>

        <CheckBox Grid.Row="1" Grid.Column="1" x:Name="VSync" Content="Vertical sync"/>
        <CheckBox Grid.Row="2" Grid.Column="1" x:Name="AllowTearing"
                  Content="Allow tearing / variable refresh rate"/>

        <TextBlock Grid.Row="3" Grid.Column="0" Grid.ColumnSpan="2"
                   TextWrapping="Wrap" Margin="0,6,0,0" Foreground="#A0A0A0"
                   Text="Tenchu Z runs at 30 frames per second, as on the Xbox 360: it draws a new frame on every second refresh of the console. This setting is the console's refresh rate, which is also the game's clock, so it changes game SPEED, not smoothness: 60 Hz is normal speed (30 fps), 120 Hz is double speed. Keep 60 Hz and vertical sync on. The in-game counter (F6 > HUD > Frame rate) shows the real fps; the window is redrawn more often, but those redraws repeat the same frame."/>

        <TextBlock Grid.Row="4" Grid.Column="0" Grid.ColumnSpan="2"
                   x:Name="SpeedNote" Margin="0,6,0,0" FontWeight="Bold"/>

        <CheckBox Grid.Row="5" Grid.Column="0" Grid.ColumnSpan="2" x:Name="Fps60" Margin="0,8,0,0"
                  Content="60 fps while playing (experimental; menus stay at 30)"/>
      </Grid>
    </GroupBox>

    <!-- Gameplay & mods -->
    <GroupBox Grid.Row="3" Header="Cheats &amp; mods">
      <StackPanel>
        <CheckBox x:Name="GodMode" Content="God mode (no damage; F8 toggles in game)"/>
        <CheckBox x:Name="FreeShop" Content="Free shop purchases"/>
        <CheckBox x:Name="SkipIntro" Content="Skip intro movies (logos and opening)"/>
        <CheckBox x:Name="MouseLook" Content="Mouse look (keyboard &amp; mouse players)"/>
        <CheckBox x:Name="MouseDirect" Content="Direct mouse camera (turns by exactly the distance moved)" Margin="0,2,0,0"/>
        <StackPanel Orientation="Horizontal" Margin="0,2,0,0">
          <TextBlock Text="Mouse speed (0.05-20x):" Margin="0,0,8,0"/>
          <TextBox x:Name="MouseSpeed" Width="85" Text="1.0"/>
        </StackPanel>
        <StackPanel Orientation="Horizontal" Margin="0,2,0,6">
          <TextBlock Text="Mouse as a stick, sensitivity (0.01-100x):" Margin="0,0,8,0"/>
          <TextBox x:Name="MouseSensitivity" Width="85" Text="1.0"/>
        </StackPanel>
        <CheckBox x:Name="DumpShaders" Content="Dump shaders for shader modding (to shader_dump\ next to the game)"/>
        <StackPanel Orientation="Horizontal">
          <CheckBox x:Name="ModsEnabled" Content="Load mods"/>
          <Button x:Name="OpenMods" Content="Open mods folder" Margin="12,0,0,0" Padding="8,2"/>
          <TextBlock x:Name="ModCount" Margin="10,0,0,0" Foreground="#A0A0A0"/>
        </StackPanel>
        <Grid Margin="0,6,0,0">
          <Grid.ColumnDefinitions>
            <ColumnDefinition Width="*"/>
            <ColumnDefinition Width="Auto"/>
          </Grid.ColumnDefinitions>
          <ListBox Grid.Column="0" x:Name="ModList" Height="110"
                   Background="#2D2D30" Foreground="#E8E8E8" BorderBrush="#3F3F46"/>
          <StackPanel Grid.Column="1" Margin="6,0,0,0">
            <Button x:Name="ModUp" Content="Up" Padding="8,2" Margin="0,0,0,4"/>
            <Button x:Name="ModDown" Content="Down" Padding="8,2" Margin="0,0,0,4"/>
            <Button x:Name="ModRefresh" Content="Refresh" Padding="8,2"/>
          </StackPanel>
        </Grid>
        <TextBlock Foreground="#A0A0A0" TextWrapping="Wrap" Margin="0,2,0,0"
                   Text="Tick to enable. Mods lower in the list win when two replace the same file. Changes are applied (folders renamed) when you save or launch."/>
        <TextBlock TextWrapping="Wrap" Margin="0,6,0,0" Foreground="#A0A0A0"
                   Text="In game: F6 menu, F8 god mode, F5 refill health, F9 pause mouse look, F10 photo mode, F1/F2 save/load position. A mod is a folder inside mods\ that mirrors the game folder, e.g. mods\MyMod\DATA\TEXTURE\ENGLISH\title.xpr."/>
      </StackPanel>
    </GroupBox>

    <!-- Actions -->
    <WrapPanel Grid.Row="4" HorizontalAlignment="Right" Margin="0,0,0,8">
      <Button x:Name="SafeLaunch" Content="Safe launch" Padding="14,5" Margin="0,0,8,0"
        ToolTip="Starts once in a 1280x720 window with Direct3D 12, 1x scale, mods/effects off. Does not save settings or change your frame rate."/>
      <Button x:Name="SaveOnly" Content="Save config" Padding="14,5" Margin="0,0,8,0"/>
      <Button x:Name="Launch" Content="Save &amp; Launch" Padding="14,5" FontWeight="Bold"/>
    </WrapPanel>

    <TextBlock Grid.Row="5" x:Name="Status" Foreground="#6A9955" TextWrapping="Wrap" Margin="0,0,0,4"/>
    <TextBlock Grid.Row="6" x:Name="PathInfo" Foreground="#808080" FontSize="11" TextWrapping="Wrap"/>
  </Grid>
  </ScrollViewer>
</Window>
'@

$reader = New-Object System.Xml.XmlNodeReader $xaml
$window = [Windows.Markup.XamlReader]::Load($reader)

# Grab named controls.
$ui = @{}
foreach ($name in @('RenderScale','EffectiveDraw',
                    'WinW','WinH','OutputResolution','MatchWindow','DisplayMode','Refresh','VSync',
                    'AllowTearing','SaveOnly','Launch','Status','PathInfo','SpeedNote',
                    'GodMode','FreeShop','ModsEnabled','OpenMods','ModCount',
                    'SkipIntro','MouseLook','MouseDirect','MouseSpeed','MouseSensitivity',
                    'ModList','ModUp','ModDown','ModRefresh',
                    'DumpShaders','Renderer','Fps60','GraphicsProfile','SaveProfile','LoadProfile',
                    'Monitor','Widescreen','MenuScale','CameraFov','CameraDistance',
                    'SafeLaunch','ContentScroll','ContentScale')) {
    $ui[$name] = $window.FindName($name)
}

# WPF already follows monitor DPI. This additional uniform layout scale follows
# window width; the scroll viewer keeps every control reachable at short heights.
$window.MaxHeight = [System.Windows.SystemParameters]::WorkArea.Height
$window.Height = [math]::Min(900, $window.MaxHeight * 0.92)
$ui.ContentScroll.Add_SizeChanged({
    $factor = [math]::Max(0.75, [math]::Min(1.8, ($ui.ContentScroll.ActualWidth - 20) / 558.0))
    $ui.ContentScale.ScaleX = $factor
    $ui.ContentScale.ScaleY = $factor
})
foreach ($scale in @('0.75', '1.0', '1.25', '1.5', '1.75', '2.0')) { [void]$ui.MenuScale.Items.Add($scale) }
Add-Type -AssemblyName System.Windows.Forms
$script:Screens = @([System.Windows.Forms.Screen]::AllScreens)
[void]$ui.Monitor.Items.Add([pscustomobject]@{ Label='System default'; Device='' })
foreach ($screen in $script:Screens) {
    [void]$ui.Monitor.Items.Add([pscustomobject]@{
        Label=('{0}  {1} x {2}{3}' -f $screen.DeviceName, $screen.Bounds.Width,
            $screen.Bounds.Height, $(if ($screen.Primary) { ' (primary)' } else { '' }))
        Device=$screen.DeviceName
    })
}
$ui.Monitor.DisplayMemberPath = 'Label'

# Populate refresh rates.
foreach ($hz in $script:RefreshPresets) { [void]$ui.Refresh.Items.Add($hz) }
$ui.Refresh.SelectedIndex = 2     # 60 Hz

$script:Initialising = $true

# ---------------------------------------------------------------------------
# Load current config into the UI
# ---------------------------------------------------------------------------

function Load-CurrentConfig {
    $vals = @{}
    if (Test-Path -LiteralPath $script:Config) {
        foreach ($line in (Get-Content -LiteralPath $script:Config)) {
            if ($line -match '^\s*\[') { break }   # runtime cvars live at the root only
            $m = [regex]::Match($line, '^\s*([A-Za-z0-9_]+)\s*=\s*(.+?)\s*(#.*)?$')
            if ($m.Success) { $vals[$m.Groups[1].Value] = $m.Groups[2].Value }
        }
    }

    # Render scale
    $scale = 1
    if ($vals.ContainsKey('resolution_scale')) {
        $scale = [int]($vals['resolution_scale'] -replace '[^0-9]', '')
    } elseif ($vals.ContainsKey('draw_resolution_scale_x')) {
        $scale = [int]($vals['draw_resolution_scale_x'] -replace '[^0-9]', '')
    }
    $idx = [array]::IndexOf($script:RenderScales, [math]::Min([math]::Max($scale, 1), 7))
    $ui.RenderScale.SelectedIndex = [math]::Max($idx, 0)

    $ui.Fps60.IsChecked = ($vals['frame_rate_60'] -eq 'true')

    # Renderer
    $ui.Renderer.SelectedIndex = $(if ($vals['gpu_backend'] -match 'vulkan') { 1 } else { 0 })

    # Window
    if ($vals.ContainsKey('window_width'))  { $ui.WinW.Text = ($vals['window_width']  -replace '[^0-9]','') }
    if ($vals.ContainsKey('window_height')) { $ui.WinH.Text = ($vals['window_height'] -replace '[^0-9]','') }
    Update-OutputPreset

    # Fullscreen / vsync / tearing
    $ui.DisplayMode.SelectedIndex = $(if ($vals['fullscreen'] -eq 'true') { 1 } else { 0 })
    $ui.Widescreen.IsChecked = ($vals['widescreen'] -eq 'true')
    $ui.CameraFov.Text = $(if ($vals.ContainsKey('camera_fov')) { $vals['camera_fov'] } else { '48.0' })
    $ui.CameraDistance.Text = $(if ($vals.ContainsKey('camera_distance')) { $vals['camera_distance'] } else { '1.0' })
    $ui.MenuScale.Text = $(if ($vals.ContainsKey('menu_ui_scale')) { $vals['menu_ui_scale'] } else { '1.0' })
    $ui.Monitor.SelectedIndex = 0
    if ($vals.ContainsKey('display_device')) {
        $device = Convert-RootValue $vals['display_device']
        for ($i = 0; $i -lt $ui.Monitor.Items.Count; $i++) {
            if ($ui.Monitor.Items[$i].Device -eq $device) { $ui.Monitor.SelectedIndex = $i; break }
        }
        if ($device -and $ui.Monitor.SelectedIndex -eq 0) {
            [void]$ui.Monitor.Items.Add([pscustomobject]@{ Label="$device (disconnected; uses default)"; Device=$device })
            $ui.Monitor.SelectedIndex = $ui.Monitor.Items.Count - 1
        }
    }
    $ui.VSync.IsChecked        = ($vals['vsync'] -ne 'false')   # SDK default is true
    $ui.AllowTearing.IsChecked = ($vals['d3d12_allow_variable_refresh_rate_and_tearing'] -ne 'false')

    # Cheats & mods (defaults: off, off, on)
    $ui.GodMode.IsChecked     = ($vals['god_mode'] -eq 'true')
    $ui.FreeShop.IsChecked    = ($vals['free_shopping'] -eq 'true')
    $ui.ModsEnabled.IsChecked = ($vals['mods_enabled'] -ne 'false')
    $ui.SkipIntro.IsChecked   = ($vals['skip_intro_movies'] -eq 'true')
    $ui.MouseLook.IsChecked   = ($vals['mnk_mouse'] -eq 'true')
    $ui.MouseDirect.IsChecked = ($vals['camera_mouse_direct'] -ne 'false')
    $ui.MouseSpeed.Text       = $(if ($vals.ContainsKey('camera_mouse_speed')) { $vals['camera_mouse_speed'] } else { '1.0' })
    $ui.MouseSensitivity.Text = $(if ($vals.ContainsKey('mnk_sensitivity')) { $vals['mnk_sensitivity'] } else { '1.0' })
    $ui.DumpShaders.IsChecked = ($vals.ContainsKey('dump_shaders') -and $vals['dump_shaders'] -notmatch '^""?$')

    # Refresh
    if ($vals.ContainsKey('video_mode_refresh_rate')) {
        $hz = [double]($vals['video_mode_refresh_rate'] -replace '[^0-9\.]', '')
        $txt = [string][int][math]::Round($hz)
        if ($ui.Refresh.Items.Contains($txt)) { $ui.Refresh.SelectedItem = $txt }
        else { [void]$ui.Refresh.Items.Add($txt); $ui.Refresh.SelectedItem = $txt }
    }
}

# ---------------------------------------------------------------------------
# UI -> config values
# ---------------------------------------------------------------------------

function Get-RenderScale {
    $i = $ui.RenderScale.SelectedIndex
    if ($i -lt 0) { return 1 }
    return $script:RenderScales[$i]
}

function Update-EffectiveDraw {
    $s = Get-RenderScale
    $ui.EffectiveDraw.Text = "$($script:GameFrame.w * $s) x $($script:GameFrame.h * $s)"
    Update-SpeedNote
}

function Update-OutputPreset {
    $size = $ui.WinW.Text.Trim() + 'x' + $ui.WinH.Text.Trim()
    $script:SyncingOutput = $true
    try {
        $ui.OutputResolution.SelectedIndex = 0
        for ($i=1; $i -lt $ui.OutputResolution.Items.Count; $i++) {
            if ([string]$ui.OutputResolution.Items[$i].Tag -eq $size) {
                $ui.OutputResolution.SelectedIndex=$i
                break
            }
        }
    } finally { $script:SyncingOutput = $false }
}

<#
    Report the implied game speed.

    The runtime generates the guest vblank from video_mode_refresh_rate when
    vsync is on, and from a fixed 1000 Hz timer when vsync is off
    (src/graphics/graphics_system.cpp). Tenchu Z runs its simulation off vblank,
    so the vblank rate IS the game speed. 60 Hz is the console's normal rate.
#>
function Update-SpeedNote {
    if (-not $ui.SpeedNote) { return }
    $vsyncOn = [bool]$ui.VSync.IsChecked
    $hz = 60.0
    [void][double]::TryParse([string]$ui.Refresh.SelectedItem, [ref]$hz)

    if (-not $vsyncOn) {
        $ui.SpeedNote.Foreground = '#F48771'
        $ui.SpeedNote.Text = 'Vertical sync is OFF: the runtime uses a 1000 Hz timer, so the game will run far too fast. Turn vertical sync on for playable speed.'
        return
    }

    $mult = $hz / 60.0
    $desc = if ([math]::Abs($mult - 1.0) -lt 0.01) { 'normal speed' }
            elseif ($mult -lt 1.0) { '{0:0.##}x speed (slower)' -f $mult }
            else { '{0:0.##}x speed (faster)' -f $mult }

    if ([math]::Abs($mult - 1.0) -lt 0.01) {
        $ui.SpeedNote.Foreground = '#6A9955'
    } else {
        $ui.SpeedNote.Foreground = '#DCDCAA'
    }
    $fps = [math]::Round($hz / 2)
    $ui.SpeedNote.Text = "$hz Hz = $desc, $fps fps   (60 Hz = normal speed, 30 fps, as on the Xbox 360)"
}

function Build-Values {
    $s   = Get-RenderScale

    $winW = 0; $winH = 0
    [void][int]::TryParse($ui.WinW.Text.Trim(), [ref]$winW)
    [void][int]::TryParse($ui.WinH.Text.Trim(), [ref]$winH)

    $hz = 60.0
    [void][double]::TryParse([string]$ui.Refresh.SelectedItem, [ref]$hz)

    $vals = @{
        resolution_scale          = $s
        video_mode_width          = [int]$script:GameFrame.w
        video_mode_height         = [int]$script:GameFrame.h
        video_mode_refresh_rate   = $hz
        fullscreen                = ($ui.DisplayMode.SelectedIndex -eq 1)
        vsync                     = [bool]$ui.VSync.IsChecked
        d3d12_allow_variable_refresh_rate_and_tearing = [bool]$ui.AllowTearing.IsChecked
        god_mode                  = [bool]$ui.GodMode.IsChecked
        free_shopping             = [bool]$ui.FreeShop.IsChecked
        mods_enabled              = [bool]$ui.ModsEnabled.IsChecked
        skip_intro_movies         = [bool]$ui.SkipIntro.IsChecked
        mnk_mouse                 = [bool]$ui.MouseLook.IsChecked
        mnk_sensitivity           = Read-Number $ui.MouseSensitivity.Text 0.01 100 'Mouse sensitivity'
        camera_mouse_direct       = [bool]$ui.MouseDirect.IsChecked
        camera_mouse_speed        = Read-Number $ui.MouseSpeed.Text 0.05 20 'Mouse speed'
        # A path turns dumping on; $null removes the key (runtime default: off).
        dump_shaders              = $(if ($ui.DumpShaders.IsChecked) { Join-Path $script:ExeDir 'shader_dump' } else { $null })
        gpu_backend               = $(if ($ui.Renderer.SelectedIndex -eq 1) { 'vulkan' } else { 'd3d12' })
        frame_rate_60             = [bool]$ui.Fps60.IsChecked
        display_device            = [string]$ui.Monitor.SelectedItem.Device
        widescreen                = [bool]$ui.Widescreen.IsChecked
        present_letterbox         = (-not [bool]$ui.Widescreen.IsChecked)
        camera_fov                = Read-Number $ui.CameraFov.Text 30 100 'Camera FOV'
        camera_distance           = Read-Number $ui.CameraDistance.Text 0.5 2.5 'Camera distance'
        menu_ui_scale             = Read-Number $ui.MenuScale.Text 0.75 2.0 'Menu scale'
    }
    # These have no launcher editor. Preserve the selections made in F6.
    $root = Read-RootValues $script:Config
    foreach ($key in @('shader_preset', 'shader_effects', 'shader_overrides_enabled',
                      'widescreen_hud_correct', 'game_hud_scale', 'game_hud_offset_x', 'game_hud_offset_y')) {
        if ($root.ContainsKey($key)) { $vals[$key] = Convert-RootValue $root[$key] }
    }
    foreach ($entry in @{widescreen_hud_correct=$true; game_hud_scale=1.0; game_hud_offset_x=0.0; game_hud_offset_y=0.0}.GetEnumerator()) {
        if (-not $vals.ContainsKey($entry.Key)) { $vals[$entry.Key] = $entry.Value }
    }

    # Only pin an explicit window size when the user asked for one. Leaving
    # window_width/height out lets the SDK follow the guest video mode.
    if ($winW -gt 0 -and $winH -gt 0) {
        $vals.window_width  = [int]$winW
        $vals.window_height = [int]$winH
    }
    return $vals
}

function Read-Number([string]$Text, [double]$Min, [double]$Max, [string]$Label) {
    $value = 0.0
    if (-not [double]::TryParse($Text.Trim(), [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture, [ref]$value) -or
            [double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt $Min -or $value -gt $Max) {
        throw "$Label must be between $Min and $Max (use a decimal point)."
    }
    return $value
}

function Read-RootValues([string]$Path) {
    $values = @{}
    if (Test-Path -LiteralPath $Path) {
        foreach ($line in (Get-Content -LiteralPath $Path)) {
            if ($line -match '^\s*\[') { break }
            # Generated profile values are single-line TOML primitives.
            if ($line -match '^\s*([A-Za-z0-9_]+)\s*=\s*("(?:[^"\\]|\\.)*"|true|false|[-+0-9.eE]+)\s*(?:#.*)?$') {
                $values[$matches[1]] = $matches[2]
            }
        }
    }
    return $values
}

function Convert-RootValue([string]$Value) {
    if ($Value.StartsWith('"')) { return (ConvertFrom-Json -InputObject $Value) }
    if ($Value -eq 'true') { return $true }
    if ($Value -eq 'false') { return $false }
    if ($Value -match '^[-+]?\d+$') { return [int]::Parse($Value, [System.Globalization.CultureInfo]::InvariantCulture) }
    return Read-Number $Value (-1e10) 1e10 'Profile value'
}

function Test-ProfileValues([hashtable]$Values) {
    $ranges = @{
        resolution_scale=@(1,7); window_width=@(0,8192); window_height=@(0,8192)
        camera_fov=@(30,100); camera_distance=@(0.5,2.5); menu_ui_scale=@(0.75,2.0)
        game_hud_scale=@(0.5,1.5); game_hud_offset_x=@(-0.4,0.4); game_hud_offset_y=@(-0.4,0.4)
    }
    foreach ($key in $Values.Keys) {
        $value = $Values[$key]
        if ($ranges.ContainsKey($key)) {
            if ($value -is [bool] -or $value -is [string]) { throw "Invalid numeric profile value: $key" }
            [void](Read-Number ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, '{0}', $value)) $ranges[$key][0] $ranges[$key][1] $key)
            if ($key -in @('resolution_scale','window_width','window_height') -and [double]$value -ne [math]::Truncate([double]$value)) {
                throw "$key must be a whole number."
            }
        } elseif ($key -in @('gpu_backend','display_device','shader_preset')) {
            if ($value -isnot [string]) { throw "Invalid text profile value: $key" }
        } elseif ($value -isnot [bool]) { throw "Invalid checkbox profile value: $key" }
    }
    if ($Values.ContainsKey('gpu_backend') -and $Values.gpu_backend -notin @('d3d12','vulkan')) {
        throw 'Profile renderer must be d3d12 or vulkan.'
    }
}

function Get-ProfilePath {
    $name = $ui.GraphicsProfile.Text.Trim()
    if ($name -notmatch '^[A-Za-z0-9_-]{1,48}$' -or $name -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
        throw 'Profile name: use 1-48 letters, numbers, - or _ (no Windows reserved names).'
    }
    return Join-Path $script:ProfilesDir ($name + '.toml')
}

function Refresh-Profiles {
    $text = $ui.GraphicsProfile.Text
    $ui.GraphicsProfile.Items.Clear()
    if (Test-Path -LiteralPath $script:ProfilesDir) {
        foreach ($file in (Get-ChildItem -LiteralPath $script:ProfilesDir -Filter '*.toml' -File | Sort-Object Name)) {
            if ($file.BaseName -match '^[A-Za-z0-9_-]{1,48}$') { [void]$ui.GraphicsProfile.Items.Add($file.BaseName) }
        }
    }
    $ui.GraphicsProfile.Text = $text
}

$ui.SaveProfile.Add_Click({
    try {
        $path = Get-ProfilePath
        $current = Build-Values
        # Supply defaults for F6 settings absent from a fresh config.
        if (-not $current.ContainsKey('shader_preset')) { $current.shader_preset = 'Modern Look' }
        if (-not $current.ContainsKey('shader_effects')) { $current.shader_effects = $true }
        if (-not $current.ContainsKey('shader_overrides_enabled')) { $current.shader_overrides_enabled = $true }
        New-Item -ItemType Directory -Path $script:ProfilesDir -Force | Out-Null
        Write-Config -Values $current -Path $path -Keys $script:ProfileKeys
        Refresh-Profiles
        $ui.Status.Text = 'Graphics profile saved. Frame rate settings are excluded.'
    } catch { $ui.Status.Text = "ERROR: $($_.Exception.Message)" }
})
$ui.LoadProfile.Add_Click({
    try {
        $path = Get-ProfilePath
        if (-not (Test-Path -LiteralPath $path)) { throw 'Choose an existing graphics profile.' }
        if (Get-Process tenchuz -ErrorAction SilentlyContinue) { throw 'Close the game before loading a profile, or load it in F6 > Display.' }
        $raw = Read-RootValues $path
        $current = Build-Values
        $count = 0
        $profileValues = @{}
        foreach ($key in $script:ProfileKeys) {
            if ($raw.ContainsKey($key)) { $profileValues[$key] = Convert-RootValue $raw[$key]; $count++ }
        }
        if (-not $count) { throw 'This profile has no supported graphics settings.' }
        Test-ProfileValues $profileValues
        foreach ($key in $profileValues.Keys) { $current[$key] = $profileValues[$key] }
        Write-Config -Values $current
        Load-CurrentConfig
        Update-EffectiveDraw
        $ui.Status.Text = 'Graphics profile loaded. Frame rate settings are unchanged.'
    } catch { $ui.Status.Text = "ERROR: $($_.Exception.Message)" }
})
$ui.SafeLaunch.Add_Click({
    try {
        $p = Start-Game -Recovery
        if ($null -ne $p) { $window.Close() }
    } catch { $ui.Status.Text = "ERROR: $($_.Exception.Message)" }
})

# ---------------------------------------------------------------------------
# Wire up events
# ---------------------------------------------------------------------------

$ui.RenderScale.Add_SelectionChanged({ Update-EffectiveDraw })
$script:SyncingOutput = $false
$ui.OutputResolution.Add_SelectionChanged({
    if ($script:SyncingOutput -or $null -eq $ui.OutputResolution.SelectedItem) { return }
    $tag = [string]$ui.OutputResolution.SelectedItem.Tag
    if ($tag -match '^(\d+)x(\d+)$') {
        $script:SyncingOutput = $true
        try { $ui.WinW.Text=$matches[1]; $ui.WinH.Text=$matches[2] }
        finally { $script:SyncingOutput=$false }
        if ($tag -eq '1920x1080' -and (Get-RenderScale) -lt 2) {
            $ui.RenderScale.SelectedIndex=1
            $ui.Status.Text='1080p output selected with 2x internal rendering (1440p downsampled to 1080p).'
        }
    }
})
$ui.WinW.Add_TextChanged({ if (-not $script:SyncingOutput) { Update-OutputPreset } })
$ui.WinH.Add_TextChanged({ if (-not $script:SyncingOutput) { Update-OutputPreset } })
$ui.VSync.Add_Click({ Update-SpeedNote })
$ui.Refresh.Add_SelectionChanged({ Update-SpeedNote })
# ---------------------------------------------------------------------------
# Mod manager
#
# The game applies mods in alphabetical order of folder name (later wins) and
# skips folders starting with '_' (tenchuz-recomp/src/mod_loader.h). The list
# shows mods in that order; saving renames the folders to
# [_]NN_Name so the folder names carry exactly what the list shows. Nothing is
# renamed unless the list was changed.
# ---------------------------------------------------------------------------

$script:ModsDirty = $false

function Get-ModBaseName([string]$folder) {
    return ($folder -replace '^_+', '') -replace '^\d{2,3}_', ''
}

function Load-ModList {
    $ui.ModList.Items.Clear()
    $script:ModsDirty = $false
    if (-not (Test-Path -LiteralPath $script:ModsDir)) { return }
    # Same order the game uses: the folder name as it is, enabled or not,
    # compared after removing the disable prefix.
    $dirs = Get-ChildItem -LiteralPath $script:ModsDir -Directory |
            Where-Object { $_.Name -notmatch '^\.' } |
            Sort-Object { $_.Name -replace '^_+', '' }
    foreach ($d in $dirs) {
        $cb = New-Object System.Windows.Controls.CheckBox
        $cb.Content = Get-ModBaseName $d.Name
        $cb.IsChecked = -not $d.Name.StartsWith('_')
        $cb.Tag = $d.Name
        $cb.Foreground = '#E8E8E8'
        $cb.Add_Click({ $script:ModsDirty = $true })
        [void]$ui.ModList.Items.Add($cb)
    }
}

function Move-SelectedMod([int]$delta) {
    $i = $ui.ModList.SelectedIndex
    $j = $i + $delta
    if ($i -lt 0 -or $j -lt 0 -or $j -ge $ui.ModList.Items.Count) { return }
    $item = $ui.ModList.Items[$i]
    $ui.ModList.Items.RemoveAt($i)
    $ui.ModList.Items.Insert($j, $item)
    $ui.ModList.SelectedIndex = $j
    $script:ModsDirty = $true
}

function Save-ModList {
    if (-not $script:ModsDirty) { return }
    if (Get-Process tenchuz -ErrorAction SilentlyContinue) {
        throw 'Close the game before changing mods (its files are in use).'
    }
    $plan = @()
    $n = 1
    foreach ($cb in $ui.ModList.Items) {
        $prefix = if ($cb.IsChecked) { '' } else { '_' }
        $target = '{0}{1:D2}_{2}' -f $prefix, $n, $cb.Content
        $plan += [pscustomobject]@{ From = $cb.Tag; To = $target }
        $n++
    }
    # Two passes through temporary names, so swapping two mods' positions
    # never collides with a folder that has not been renamed yet.
    foreach ($m in $plan | Where-Object { $_.From -ne $_.To }) {
        Rename-Item -LiteralPath (Join-Path $script:ModsDir $m.From) -NewName ("~tmp~" + $m.To)
    }
    foreach ($m in $plan | Where-Object { $_.From -ne $_.To }) {
        Rename-Item -LiteralPath (Join-Path $script:ModsDir ("~tmp~" + $m.To)) -NewName $m.To
    }
    Load-ModList
}

$ui.ModUp.Add_Click({ Move-SelectedMod -1 })
$ui.ModDown.Add_Click({ Move-SelectedMod 1 })
$ui.ModRefresh.Add_Click({ Load-ModList; Update-ModCount })

function Update-ModCount {
    $n = 0
    if (Test-Path -LiteralPath $script:ModsDir) {
        $n = @(Get-ChildItem -LiteralPath $script:ModsDir -Directory |
               Where-Object { $_.Name -notmatch '^[_.]' }).Count
    }
    $ui.ModCount.Text = "$n mod(s) installed"
}

$ui.OpenMods.Add_Click({
    if (-not (Test-Path -LiteralPath $script:ModsDir)) {
        New-Item -ItemType Directory -Path $script:ModsDir -Force | Out-Null
    }
    Start-Process explorer.exe -ArgumentList "`"$($script:ModsDir)`""
    Update-ModCount
    Load-ModList
})

$ui.MatchWindow.Add_Click({
    $device = $ui.Monitor.SelectedItem.Device
    $screen = $script:Screens | Where-Object { $_.DeviceName -eq $device } | Select-Object -First 1
    if (-not $screen) { $screen = [System.Windows.Forms.Screen]::PrimaryScreen }
    $ui.WinW.Text = [string]$screen.Bounds.Width
    $ui.WinH.Text = [string]$screen.Bounds.Height
})

$ui.SaveOnly.Add_Click({
    try {
        Save-ModList
        Write-Config -Values (Build-Values)
        $ui.Status.Text = "Configuration saved to $($script:Config)"
    } catch {
        $ui.Status.Text = "ERROR: $($_.Exception.Message)"
    }
})

$ui.Launch.Add_Click({
    try {
        Save-ModList
        Write-Config -Values (Build-Values)
        $p = Start-Game
        if ($null -eq $p) {
            $ui.Status.Text = 'Configuration saved. The running game was left open.'
            return
        }
        $window.Close()
    } catch {
        $ui.Status.Text = "ERROR: $($_.Exception.Message)"
    }
})

# ---------------------------------------------------------------------------
# Headless mode
# ---------------------------------------------------------------------------

if ($Headless -or $Preset) {
    Load-CurrentConfig
    if ($Preset) {
        # Preset syntax: <n>x, e.g. 3x (an older "<output>-<n>x" form such as
        # 720p-3x is also accepted; the output part is ignored).
        if ($Preset -match '(\d)x$') {
            $idx = [array]::IndexOf($script:RenderScales, [int]$matches[1])
            if ($idx -ge 0) { $ui.RenderScale.SelectedIndex = $idx }
        }
    }
    Write-Config -Values (Build-Values)
    Write-Host "Wrote $($script:Config)"
    return
}

# ---------------------------------------------------------------------------
# Show
# ---------------------------------------------------------------------------

Load-CurrentConfig
Refresh-Profiles
Update-ModCount
Load-ModList
$ui.PathInfo.Text = "Exe: $($script:Exe)`nConfig: $($script:Config)`nGame files: $($script:DataRoot)"
Update-EffectiveDraw
$script:Initialising = $false

[void]$window.ShowDialog()
