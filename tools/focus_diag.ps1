# Focus diagnostic: launch the game, then repeatedly try to take focus and report
# whether GetForegroundWindow() actually points at the game HWND afterwards.
# This isolates the "keys don't reach the game" problem from the game itself.
param([int]$Seconds = 30)
$ErrorActionPreference = "Continue"
$root = "C:\Emulation\360\recomps\tenchu z"
$exeDir = "$root\tenchuz-recomp\out\build\win-amd64-release"
$gameDir = "$root\extracted"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class FZ {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern IntPtr GetFocus();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("user32.dll")] public static extern IntPtr SetActiveWindow(IntPtr h);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr p);
  public static uint TargetPid = 0;
  public static System.Collections.Generic.List<IntPtr> Found = new System.Collections.Generic.List<IntPtr>();
  public static bool Cb(IntPtr h, IntPtr l) {
    uint pid; GetWindowThreadProcessId(h, out pid);
    if (pid == TargetPid && IsWindowVisible(h)) Found.Add(h);
    return true;
  }
  public static string Try(IntPtr hwnd) {
    ShowWindow(hwnd, 9);
    uint me = GetCurrentThreadId();
    IntPtr fg0 = GetForegroundWindow();
    uint fgThread0 = GetWindowThreadProcessId(fg0, IntPtr.Zero);
    // Attach to the *game window's* thread too, so SetFocus works.
    uint gameThread = GetWindowThreadProcessId(hwnd, IntPtr.Zero);
    bool a1 = AttachThreadInput(me, fgThread0, true);
    bool a2 = AttachThreadInput(me, gameThread, true);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    bool a3 = AttachThreadInput(me, gameThread, false);
    AttachThreadInput(me, fgThread0, false);
    IntPtr fg1 = GetForegroundWindow();
    return String.Format("attachFg={0} attachGame={1} fgBefore=0x{2:X8} fgAfter=0x{3:X8} match={4}",
      a1, a2, fg0.ToInt64(), fg1.ToInt64(), fg1 == hwnd);
  }
}
"@

$p = Start-Process -FilePath "$exeDir\tenchuz.exe" `
  -ArgumentList @("--game_data_root=`"$gameDir`"", "--mnk_mode=true", "--log_level=info") `
  -WorkingDirectory $exeDir -PassThru
Write-Output "pid=$($p.Id)"
Start-Sleep -Seconds 6
[FZ]::TargetPid = [uint32]$p.Id
[FZ]::Found.Clear()
[FZ]::EnumWindows([FZ+EnumWindowsProc]{ param($h,$l) [FZ]::Cb($h,$l) }, [IntPtr]::Zero) | Out-Null
if ([FZ]::Found.Count -eq 0) { Write-Output "no window"; Stop-Process -Id $p.Id -Force; return }
$hwnd = [FZ]::Found[0]
Write-Output ("game hwnd=0x{0:X}" -f $hwnd.ToInt64())
for ($i = 0; $i -lt $Seconds; $i += 3) {
  Write-Output ("t={0}s {1}" -f $i, [FZ]::Try($hwnd))
  Start-Sleep -Seconds 3
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
