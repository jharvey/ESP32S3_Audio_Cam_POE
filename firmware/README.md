# Intercom firmware

Firmware for the ESP32S3_Audio_Cam_POE classroom intercom, plus a PC simulator that runs
the same code. The plan is in [Docs/firmware_plan.md](../Docs/firmware_plan.md).

![simulator](sim/docs/sim-portable-unit.png)

## Layout

| Path | What |
|---|---|
| `src/core/` | The intercom itself, plain C++17 with no Arduino calls: packets, jitter buffer, multi-talker mixer, NLMS echo canceller, link failover, room-number setup. Built unchanged into both the firmware and the simulator. |
| `src/board/Pins.h` | ESP32-S3 pin map from the schematic netlist. |
| `src/main.cpp` | ESP32-S3 drivers behind the core's interfaces. **First draft, not compiled yet.** |
| `sim/` | Qt desktop simulator and the `ic-selftest` checks. |

## What works today (in the simulator)

- Push-to-talk all-call: every unit hears whoever holds TALK, and several talkers at once are mixed (loudest 4).
- Room groups: several units with the same room number (`150a`, `150b` in the gym) don't play each other back.
- Echo cancellation on every unit, tested against simulated echo. With residual-echo suppression:
  - Classroom: about 16 dB, 20 dB with suppression.
  - Gym (2 s reverb): about 8 dB, 20 dB with suppression.
  - People talking in the room still come through while the far end plays.
- PoE / Wi-Fi failover: unplugging a battery unit moves it to Wi-Fi in one 20 ms frame. Plugging in moves it back after 200 ms.
- Room number = IP address, set on the unit's buttons: room 214 → 10.42.20.14, 214a → 10.42.21.14.
- Status beacons: every unit lists the others with link, power and battery.

## Echo canceller tuning: auto-tune plus manual override

The simulator's numbers are starting points. Real rooms, cases and speakers will differ, so every
unit can measure itself, and every setting can be changed by hand.

![auto-tune](sim/docs/sim-autotune-gym.png)

### Auto-tune

Start it by holding **MODE + UP for 3 s** on the panel, or with the console command `cal`.
It takes about 6 s and needs a quiet room. TALK cancels it. The display shows `c42` (progress) while it runs.

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
- **Network:** from any PC on the intercom VLAN.
- **Simulator:** the command box in the window.

| Command | What it does |
|---|---|
| `help` | list commands and parameters with their ranges |
| `get [key ...]` | show parameters |
| `set key value [key value ...]` | apply now; not saved until `save` |
| `save`, `load`, `defaults`, `undo` | keep, revert, factory reset, restore previous |
| `cal`, `cal result` | auto-tune, and the last measurements |
| `stats` | live ERLE, double-talk, link, levels |
| `unit 214a` | set the room number (and IP) without using the buttons |

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

Over the network, from the simulator build:

```bat
intercom-sim --send "214 get"
intercom-sim --send "214 set mu 0.1 nlp_db -18"
intercom-sim --send "214 save"
intercom-sim --send "all cal"
intercom-sim --send "all cal result"
```

The target is `all`, a room (`214`, every unit in it) or one unit (`214a`).

> **Before deployment:** network commands have no PIN yet. Anyone on the intercom VLAN could change settings.

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

This starts a demo set: rooms 101 and 214, gym units 150a/150b, and portable unit 999. To start a single unit, pass a name and options:

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

Not built yet. It needs Arduino-ESP32 3.x (the pioarduino platform in `platformio.ini`). The first build downloads about 1.5 GB of toolchain.
