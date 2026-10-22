@echo off
rem Double-click starter for tenchuz_launcher.ps1 (kept beside it).
rem -ExecutionPolicy Bypass applies to this one script run only; it does not
rem change the system policy.
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0tenchuz_launcher.ps1" %*
