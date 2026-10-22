@echo off
rem First-time setup: extracts your Tenchu Z disc image into "extracted" and
rem creates the profile settings the game needs. See README.txt.
rem -ExecutionPolicy Bypass applies to this one script run only.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
