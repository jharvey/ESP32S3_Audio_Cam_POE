@echo off
rem Update the plugged-in intercom unit over USB with the newest release. Keeps its settings.
rem   update-unit.bat               flash the newest release
rem   update-unit.bat info          show room, IP and firmware version
rem   update-unit.bat setup --unit 214 --webpass ... --netkey ... --selftest
rem See unit_tool.py for everything else.
setlocal
set PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe
if not exist "%PY%" (
  echo PlatformIO's Python not found at %PY%. Install PlatformIO first.
  exit /b 1
)
if "%1"=="" (
  "%PY%" "%~dp0unit_tool.py" update
) else (
  "%PY%" "%~dp0unit_tool.py" %*
)
