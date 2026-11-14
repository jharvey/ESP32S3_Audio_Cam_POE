# Intercom roadmap

Where the OVCS classroom intercom stands and what comes next. The design detail is in
[firmware_plan.md](firmware_plan.md); this file tracks progress and order.

Last updated: 14 Nov 2026.

## Where it stands

| Area | Status |
|---|---|
| Intercom logic (talk, mixing, echo cancelling, auto-tune, console, failover) | Done and tested in the PC simulator |
| Whole-school network load (35 units, emergency video) | Simulated; results in [network_sim_results.md](network_sim_results.md) |
| ESP32-S3 firmware | Compiles. Not run on hardware yet. |
| Web interface | Built; tested in the simulator |
| Network command key | Built; tested in the simulator |
| Front panel driver (TM1637 display and buttons) | Built; tested against a model of the chip |
| USB update and setup tool | Built; not yet tried on a board |
| Office console (PC app) | Room grid, all-call, talk to a room, signed commands. Tested with simulated units. |
| First-board checklist | [bring-up.md](bring-up.md) |
| PCB | Placement done. 63 connections unrouted, DRC not run. |
| Emergency mode, camera, LoRa | Not started |

### What the firmware does today

When a board boots, the firmware:
- starts the USB console, the I/O expander (TCA9555) and the servo/LED driver (PCA9685),
- reads one PDM mic and drives the speaker amp at 16 kHz,
- brings up Ethernet (W5500) with a fixed IP from the room number (room 214 → 10.42.20.14),
- runs the front panel in its own task: room number on the display, buttons through the TM1637 key scan,
- runs the intercom every 20 ms:
  - sends mic audio while TALK is held (or for `talk` on USB),
  - plays and mixes everyone else (loudest 4 talkers), skipping units in the same room,
  - cancels speaker echo (NLMS, 1024 taps),
  - sends a status beacon once a second,
- accepts console commands over USB and the network. Network commands must be signed with the network key.
- serves a password-protected web page on port 80 (off until a password is set over USB),
- runs `selftest`: I2C chips, display, Ethernet, PSRAM, and a tone the mic must hear,
- runs auto-tune's analysis in a low-priority task, so audio keeps going,
- moves to the new IP address at once when the room number changes.

### What the firmware doesn't do yet

| Gap | Effect |
|---|---|
| Wi-Fi | Never started, so portable units have no failover. |
| Battery monitor (AXP2101) | Always reports "on PoE". |
| Second mic | Only one of the two PDM mics is used. |
| ESP-SR echo canceller | Uses the simpler NLMS canceller instead. |
| Camera, cover servo, video | Not started. |
| Watchdog | No automatic restart if the audio task hangs. |

### Hardware finding: one key at a time

The four panel buttons are read through the TM1637's key scan, which reports only one key at a time.
Two-button combos can't work, so auto-tune on the panel is **UP held 5 s** (MODE + UP still works in the simulator).
If you want TALK to work while another button is held, or real combos, the buttons need to move to the TCA9555's
spare port 1 pins in a later board spin.

## Phases

### Phase 0: bring-up on the first board

Goal: two boards on a PoE switch talk to each other. Checklist: [bring-up.md](bring-up.md).

- [ ] Route the PCB and run DRC. Reminder set for Mon 16 Nov.
- [ ] Order and assemble 2–3 boards.
- [x] Firmware support for bring-up: `talk`, `selftest`, `tone`, `version`, and the USB setup tool (`--burn-efuse` for the strapping-pin fix).
- [ ] Burn the flash-voltage eFuse; reset test with the camera fitted.
- [ ] Self-test passes: power, I2C chips, display, camera power polarity (Q1), PDM mic, speaker.
- [ ] Ethernet link and multicast between two boards.
- [ ] Audio both ways (`talk` on USB).
- [ ] Measure the real speaker→mic delay and echo; compare with the simulator.

### Phase 1: a usable wall unit

Goal: one classroom unit that a teacher can use.

- [x] TM1637 display and buttons through the TCA9555; room setup on the panel.
- [x] Auto-tune analysis in a low-priority task.
- [x] Room number change moves the unit to its new IP at once.
- [ ] Confirm the button → function mapping (`firmware/src/board/Panel.h`) against the case labels.
- [ ] Both mics; try ESP-SR (echo cancelling, noise suppression, auto gain). Keep NLMS as the fallback.
- [ ] Audio sent ahead of video in each unit (the network simulation showed this matters).
- [ ] Watchdog and automatic restart; log the reset reason.
- [ ] 3D-printed case with separate speaker and mic chambers; re-run auto-tune in the case.
- [ ] Gym test: two or three units in one room.

### Phase 2: ready to install

Goal: safe to put on the school network.

- [x] **Network command key** (`netkey`): signed commands, replay-proof, set over USB only.
- [x] **Firmware updates over USB only (decided).** `firmware/tools/update-unit.bat`:
  - flashes the newest release,
  - checks the unit comes back with the new version and the same room,
  - keeps all settings, and logs every update.
  - Every fix still means visiting each room, so get the firmware stable during the pilot before the full install.
- [x] **Setup tool** for new units: eFuse, room number, web password, network key and self-test in one command.
- [x] **Web interface on each unit** (`http://10.42.20.14`), password protected. See [firmware/README.md](../firmware/README.md#web-interface).
  - Still to do: move the first-login password check out of the audio task (it can cost one 20 ms frame).
- [ ] Wi-Fi credentials in the setup tool (when Wi-Fi is brought up).
- [ ] Intercom VLAN with IGMP snooping, audio priority (DSCP), and a gigabit port for the NVR.
- [ ] Opus codec (cuts audio bandwidth about 10×).
- [ ] Pilot: 3–5 rooms for a few weeks.

### Phase 3: office console

- [x] PC app (Qt, like the simulator): room grid with status, all-call, talk to one room, signed commands, open a unit's web page.
- [x] Health view: offline units in red, never-heard units listed from a rooms file, battery units highlighted.
- [ ] Alarm button that starts emergency mode (needs Phase 5).
- [ ] Listen-in. Needs a privacy decision first: it would let the office hear a room without anyone pressing TALK.
- [ ] Show failed camera checks and units that need auto-tune.

### Phase 4: portable units

- [ ] Wi-Fi alongside Ethernet; switching under 1 s.
- [ ] Wi-Fi relay: portable units unicast to a wired unit.
- [ ] AXP2101 battery level on the display and the console.
- [ ] Check parking-lot Wi-Fi coverage (may need an outdoor access point).

### Phase 5: emergency mode and camera

- [ ] Camera driver; cover servo; "camera live" light.
- [ ] Cover self-test: snapshot of the printed room number compared with a reference.
- [ ] MJPEG/RTSP stream to the NVR; video limited so audio stays clean.

### Phase 6: later

- [ ] Pan/tilt person tracking and Class 2 laser.
- [ ] LoRa: alerts and status, 3rd-floor repeater.
- [ ] LoRa weather stations with a radiation sensor (separate project).
- [ ] Next board spin: move camera D1/D2 off GPIO45/46; consider moving the buttons to the TCA9555.

## Suggested additions

Not in the plan yet, roughly by value to the school.

| Idea | Why |
|---|---|
| Lockdown / all-clear tones | A distinct sound plus a display code ("LOC") that everyone recognises, sent from the office or any unit. |
| Fire alarm / bell system input | Mute the intercom or start emergency mode from a dry contact on a MISC input. |
| Emergency audio recording at the console | A record of what was said, for after-action review. Check school policy first. |
| Call-the-office button | A single press that rings only the office, not the whole school, for non-urgent calls. |
| Time sync (NTP or from the console) | Timestamps in logs and recordings. |

## Risks

- **Echo cancelling in the gym** may still need ducking (half duplex) even with ESP-SR.
- **Ethernet and Wi-Fi on the same subnet** is untested in lwIP.
- **CPU budget** with ESP-SR, Opus and the camera running at once is unknown until measured.
- **Camera on strapping pins** depends on the eFuse step until the next board spin.
- **USB-only updates** mean a bug found after install needs a visit to every room. The pilot phase is there to catch these early.
- **I2C bus load:** the panel's key scan uses a few ms of I2C every 30 ms. Fine now, but measure it once the AXP2101 and camera share the bus.
- **Office PC clocks:** signed commands are refused if the sending PC's clock runs behind the last command a unit accepted.
