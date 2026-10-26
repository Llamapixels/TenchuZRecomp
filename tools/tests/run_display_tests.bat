@echo off
setlocal
set "VSWD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSWD%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "PATH=%VSWD%\VC\Tools\Llvm\x64\bin;%VSWD%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSWD%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cd /d "%~dp0..\..\tenchuz-recomp"
if errorlevel 1 exit /b 1
cmake --preset win-amd64-release -DTENCHUZ_DISPLAY_TESTS=ON -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=
if errorlevel 1 exit /b 1
cmake --build --preset win-amd64-release --target tenchuz_display_tests
if errorlevel 1 exit /b 1
out\build\win-amd64-release\tenchuz_display_tests.exe "out\build\win-amd64-release\display-test-data"
if errorlevel 1 exit /b 1
powershell -NoProfile -STA -ExecutionPolicy Bypass -File "..\tools\tests\launcher_display_tests.ps1"
exit /b %errorlevel%
