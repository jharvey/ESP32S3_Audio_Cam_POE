# Intercom firmware

Firmware for the ESP32S3_Audio_Cam_POE classroom intercom, plus a PC simulator that runs
the same code. The plan is in [Docs/firmware_plan.md](../Docs/firmware_plan.md).

![simulator](sim/docs/sim-portable-unit.png)

## Layout

| Path | What |
|---|---|
| `src/core/` | The intercom itself, plain C++17 with no Arduino calls: packets, jitter buffer, multi-talker mixer, NLMS echo canceller, link failover, room-number setup. Built unchanged into both the firmware and the simulator. |
| `src/board/Pins.h` | ESP32-S3 pin map from the schematic netlist. |
| `src/board/Tm1637.*`, `Panel.h` | Front-panel display and buttons (TM1637 through the TCA9555), plain C++ so the PC tests check it. |
| `src/main.cpp` | ESP32-S3 drivers behind the core's interfaces. **Compiles; not tested on hardware yet.** |
| `tools/` | `update-unit.bat` / `unit_tool.py`: USB firmware updates and first-time setup. |
| `sim/` | Qt desktop simulator and the `ic-selftest` checks. |
| `sim/console/` | `intercom-console`: the office console (room grid, all-call, talk to a room, signed commands). |
| `sim/netsim/` | `ic-netsim`: a whole-school network load simulation (switches, uplinks, QoS, Wi-Fi, emergency video). Results are in [Docs/network_sim_results.md](../Docs/network_sim_results.md). |

## What works today (in the simulator)

- Push-to-talk all-call: every unit hears whoever holds TALK, and several talkers at once are mixed (loudest 4).
- Room groups: several units in one room (the gym: 299, 298, ...) set to one group with `group 299` don't play each other back.
- Echo cancellation on every unit, tested against simulated echo. With residual-echo suppression:
  - Classroom: about 16 dB, 20 dB with suppression.
  - Gym (2 s reverb): about 8 dB, 20 dB with suppression.
  - People talking in the room still come through while the far end plays.
- PoE / Wi-Fi failover: unplugging a battery unit moves it to Wi-Fi in one 20 ms frame. Plugging in moves it back after 200 ms.
- Room number = IP address, set on the unit's buttons: room 214 → 10.42.2.14 (10.42.<floor>.<room>). Two units on one number both show `dUP`.
- Status beacons: every unit lists the others with link, power and battery.

## Echo canceller tuning: auto-tune plus manual override

The simulator's numbers are starting points. Real rooms, cases and speakers will differ, so every
unit can measure itself, and every setting can be changed by hand.

![auto-tune](sim/docs/sim-autotune-gym.png)

### Auto-tune

Start it by holding **UP for 5 s** on the panel, or with the console command `cal` (or the web page).
It takes about 6 s and needs a quiet room. TALK cancels it. The display shows `c42` (progress) while it runs.

> The board reads its buttons through the TM1637 display chip, which reports **one key at a time**,
> so two-button combos can't work on the hardware. MODE + UP for 3 s still works in the simulator.

On the ESP32 the analysis after the measurement runs in a low-priority task, so audio keeps
playing while it works (it takes a few seconds on the S3).

1. It measures the room noise.
2. It plays a 2 s noise burst and records the mic, then records 1 s of silence.
3. From the recording it works out:

   | Measurement | How | Used for |
   |---|---|---|
   | Speaker → mic delay | cross-correlation | `delay_ms` |
   | Reverb time (RT60) | how fast the room goes quiet after the burst | `taps`, gym ducking |
   | Case coupling | mic level vs speaker drive | warnings |
   | Noise floor | the quiet recording | warnings |

4. It replays the recording through the echo canceller with the old settings and several new filter lengths, and keeps the best.
5. It sets the residual echo suppression depth (`nlp_db`) and turns ducking on for long reverb.
6. It saves the result. `undo` brings back the previous settings.

Afterwards the display shows `e17` (17 dB of echo reduction) or `err`.

Auto-tune refuses to run in a noisy room. It also reports "no clear echo" when the speaker or mics aren't working, so it doubles as a hardware check.

Simulator results:

| Case | Result |
|---|---|
| 12 ms hardware latency | found 12.1 ms |
| 40 ms hardware latency | found 40.0 ms; echo reduction 14.4 → 19.2 dB |
| 150 ms latency (beyond the default filter) | echo reduction −0.3 → 19 dB |
| Classroom | RT60 measured 0.5 s |
| Gym | RT60 measured 1.3 s, ducking switched on |

The thresholds behind these decisions are tuned in the simulator, so treat them as first guesses until real rooms confirm them.

### Manual override: the console

The same text commands work in three places:
- **USB serial:** 115200 baud, native USB port.
- **Network:** from the office console or `intercom-sim --send`, signed with the network key.
- **Simulator:** the command box in the window.

| Command | What it does |
|---|---|
| `help` | list commands and parameters with their ranges |
| `get [key ...]` | show parameters |
| `set key value [key value ...]` | apply now; not saved until `save` |
| `save`, `load`, `defaults`, `undo` | keep, revert, factory reset, restore previous |
| `cal`, `cal result` | auto-tune, and the last measurements |
| `stats` | live ERLE, double-talk, link, levels |
| `unit 214` | set the room number (and IP) without using the buttons |
| `group 299` | put this unit in room group 299 (`group off` = its own room) |
| `wifi <ssid> <pass>` | Wi-Fi for failover and portable units (USB only; `wifi off`) |
| `tone [seconds]` | play a 1 kHz test tone on the unit's speaker |
| `talk [seconds\|off]` | transmit as if TALK were held (default 5 s). For bring-up before the panel works. **USB only.** |
| `selftest`, `selftest result` | board check: which chips answer, Ethernet link, PSRAM, and a tone the mic must hear |
| `version` | firmware version |
| `webpass <password>` | set the web interface password; `webpass off` turns the web interface off. **USB only.** |
| `netkey <key>` | set the network command key (16+ characters); `netkey off` refuses all network commands. **USB only.** |

Parameters:

| Parameter | Meaning |
|---|---|
| `aec` | echo canceller on/off |
| `taps` | echo canceller filter length |
| `delay_ms` | speaker → mic delay |
| `mu` | adaptation step |
| `dt_db` | double-talk threshold |
| `nlp`, `nlp_db` | residual echo suppression on/off and depth |
| `duck`, `duck_db` | speaker ducking while TALK is held, on/off and depth |
| `volume_db` | speaker volume |
| `mic_gain_db` | gain on the sent audio |
| `cal_level_db` | auto-tune noise burst level |

Over the network, from the simulator build (or the office console):

```bat
intercom-sim --send "214 get" --key <network key>
intercom-sim --send "214 set mu 0.1 nlp_db -18" --key <network key>
intercom-sim --send "all cal result" --key <network key>
```

The target is `all` or a room number (`214`).

**Network key.** Units refuse network commands until a key is set over USB (`netkey`).
- Every command is signed with the key (HMAC-SHA256), including its target, so it can't be changed or re-addressed.
- Each command carries the sender's clock in ms. A unit only accepts a number higher than the last one it took, and remembers it across restarts, so a recorded command can't be replayed.
  - Office PCs need a correct clock: one running behind is refused with "old or repeated command".
- `talk`, `webpass` and `netkey` are refused over the network even when signed.
- Commands and replies aren't encrypted, so keep the intercom on its own VLAN.

## Web interface

Each unit serves its own page at its IP address, for example `http://10.42.2.14` for room 214.

![web interface](sim/docs/sim-web.jpg)

The page shows and does:

| Section | Contents |
|---|---|
| Header | room, IP, Ethernet / Wi-Fi, PoE / battery, firmware version |
| Echo canceller | live echo reduction, double talk, mic / sent / speaker levels |
| Audio | talkers playing, dropouts filled in, late packets, uptime |
| Tools | auto-tune with a progress bar, test tone, set room number |
| Settings | every parameter with its range; apply, save, undo, reload, factory defaults |
| Last auto-tune | the measurements from `cal result` |
| Other units heard | every unit's link, power and battery: a quick health check of the school from any unit |
| Console | the same commands as USB |

Security:
- **Off until a password is set over USB** with `webpass <password>` (8 characters or more). It can't be set or changed from the network or the web page.
- **Login:** the browser asks for a password (any user name).
  - The password is stored as a salted, slow hash, never as text.
  - 5 wrong passwords lock the page for 30 s.
- **Firmware upload is a separate switch.** It is off until `netupdate on` is typed on the USB console, and
  then still needs the web password. See "Updates over the network" below.
- **Cross-site protection:** commands need a header that other websites can't send.
- **Not encrypted (plain HTTP).** Keep the intercom on its own VLAN.

In the simulator, `run-sim.bat` starts each unit with its page on localhost:

| Unit | Address |
|---|---|
| 101 | http://localhost:8101 |
| 214 | http://localhost:8214 |
| 150a | http://localhost:8150 |
| 150b | http://localhost:8151 |
| 999 | http://localhost:8999 |

Log in with any user name and the demo password `intercom-demo`. That password exists only in the simulator: real units get theirs over USB.
For a single unit: `intercom-sim --name 214 --web-port 8214 --web-pass <password>`.

## Office console

`intercom-console` is the office PC app. It works the same with simulated units and real ones.

![office console](sim/docs/office-console.png)

- **Room grid:** every unit's link, power, battery and uptime.
  - Talking units are highlighted.
  - Units on battery are yellow; red below 25 %.
  - Units not heard for 3.5 s are red **OFFLINE**.
  - With `--rooms` or `--rooms-file`, units that were never heard show up too, so a dead unit is noticed before an emergency.
- **All-call** and **Talk to selected room**: hold the button. Uses the PC microphone, or a test voice. Only the chosen room hears a room call.
- **Hears every unit** that talks, mixed on the PC speakers.
- **Commands:** to the selected unit, its room, or all units, signed with the network key. Replies show in the log.
- **Open web page** for the selected unit.

```bat
intercom-console --key <network key> --rooms-file rooms.txt
```

Not built yet: emergency mode (no alarm button) and listen-in.

## Simulator

Build (Windows, the same Qt MinGW kit as the T-Deck Pro simulator; no extra Qt modules needed):

```bat
firmware\sim\build-sim.bat
```

This also runs `ic-selftest`, which must print `ALL PASS`.

Run:

```bat
firmware\sim\run-sim.bat
```

This starts a demo set: rooms 101 and 214, gym units 150a/150b, and portable unit 999. They use the demo network key
`intercom-demo-key`, so the office console can talk to them:

```bat
firmware\sim\build\intercom-console.exe --key intercom-demo-key --rooms 101,150a,150b,214,999
```

To start a single unit, pass a name and options:

```bat
firmware\sim\run-sim.bat 305 --echo gym
```

- **Keys:** Space = TALK, M = MODE, arrow keys = UP/DOWN.
- **Hardware switches:** the right-hand panel has PoE, Wi-Fi range, battery, the mic source (synthetic talker or the PC mic) and the room echo.
- **Hearing a unit:** tick "Play this unit's speaker on the PC" on one unit.
- **Network:** units talk over real UDP multicast (239.255.42.1:5004), so simulators on other PCs on the LAN join in. Windows may ask to allow network access the first time.
- **Echo canceller code:** the simulator uses the portable NLMS canceller. On the ESP32 the plan is the ESP-SR audio front end, so final tuning happens on hardware.

Scripted run (no window), prints a `SUMMARY` line:

```bat
intercom-sim --name 101 --headless --auto-talk 3,2 --quit-after 8
```

## Firmware build

Compiles with no errors or warnings (Arduino-ESP32 3.2.0 on ESP-IDF 5.4, via the pioarduino platform in `platformio.ini`).
Not run on hardware yet. The first-board checklist is in [Docs/bring-up.md](../Docs/bring-up.md).

```bat
cd firmware
pio run
```

Where the files live:
- **Espressif framework and libraries (~1.5 GB):** on the NAS at `M:\pio` (`core_dir`).
- **Compiler (1.3 GB):** a local copy at `C:\pio-tc`. Windows won't run programs from the NAS share ("Access is denied"), so `platform_packages` links it in.
  - To set up on another PC, copy `M:\pio\packages\toolchain-xtensa-esp-elf` to `C:\pio-tc\`.
- **Build output:** local, in `firmware\.pio`.

The NAS is slow, so a clean build takes about 50 minutes. Rebuilds after small changes are much faster.
If that's too slow, move `core_dir` to a local short path such as `C:/pio`.

## Updates over the network (R3.0)

Status: tested in the simulator's self-test and fuzzer, and the ESP32 build compiles. Not yet run on a board.

A unit that has a web password and `netupdate on` (both set once over USB; `unit_tool.py setup --netupdate`)
takes new firmware from its own web page ("Firmware update") or from a PC:

    python tools/net_update.py firmware.bin --rooms 214
    python tools/net_update.py firmware.bin --rooms 101-112,201-212
    python tools/net_update.py --rooms 101-112 --info

The file goes up in 4 KB pieces into the spare firmware slot. The running firmware is not touched until the
whole image has arrived and checked out, then the unit restarts into it with its settings kept.

- **Calls come first.** While the unit talks, plays a call or a sound, the transfer waits and then carries on.
- **What is refused:** a wrong size, a damaged image (SHA-256), a file that is not an ESP32 image, and
  firmware of another project (the image must carry this firmware's tag). A transfer that stalls for 30 s
  is dropped. On battery below 30 % it does not start.
- **One unit at a time.** `net_update.py` stops at the first unit that fails or does not come back, so a bad
  image reaches one room, not the school.
- **Recovery** if a unit does not come back and did not fall back: USB (`update-unit.bat`) or `/firmware.bin` on an SD card.
- **Fall-back (R3.1):** new firmware is on trial for its first minute. See the next section.

## Fall-back, event log, emergency messages, self-check, backup, camera (R3.1)

Status: 39 checks in the simulator's self-test, with the fuzzer and chaos test run over the new commands,
and the ESP32 build compiles. Nothing here has run on a board yet; the camera and SD card parts cannot be
tested without one.

- **Fall-back after a bad update.** Firmware that came from an update is on trial until it has run for a
  minute with a network link. If it crashes or hangs before that, the bootloader starts the old firmware
  again at the next restart. If it runs but has no network link for 5 minutes, it goes back by itself.
  A power cut in that first minute also puts the old firmware back. `net_update.py` waits for the unit to
  confirm before it goes on to the next one. Firmware flashed over USB is not on trial.
- **Event log.** Every unit keeps the last 200 faults and calls it saw, for the whole school: rooms going
  offline and coming back, restarts, battery power, fault codes, failed self-checks, calls to the office,
  missed calls, and who talked to whom for how long. `log`, `log faults`, `log calls` on the console; the
  school page has a "Log" panel. The log is kept in memory, so a restart of that unit clears it.
- **Firmware version per room.** Status packets carry the version; the school page names a room whose
  firmware differs from the unit you are looking at.
- **Emergency messages.** `emergency lockdown|evacuate|shelter|clear`, with `all` to tell every unit (USB or
  web page; the school page has the four buttons). Needs the network key and a set clock, like `send`.
  - Each kind has its own signal. It sounds for 1 minute (evacuate: 5), then the room is left quiet.
  - The display shows `LoC`, `EUA` or `SHE`, the alarm light (J11) flashes and the RGB LED is red until
    `emergency clear`. An emergency nobody clears ends by itself after 4 hours.
  - A live voice comes first: the office can still talk to every room. Timed bells are off meanwhile.
  - The command goes out ten times, 2 s apart, then every 30 s, so a lost packet or a unit that starts
    late does not leave a room out.
  - Recorded messages: put `/msg/lockdown.wav`, `/msg/evacuate.wav`, `/msg/shelter.wav` on the unit's SD
    card (16 kHz, mono, 16-bit WAV, up to 30 s). The message plays after each signal. Loaded at start.
- **Nightly self-check.** `selfcheck 02:30` (or the school page: "Set on all units"). At that time each
  idle unit plays the 1 s test tone and listens for it; rooms start a few seconds apart. A failure is
  tried once more 30 s later, then shows as "SELF-CHECK FAILED" on the school page and in the log.
  `selfcheck off` stops it, `selfcheck now` runs one.
- **Settings backup.** The unit page saves room number, group, tuning, timed events, self-check time and
  coupling results to a text file and restores them on a replacement unit (`backup` / `restore key=value`
  on the console). Passwords and keys are never in the file.
- **Camera, emergencies only.** `/api/cam.jpg` gives a picture only while an emergency is on at that unit,
  and each viewing goes into the event log. The camera is powered only then. The unit page shows it.

## Optional features (R3.2)

Status: 32 checks in the simulator's self-test, fuzzer and chaos test run over the new commands, ESP32
build compiles. Not yet run on a board; the page controls have not been tried in a browser.

Every feature here is **off** until it is switched on: `feature NAME on` on the USB console, the tick
boxes under "Features" on the school page (they set this unit and send to all), or a signed network
command (`send all feature dnd on`). `feature` lists them; `feature all on|off` sets all seven.

| Feature | What it does | Commands |
|---|---|---|
| `reason` | OFFICE held 1.5 s calls the office as "help needed" (display `HLP`); a short press stays a question. | `callofc help` |
| `queue` | Rooms calling the office wait in order: silent help, then help, then longest wait. The school page lists them; the office unit shows the first room's number (third dot lit) and its TALK answers that room. | |
| `zones` | Up to 8 named sets of rooms to page. | `zone 1 east 101-112,201`, `zone 1 off`, `talk 5 zone 1` |
| `dnd` | Do not disturb for 1 to 240 minutes (exams): calls from rooms and timed bells are not played; the office and emergencies still are. Display `dnd`. | `dnd 45`, `dnd off` |
| `quiet` | Quieter between two times of day (over midnight too). Calls, chimes and bells are turned down; emergency signals never are. | `quiet 18:00 07:00 -12`, `quiet off` |
| `silent` | TALK and OFFICE held together 1 s: a silent help call. Nothing sounds or shows in the room except the small "sending" dot; its microphone goes to the office unit for 2 minutes; no call is played in that room meanwhile. Ends on OFFICE, when the office clears it (`callofc off`), or after 30 minutes. This is a live microphone: decide on it as school policy before switching it on. | `callofc silent` |
| `failover` | While the office unit is offline, the stand-in acts as the office: it gets the queue and the silent calls. | `office 105 104` |

`office ROOM [STAND-IN]` names the office unit and its stand-in on every unit (send it to all). Zones,
the office, quiet hours and the switches are part of the settings backup.

## People sensor (mmWave radar, 0.5.1)

A unit with a Hi-Link radar tells the office how many people its sensor sees. Nothing is set up: the unit
finds the sensor by itself, and a unit without one sends nothing.

| Sensor | Where | What it gives |
|---|---|---|
| HLK-LD2410C | J21 on the board (0 ohm links R113 and R114 fitted) | someone is there or not, how far away, moving or still |
| HLK-LD2450 | J22 on a cable (remove R113 and R114) | up to three people, each with a position and speed |

Pins: the sensor's serial line is on GPIO10 (256000 baud, read only). The LD2410C's OUT pin is on U22 P13.
LoRa DIO0 moved to U22 P14 to free GPIO10.

- **Count.** The count is the most people seen at once in the last 3 s, so a person the sensor loses for a
  moment still counts. The LD2450 follows three people at most, so 3 is shown as "3 or more". The LD2410C
  cannot count: it shows "someone here".
- **Empty for how long.** Each unit keeps the time since its sensor last saw anyone, in minutes (up to 4 hours).
- **OUT pin.** If no serial data comes, the unit uses the LD2410C's OUT pin (high = someone there). The pin
  counts as a sensor only after it has been low once, because a pin with nothing on it stays high.
- **Office page.** Each room's tile shows "2 people", "someone here" or "empty 12 min". The top line shows the
  total, for example "at least 14 people in 9 of 20 rooms with a sensor". The red alarm box lists the rooms
  with people in them. On the plan the people are small black dots, as before.
- **Several units in one room** (a group, such as the gym): the room's count is that of the unit that sees
  most, not the sum, because the sensors see the same people.
- **Commands.** `radar` shows what this unit's sensor sees. `people` lists every room with a sensor.
- **PC console.** The room table has a People column.

The count is a help, not a head count: a sensor sees only part of a room, and rooms without a sensor are not counted.

Not tested on a real sensor yet. The PC console change is not compiled (the build machine has no Qt).

## USB updates and first-time setup

`tools/update-unit.bat` makes each room visit one command.
It uses PlatformIO's own Python, so nothing extra needs installing.

1. **Build and publish a release:**
   ```bat
   cd firmware
   pio run
   tools\update-unit.bat release
   ```
   This copies the four flash images and a `manifest.json` into `M:\ESP32S3_Audio_Cam_POE\releases\<version>`.
   Bump `kFirmwareVersion` in `src/core/Config.h` for each release.
2. **Update a unit:** plug the laptop into the unit's USB-C port and run `tools\update-unit.bat`.
   - It reads the unit's room and version first.
   - It flashes the newest release.
   - It checks the unit comes back with the new version **and the same room number**.
   - It never erases flash, so the room number, tuning, web password and network key are kept.
3. **New unit:**
   ```bat
   tools\update-unit.bat setup --flash --unit 214 --webpass <password> --netkey <key> --selftest
   ```
   Add `--burn-efuse` on boards with the camera on GPIO45, to fix the flash voltage permanently (see [bring-up](../Docs/bring-up.md)).

Every update and setup is logged to `updates.csv` in the releases folder.
If more than one Espressif device is plugged in, the tool won't guess: pass `--port COMx`.
