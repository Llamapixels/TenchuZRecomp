@echo off
setlocal
REM Launch the recompiled Tenchu Z.
REM game_data_root points at the extracted game files (default.xex + DATA/).
set "EXEDIR=%~dp0..\tenchuz-recomp\out\build\win-amd64-release"
set "GAMEDIR=%~dp0..\extracted"

cd /d "%EXEDIR%"
if errorlevel 1 (echo EXE_DIR_MISSING: %EXEDIR% & exit /b 1)

start "" "tenchuz.exe" --game_data_root="%GAMEDIR%" %*
exit /b 0
