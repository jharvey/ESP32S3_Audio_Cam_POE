@echo off
rem Start simulated intercom units.
rem   run-sim.bat                 demo: rooms 101 and 214, two gym units, a portable unit
rem   run-sim.bat 305 --echo gym  one unit, any intercom-sim options
setlocal
set EXE=%~dp0build\intercom-sim.exe
if not "%1"=="" (
  start "" "%EXE%" --name %*
  exit /b 0
)
start "" "%EXE%" --name 101 --listen
start "" "%EXE%" --name 214
start "" "%EXE%" --name 150a --echo gym
start "" "%EXE%" --name 150b --echo gym
start "" "%EXE%" --name 999 --battery
