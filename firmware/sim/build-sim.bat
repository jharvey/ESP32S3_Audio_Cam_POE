@echo off
rem Build the intercom simulator and self-test (Windows, Qt MinGW kit).
rem   build-sim.bat     -> sim\build\intercom-sim.exe, sim\build\ic-selftest.exe
rem Prereqs: Qt 6 (MinGW kit) + MinGW + CMake + Ninja under %QTROOT% (same kit as the
rem T-Deck Pro simulator; no Qt Multimedia needed).
setlocal
if "%QTROOT%"=="" set QTROOT=%USERPROFILE%\Qt
if "%QTVER%"=="" set QTVER=6.10.3
set PATH=%QTROOT%\Tools\mingw1310_64\bin;%QTROOT%\Tools\Ninja;%QTROOT%\Tools\CMake_64\bin;%QTROOT%\%QTVER%\mingw_64\bin;%PATH%
cd /d "%~dp0"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QTROOT%\%QTVER%\mingw_64 || goto :fail
cmake --build build -j 8 || goto :fail
windeployqt --no-translations --no-system-d3d-compiler --no-opengl-sw build\intercom-sim.exe >nul || goto :fail
copy /y %QTROOT%\%QTVER%\mingw_64\plugins\platforms\qoffscreen.dll build\platforms\ >nul
for %%d in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do copy /y %QTROOT%\Tools\mingw1310_64\bin\%%d build\ >nul
echo.
echo Built sim\build\intercom-sim.exe   (run-sim.bat to start)
build\ic-selftest.exe
exit /b %ERRORLEVEL%
:fail
echo BUILD FAILED
exit /b 1
