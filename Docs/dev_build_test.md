# Building and testing (development)

## Firmware (ESP32-S3)

- PlatformIO with the community **pioarduino** platform (Arduino-ESP32 3.x / ESP-IDF 5.x); `firmware/platformio.ini`.
  The first build downloads about 1.5 GB of toolchain and framework.
- Libraries: arduino-libopus (pinned commit) for Opus. ESP-SR, the W5500 driver, SD_MMC and the HTTP/WebSocket
  servers come with the framework.
- **Fast builds on VM100** (`ssh debian-build`, Debian 12): `~/.platformio/penv/bin/pio run -e intercom`. VM100 has no
  GitHub push rights: push from the PC, then fetch there (or copy the source over with `tar | ssh`).
- **Windows:** `local-windows.ini` (not committed; see `local-windows.ini.example`) points PlatformIO at the NAS
  toolchain.
- Output: `.pio/build/intercom/firmware.bin`. Flash and set up units with `tools/update-unit.bat` / `unit_tool.py`.

## Simulator and tests (PC)

- **Windows (Qt 6.10, MinGW):** `firmware\sim\build-sim.bat` builds the simulator (`intercom-sim.exe`), the office
  console (`intercom-console.exe`), the self-test and the network simulation, then runs the self-test. libopus is
  fetched and built by CMake the first time (needs git and internet).
- **Linux (no Qt needed for the tests):**
  `cmake -S firmware/sim -B build && cmake --build build --target ic-selftest && build/ic-selftest`.
- **`ic-selftest`** (must print `ALL PASS`, about 115 checks): packets and Opus, jitter buffer, mixer, echo canceller
  in a classroom and a gym, echo from the board layout, echo between rooms, the 4-unit intercom on an in-memory
  network (talk, mixing, groups, failover, fault codes, duplicates, codecs), tuning, auto-tune, console, signed
  commands, web interface, panel driver.
- **`ic-netsim`:** whole-school network load (units, switches, uplinks, emergency video); results in
  `Docs/network_sim_results.md`.
- **Demo:** `firmware\sim\run-sim.bat` starts rooms 101, 214, gym units 299/298 (group 299) and a portable unit 999;
  web pages on http://localhost:8101 etc. (demo password `intercom-demo`).
- **Scripted runs:** `intercom-sim --name 101 --headless --auto-talk 3,2 --quit-after 8` prints a SUMMARY line;
  `intercom-sim --send "214 cal" --key <key>` sends a signed command.

## Walkthrough videos

`powershell -ExecutionPolicy Bypass -File firmware\sim\record-walkthroughs.ps1` records the five videos into
`Docs/videos/` (needs ffmpeg on the PATH; records only the two simulator windows at the top-left of the screen;
don't use the PC while it runs). `-Only 02-gym-group` records one.

## Echo prediction from the board

`"C:\Program Files\KiCad\10.0\bin\python.exe" firmware\tools\echo_predict.py <board>.kicad_pcb
firmware\sim\common\BoardGeometry.h Docs\echo_prediction.md [--speaker X,Y]` after the mics or the speaker move.
