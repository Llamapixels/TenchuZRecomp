# Enumerate every top-level window belonging to the game process, with class,
# title, visibility and rect. Used to confirm which HWND actually receives SDL
# input, and whether PrintWindow is targeting the render window.
param([string]$ExeName = "tenchuz")
$ErrorActionPreference = "Continue"
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class WE {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static uint TargetPid = 0;
  public static void Dump() {
    EnumWindows((h, l) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != TargetPid) return true;
      var cls = new StringBuilder(256); GetClassName(h, cls, 256);
      var txt = new StringBuilder(256); GetWindowText(h, txt, 256);
      RECT r; GetWindowRect(h, out r);
      IntPtr parent = GetParent(h);
      Console.WriteLine(String.Format(
        "hwnd=0x{0:X8} vis={1} cls='{2}' title='{3}' rect={4},{5} {6}x{7} parent=0x{8:X8}",
        h.ToInt64(), IsWindowVisible(h), cls, txt, r.L, r.T, r.R-r.L, r.B-r.T, parent.ToInt64()));
      return true;
    }, IntPtr.Zero);
  }
}
"@
$p = Get-Process -Name $ExeName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Output "process '$ExeName' not running"; return }
Write-Output "pid=$($p.Id) fg=0x$([WE]::GetForegroundWindow().ToInt64().ToString('X8'))"
[WE]::TargetPid = [uint32]$p.Id
[WE]::Dump()
