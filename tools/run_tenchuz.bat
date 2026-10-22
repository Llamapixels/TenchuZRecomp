@echo off
setlocal
REM Launch the recompiled Tenchu Z.
REM game_data_root points at the extracted game files (default.xex + DATA/).
set "EXEDIR=C:\Emulation\360\recomps\tenchu z\tenchuz-recomp\out\build\win-amd64-release"
set "GAMEDIR=C:\Emulation\360\recomps\tenchu z\extracted"

cd /d "%EXEDIR%"
if errorlevel 1 (echo EXE_DIR_MISSING: %EXEDIR% & exit /b 1)

start "" "tenchuz.exe" --game_data_root="%GAMEDIR%" %*
exit /b 0
