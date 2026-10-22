@echo off
setlocal
set "VSWD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"

echo === MSVC environment ===
call "%VSWD%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)

set "PATH=%VSWD%\VC\Tools\Llvm\x64\bin;%VSWD%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSWD%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

cd /d "%~dp0..\tenchuz-recomp"
if errorlevel 1 (echo CD_FAILED & exit /b 1)

echo === CONFIGURE (win-amd64-release) ===
cmake --preset win-amd64-release
if errorlevel 1 (echo CONFIGURE_FAILED & exit /b 1)

echo === BUILD (win-amd64-release) ===
cmake --build --preset win-amd64-release
if errorlevel 1 (echo BUILD_FAILED & exit /b 1)

echo === DONE_OK ===
exit /b 0
