@echo off
rem Start simulated intercom units.
rem   run-sim.bat                 demo: rooms 101 and 214, two gym units (299, 298 in group 299), a portable unit
rem   run-sim.bat 305 --echo gym  one unit, any intercom-sim options
setlocal
set EXE=%~dp0build\intercom-sim.exe
if not "%1"=="" (
  start "" "%EXE%" --name %*
  exit /b 0
)
rem Web interfaces: http://localhost:8101, :8214, :8299, :8298, :8999
rem Demo login: any user name, password "intercom-demo". Demo network key: "intercom-demo-key"
rem (intercom-console --key intercom-demo-key). Simulator only: real units get theirs over USB.
set WEB=--web-pass intercom-demo --net-key intercom-demo-key
start "" "%EXE%" --name 101 --listen --web-port 8101 %WEB%
start "" "%EXE%" --name 214 --web-port 8214 %WEB%
start "" "%EXE%" --name 299 --echo gym --web-port 8299 %WEB%
start "" "%EXE%" --name 298 --echo gym --web-port 8298 %WEB%
start "" "%EXE%" --name 999 --battery --web-port 8999 %WEB%
rem gym units form one room group (they don't play each other back)
timeout /t 3 /nobreak >nul
"%EXE%" --send "299 group 299" --key intercom-demo-key
"%EXE%" --send "298 group 299" --key intercom-demo-key
