# Focus + input probe. Launches the game, waits for the title screen, then tries
# to focus the window and deliver Start (Return/X) via SendInput and window
# messages, capturing the screen after each attempt.
# Usage: input_probe.ps1 -Tag in1 -Wait 50 -Method both
param(
  [string]$Tag = "inprobe",
  [int]$Wait = 50,
  [string]$Method = "both"
)

$ErrorActionPreference = "Continue"
$root = "C:\Emulation\360\recomps\tenchu z"
$exeDir = "$root\tenchuz-recomp\out\build\win-amd64-release"
$gameDir = "$root\extracted"
$log = "$root\logs\runtime_$Tag.log"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class IP {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool alt);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  public static System.Collections.Generic.List<IntPtr> Found = new System.Collections.Generic.List<IntPtr>();
  public static uint TargetPid = 0;
  public static bool Cb(IntPtr h, IntPtr l) {
    uint pid; GetWindowThreadProcessId(h, out pid);
    if (pid == TargetPid && IsWindowVisible(h)) Found.Add(h);
    return true;
  }
  public static string ForceFocus(IntPtr hwnd) {
    ShowWindow(hwnd, 9);  // SW_RESTORE
    IntPtr fg = GetForegroundWindow();
    uint fgPid; uint fgThread = GetWindowThreadProcessId(fg, out fgPid);
    uint me = GetCurrentThreadId();
    bool attached = AttachThreadInput(me, fgThread, true);
    BringWindowToTop(hwnd);
    bool sfw = SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    AttachThreadInput(me, fgThread, false);
    return String.Format("attach={0} setfg={1} fgBefore=0x{2:X} fgAfter=0x{3:X}", attached, sfw, fg.ToInt64(), GetForegroundWindow().ToInt64());
  }
  public static void SendKeyInput(int vk) {
    INPUT[] a = new INPUT[2];
    a[0].type = 1; a[0].ki.wVk = (ushort)vk;
    a[1].type = 1; a[1].ki.wVk = (ushort)vk; a[1].ki.dwFlags = 2;
    SendInput(2, a, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void PostKey(IntPtr h, int vk) {
    // Make SDL believe the window is active/focused, then deliver the key.
    PostMessageW(h, 0x0006, (IntPtr)1, IntPtr.Zero);   // WM_ACTIVATE, WA_ACTIVE
    PostMessageW(h, 0x0007, IntPtr.Zero, IntPtr.Zero); // WM_SETFOCUS
    System.Threading.Thread.Sleep(150);
    long down = 0x001E0001L;
    long up = unchecked((long)0xC01E0001L);
    PostMessageW(h, 0x0100, (IntPtr)vk, (IntPtr)down); // WM_KEYDOWN
    System.Threading.Thread.Sleep(150);
    PostMessageW(h, 0x0101, (IntPtr)vk, (IntPtr)up);   // WM_KEYUP
    // Also a WM_CHAR for good measure.
    PostMessageW(h, 0x0102, (IntPtr)vk, (IntPtr)down);
  }
}
"@

Remove-Item $log -Force -ErrorAction SilentlyContinue
$args = @("--game_data_root=`"$gameDir`"", "--log_level=debug", "--log_file=`"$log`"", "--mnk_mode=true")
$p = Start-Process -FilePath "$exeDir\tenchuz.exe" -ArgumentList $args `
     -WorkingDirectory $exeDir -PassThru
Write-Output "pid=$($p.Id)"
Start-Sleep -Seconds 2
[IP]::TargetPid = [uint32]$p.Id
[IP]::Found.Clear()
[IP]::EnumWindows([IP+EnumWindowsProc]{ param($h,$l) [IP]::Cb($h,$l) }, [IntPtr]::Zero) | Out-Null
$hwnd = [IP]::Found[0]
$r = New-Object IP+RECT
[IP]::GetWindowRect($hwnd, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
Write-Output ("window 0x{0:X} ${w}x${h}" -f $hwnd.ToInt64())

function Capture([string]$name) {
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [IP]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $file = "$root\logs\${Tag}_$name.png"
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $sum=0.0;$n=0
  for ($y=0;$y -lt $h;$y+=16) { for ($x=0;$x -lt $w;$x+=16) { $c=$bmp.GetPixel($x,$y); $sum += 0.299*$c.R+0.587*$c.G+0.114*$c.B; $n++ } }
  $bmp.Dispose()
  $txt = & powershell -ExecutionPolicy Bypass -File "$root\tools\ocr.ps1" $file 2>$null
  $txt = (($txt | Where-Object { $_ -notmatch "=== OCR|=== END OCR" }) -join " | ").Trim()
  Write-Output ("[{0}] PrintWindow={1} meanL={2:N1} OCR: {3}" -f $name,$ok,($sum/$n),$txt)
}

Write-Output "waiting ${Wait}s for title..."
Start-Sleep -Seconds $Wait
Capture "before"

Write-Output ("focus: " + [IP]::ForceFocus($hwnd))
Start-Sleep -Milliseconds 400
Capture "focused"

if ($Method -eq "sendinput" -or $Method -eq "both") {
  [IP]::SendKeyInput(0x0D)   # Return
  Write-Output "SendInput Return"
  Start-Sleep -Seconds 2
  Capture "sendinput_return"
  [IP]::SendKeyInput(0x58)   # X (also bound to Start)
  Write-Output "SendInput X"
  Start-Sleep -Seconds 2
  Capture "sendinput_x"
}

if ($Method -eq "postmessage" -or $Method -eq "both") {
  [IP]::PostKey($hwnd, 0x0D)
  Write-Output "PostMessage Return"
  Start-Sleep -Seconds 2
  Capture "post_return"
  [IP]::PostKey($hwnd, 0x58)
  Write-Output "PostMessage X"
  Start-Sleep -Seconds 2
  Capture "post_x"
}

# Extra: hold Return down longer via SendInput (some games need a held press).
if ($Method -eq "both") {
  [IP]::SendKeyInput(0x0D)
  Start-Sleep -Milliseconds 300
  [IP]::PostKey($hwnd, 0x20)   # Space
  Write-Output "extra Return+Space"
  Start-Sleep -Seconds 2
  Capture "extra"
}

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
