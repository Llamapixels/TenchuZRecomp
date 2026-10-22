@echo off
setlocal
REM Rebuild + reinstall the ReXGlue SDK (Release, win-amd64) after local source edits.
REM The Tenchu Z recomp links against out/install/win-amd64, so the SDK must be
REM reinstalled for runtime (texture cache, HLE) changes to take effect.
set "VSWD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"

call "%VSWD%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)

set "PATH=%VSWD%\VC\Tools\Llvm\x64\bin;%VSWD%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSWD%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

rem The ReXGlue SDK clone: REXGLUE_SDK, or rexglue-sdk next to this project's folder.
if not defined REXGLUE_SDK set "REXGLUE_SDK=%~dp0..\..\rexglue-sdk"
cd /d "%REXGLUE_SDK%"
if errorlevel 1 (echo CD_FAILED & exit /b 1)

echo === BUILD SDK (Release) ===
cmake --build out/build/win-amd64 --config Release
if errorlevel 1 (echo BUILD_FAILED & exit /b 1)

echo === INSTALL SDK (Release) ===
cmake --install out/build/win-amd64 --config Release
if errorlevel 1 (echo INSTALL_FAILED & exit /b 1)

echo === SDK_DONE_OK ===
exit /b 0
