# Intercom firmware roadmap

Every firmware feature, built or planned, in release order. The design detail is in
[firmware_plan.md](firmware_plan.md); how to build, use and tune it is in [firmware/README.md](../firmware/README.md).

Last updated: 23 Nov 2026.

## Releases and branches

| Release | Branch | What it is |
|---|---|---|
| R0 Foundations | (done, on `single-sided` and earlier) | Intercom core, PC simulator, office console, tools, network load test |
| R1 Intercom | `fw/r1-intercom` | A PoE intercom: hold TALK and every unit hears you, wired or Wi-Fi |
| R2 Room calls and timed events | `fw/r2-room-calls`, cut from R1 | The office calls one room (or a few); a room calls the office; scheduled chimes such as lunch |
| R3 Camera | `fw/r3-camera`, cut from R2 | Camera, cover servo, emergency video |
| R4 LoRa | `fw/r4-lora`, cut from R3 | LoRa alerts and repeaters; role still to define |
| Later | | Tracking, laser, encryption, console extras |

Each release branch is cut from the one before, so it starts from working code. A fix found on a later branch
goes back to the earlier one first.

Status: **done** = built and tested (in the simulator unless it says hardware), **code** = written and compiling,
not run on a board, **new** = to write, **decide** = needs a decision first.

## Increments

Smaller steps inside the releases (designs: [release_designs.md](release_designs.md)). Each one ends tested and usable before the next starts; most can be built and
tested in the simulator before boards exist. Item numbers refer to the tables further down.

| Increment | What it delivers | Items | Testable where | Status |
|---|---|---|---|---|
| R1.0 Bring-up | Self-test, two boards talking over PoE, speaker/mic check, measured echo delay | 1.1-1.4 | Board | waiting for boards |
| R1.1 Wired intercom | TALK everyone, room = IP, fault codes, dUP, extra TALK buttons, hidden SETUP, watchdog | 1.5, 1.7, 1.8a, 1.9, 1.13a-b | Simulator, then board | done in code |
| R1.2 Portable and failover | Wi-Fi with the same IP, link rules, battery readout, SD-card update | 1.10-1.13, 1.16 | Simulator (logic), board | code |
| R1.3 Echo in the room | ESP-SR front end with both mics, auto-tune on the board, NLMS fallback | 1.6, 1.6a-b | Board | ESP-SR AEC links |
| R1.4 Echo between rooms | Echo predicted from the board layout and simulated between rooms ([echo_prediction.md](echo_prediction.md)); coupling-map chirp test, automatic room groups, ducking of coupled rooms | 1.6c-e, 1.9a | Simulator first | prediction and measurement done; own-voice NLMS canceller tried and dropped (made it worse); coupling map and ducking next |
| R1.5 Install and support | Console: TALK held, remote mute, faults and dUP in red (done in sim); web status; USB tool for Wi-Fi and groups; code card | 1.8b, 1.13c, 1.17-1.20 | Simulator and PC | next |
| R1.6 Compression | Opus, 16 kHz mono, 20 ms frames, about 24 kbit/s; packet-loss concealment and FEC for Wi-Fi; PCM kept for debugging | 1.21 | Simulator, then board (CPU) | done in code: sim tested (21.5 kbit/s, mixed PCM/Opus units), firmware builds; CPU to measure on the board |
| R1.7 Web console | Office PC browser -> office unit: room grid with status, talk to one room / floor / all with the PC mic, hear the reply | 1.22 | Simulator (localhost), then board (HTTPS) | next |
| R2.0 Room calls | Office calls a room or floor, call tone, answer to office, call-the-office button | 2.1-2.8 | Simulator | |
| R2.1 Timed events | Time sync, schedule on each unit, chimes, days off, quiet hours | 2.9-2.15 | Simulator | |
| R3.0 Video in the simulator | Simulated units stream test video; console video grid; audio ahead of video | 3.4-3.6 | Simulator | |
| R3.1 Camera on the board | Camera driver, Q1 power, cover servo | 3.1-3.2 | Board | |
| R3.2 Emergency mode | Alarm opens covers, streams to the NVR, cover snapshot self-test | 3.3-3.8 | Simulator, then board | |
| R4.0 LoRa protocol | Alert and status messages, repeater, simulated | 4.1, 4.3 | Simulator | |
| R4.1 LoRa on the board | RFM95 driver, 3rd-floor repeater | 4.2-4.3 | Board | |

## R0 Foundations (done)

The intercom logic was built first, in a PC simulator that runs the same code as the firmware.

| Feature | Status | Where |
|---|---|---|
| Push-to-talk all-call: every unit hears whoever holds TALK | done | `firmware/src/core/Intercom.*` |
| Mixing: several talkers at once, loudest 4 | done | `Mixer.*` |
| Jitter buffer per talker, packet format | done | `JitterBuffer.*`, `Packet.*` |
| Echo cancelling: NLMS, 1024 taps, with residual-echo suppression (classroom ≈ 20 dB, gym ≈ 20 dB) | done | `NlmsAec.*` |
| Auto-tune: measures delay, reverb, coupling and noise, then picks settings; runs in a low-priority task | done | `Calibrator.*`, `Tuning.*` |
| Manual override of every audio setting, saved in flash | done | console commands |
| Room groups: units with the same room number (`150a`, `150b`) don't play each other back | done | `Intercom.*` |
| PoE / Wi-Fi failover logic: to Wi-Fi in one 20 ms frame, back to wired after 200 ms | done (logic only) | `Intercom.*` |
| Status beacons: every unit lists the others with link, power and battery | done | `Intercom.*` |
| Room setup on the front panel (hold MODE 3 s, UP/DOWN); new IP at once | done | `RoomSetup.*` |
| Console commands over USB and the network | done | `Intercom.*` |
| Network command key (`netkey`): HMAC-SHA256 signed, replay-proof, set over USB only | done | `Auth.*` |
| Web interface: password protected, off until a password is set over USB, no firmware upload | done | `WebUi.*`, `WebPage.cpp` |
| Front panel driver: TM1637 display and key scan through the TCA9555 | done (against a chip model) | `firmware/src/board/` |
| ESP32-S3 drivers: mic, speaker, Ethernet (W5500), TCA9555, PCA9685, self-test | code | `firmware/src/main.cpp` |
| USB update and first-time setup tool (eFuse, room, web password, network key, self-test) | code | `firmware/tools/` |
| Office console (PC): room grid with status, all-call, talk to a room, signed commands, open web page | done | `firmware/sim/console` |
| Console health view: offline units in red, never-heard units, battery units highlighted | done | `firmware/sim/console` |
| Whole-school network load test (35 units, emergency video) | done | [network_sim_results.md](network_sim_results.md) |
| Pin map matches the board (`Pins.h`, checked 23 Nov) | done | `firmware/src/board/Pins.h` |

## R1 Intercom (first; keep it simple)

Goal: units on a PoE switch talk to each other, with Wi-Fi and battery as the backup. It covers the old
phases 0 (bring-up), 1 (wall unit), 2 (install) and 4 (portable units).

### Bring-up on the first board
| # | Feature | Status |
|---|---|---|
| 1.1 | Bring-up commands: `talk`, `selftest`, `tone`, `version` | code |
| 1.2 | Self-test passes on hardware: power, I2C chips, display, PDM mic, speaker, Ethernet, PSRAM | new (needs a board) |
| 1.3 | Two boards on a PoE switch: Ethernet link, multicast, audio both ways | new (needs a board) |
| 1.4 | Measure the real speaker→mic delay and echo; compare with the simulator | new (needs a board) |

### Talk and audio
| # | Feature | Status |
|---|---|---|
| 1.5 | Hold TALK: raw PCM (16 kHz, 20 ms) on UDP multicast; every unit mixes and plays | done (sim) / code |
| 1.6 | NLMS echo cancelling on the board (the fallback once 1.6a works) | done (sim) / code |
| 1.6a | **Advanced echo cancelling:** ESP-SR audio front end with both mics: echo cancelling, beamforming, noise suppression, auto gain, voice detection. First check it builds under Arduino-ESP32 / PlatformIO (else ESP-IDF) | code: ESP-SR echo canceller (`EspSrAec`) builds and is the default; front end with both mics, NS, AGC next |
| 1.6b | **Auto-tune on the board:** run the built auto-tune with the real speaker, mics and case; pick settings for ESP-SR as well as NLMS; re-run after install | done (sim) / new (board) |
| 1.6c | **Cross-room echo, own voice:** measured in the simulator: your voice comes back at the room-to-room coupling (-20 dB through a wall, -10 dB open door), 55 ms late in the sim (75-100 ms expected on boards). An NLMS canceller with the sent audio as reference made it worse and was removed; options: a decorrelating feedback canceller, or ducking coupled rooms (1.6d) | measured; canceller dropped |
| 1.6d | **Cross-room echo, neighbours' speakers:** a longer canceller tail or extra suppression for room-to-room paths; ducking of nearby units while one talks, if needed | new |
| 1.6e | **Coupling map:** at install, each unit chirps in turn while the others listen; the result shows which rooms hear each other and sets 1.6c/1.6d per unit; shown on the console | new |
| 1.6f | Gym and corridor test with real units: residual echo measured per room and between rooms | new (needs boards) |
| 1.6g | **Noise suppression (gym):** ESP-SR's neural noise suppression and AGC on the sent audio, voice detection so noise isn't sent; a portable suppressor in the simulator so it can be tested with crowd noise | new |
| 1.6h | **Automatic volume:** the unit measures room noise between calls and raises its speaker volume with it, up to a set limit (`volume_db` stays the base) | new |
| 1.7 | Watchdog and automatic restart; log the reset reason | code: audio-task watchdog (3 s), reset reason logged |
| 1.8 | Confirm the button mapping (`Panel.h`) against the case labels | new |
| 1.8a | Extra buttons on the TM1637 key scan (board cc3eacb): PB7-PB12 = TALK (any one talks; PB10-PB12 in a column left of the display), PB6 = hidden SETUP under the front cover; room setup only from SETUP held 3 s. Free for later: K1+SEG7-8, K2+SEG6-8. A D-pad doesn't fit beside the display | done (sim) / code; board placed, not routed |
| 1.8b | No TALK timeout (decided 23 Nov): an emergency may need TALK held a long time. Instead, after about 2 min held the console shows the room as "TALK held", and the office can mute that unit's TALK with a signed command (`mute <minutes>` / `mute off`; the console's button mutes 15 min, which also covers a button that stays stuck). Status flags "TALK held" and "muted" | done (sim) / code |

### Network, power and addressing
| # | Feature | Status |
|---|---|---|
| 1.9 | Room number = IP address, fixed IP (no DHCP wait): 10.42.&lt;floor&gt;.&lt;room on that floor&gt;, so 214 → 10.42.2.14, netmask 255.255.0.0 (decided 23 Nov; was 10.42.&lt;floor·10&gt;.&lt;room&gt;). No letters. The 10.42 prefix can be changed over USB | done (sim) / code |
| 1.9a | Room groups without letters: units that hear each other loudly in the coupling map (1.6e) are one room and don't play each other back; manual override over USB | code: `group 299` over USB (manual); automatic from the coupling map: new |
| 1.10 | Setup over USB: room number, IP prefix, Wi-Fi name and password, stored in flash (panel setup stays) | code: room, Wi-Fi (`wifi <ssid> <pass>`); IP prefix: new |
| 1.11 | Wi-Fi brought up with the same IP; Wi-Fi units hear and talk like wired ones (multicast first, wired relay if too lossy) | code: Wi-Fi with the same IP, default interface follows the link; Wi-Fi audio to test on boards |
| 1.12 | Link rules. PoE power and Ethernet link: wired. PoE power, no Ethernet link: Wi-Fi, display alternates room and `E1`. No PoE (battery): Wi-Fi. `E2` if Wi-Fi is down too. Switch within 1 s | done (sim) / code |
| 1.13 | AXP2101: power source, battery %, charging in the status beacons; `Lo` on the display | code (register map to confirm on the board) |
| 1.13a | Fault display: the room number stays on; every 3 s the code flashes for 0.5 s (`E1` no cable link, `E2` no network at all, `Lo` battery low) | done (sim) / code |
| 1.13b | Duplicate number: a unit ID (from the MAC) in every status beacon. A unit that hears its own number and letter from a different ID alternates `dUP` and the number every second, within about 1 s, until one is renumbered; the console shows it in red | done (sim) / code |
| 1.13c | Code card: the codes printed on the case or a card by the display, and spelled out on the web page and console | new |
| 1.14 | Check parking-lot Wi-Fi coverage (may need an outdoor access point) | new (site) |

### Updates and install
| # | Feature | Status |
|---|---|---|
| 1.15 | Firmware updates over USB (`update-unit.bat`): newest release, checks version and room after, keeps settings, logs | code |
| 1.16 | Firmware updates from SD card: `firmware.bin` applied at boot, then renamed | code |
| 1.17 | Intercom VLAN with IGMP snooping and audio priority (DSCP); separate WPA2/WPA3 Wi-Fi for it | new (network setup) |
| 1.18 | Move the web interface's first-login password check out of the audio task | new |
| 1.19 | 3D-printed case with separate speaker and mic chambers; re-run auto-tune in the case | new |
| 1.20 | Pilot: 3-5 rooms for a few weeks | new |
| 1.21 | **Compression (Opus):** 16 kHz mono, 20 ms frames, VBR about 24 kbit/s, in-band FEC on Wi-Fi. Codec field in the audio header (already there). PCM stays as a debug option; G.722 is the fallback if the S3 runs short of CPU with ESP-SR and the camera. Firmware: libopus (arduino-libopus); simulator: the same libopus built from source | done (sim) / code: `AudioCodec` (PCM, Opus), jitter buffer keeps frames encoded and decodes in order (Opus PLC), `set opus 0|1`, `set bitrate_kbps`, FEC on Wi-Fi. Firmware libopus 1.3.2 (arduino-libopus), simulator 1.5.2 (same bitstream) |
| 1.22 | **Web console:** a page on the office unit for the office PC: room grid (link, power, faults, dUP), pick a room / floor / all, hold Talk to speak with the PC mic (browser -> WebSocket -> office unit -> targeted audio), hear the room's reply. Same login as the unit's web page. Browsers allow the mic only over HTTPS: the office unit serves HTTPS with its own certificate (accept once), or Chrome treats its address as secure | new |

### R1 decisions
- **Addressing (decided 23 Nov):** 10.42.&lt;floor&gt;.&lt;room&gt;. A broken unit is replaced by a teacher setting the room number,
  with no IT help; how (rotary dials or the panel) is still to decide, see below.
- **Gym (decided 23 Nov):** gym units get their own numbers counting down from 299 (10.42.2.99, 10.42.2.98, ...).
  They are grouped automatically from the coupling map (1.9a), so they don't play each other back.
- **Rotary dials (SW1/SW2), open.** The goal is a teacher replacing a unit by setting its room number. Two dials
  give two digits; a room needs three (floor + two-digit room). Options: a third dial in the next board spin (needs
  another I/O expander), dials for the two room digits with the floor set once, a dial position mapped through a
  table shared by all units, or the panel buttons as built.
- **Display codes (23 Nov):** the room number stays the main thing shown; codes flash briefly (1.13a). Codes:
  `E1`, `E2`, `Lo`, `dUP`.

## R2 Room calls and timed events (after R1, before the camera)

Individual addressing: the office talks to one room instead of the whole school. The audio packets already carry a
target room, and the office console's "Talk to selected room" works in the simulator; this release brings it to the
boards and adds what a teacher needs around it.

| # | Feature | Status |
|---|---|---|
| 2.1 | Office calls one room: only that room (all units with its number, e.g. 299a-d) plays the call | done (sim) / code |
| 2.2 | Office calls several rooms, a floor (2xx) or a group from a rooms file | new |
| 2.3 | Call tone before a room call, so the room knows it's for them, and a display code while it lasts (e.g. `CAL`) | new |
| 2.4 | Room answers: TALK during or just after a room call replies to the office only, not the whole school | new |
| 2.5 | Call-the-office button: one press rings only the office, for non-urgent calls; the console shows who's calling | new |
| 2.6 | All-call still wins: an all-call or emergency interrupts a room call everywhere | new |
| 2.7 | Console: room-call history with times, missed calls from rooms | new |
| 2.8 | Privacy: no listening to a room without the room seeing it; listen-in stays out unless the school decides otherwise | decide |
| 2.8a | **Visual call indicator:** a room call pulses the lamp output LED.OTH slowly (red or amber LED) and shows `CAL` until answered; steady glow during an all-call. Flashing at most 2 per second; never white-strobe like the fire alarm | new |

### Timed events
| # | Feature | Status |
|---|---|---|
| 2.9 | Time sync: NTP from the school network, or the office console as the time source; time in logs | new |
| 2.10 | Schedule kept on each unit (pushed from the console, signed), so chimes still play with the office PC off | new |
| 2.11 | Chimes and bells: lunch, start and end of day, class changes; played by all rooms or chosen rooms/floors | new |
| 2.12 | Weekday tables, days off (holidays, snow days) and a one-tap "skip today" on the console | new |
| 2.13 | Chime sounds and short recorded messages stored on the SD card (or sent from the console) | new |
| 2.14 | Priority: an all-call, room call or emergency cuts a chime; a chime never interrupts them | new |
| 2.15 | Quiet hours: no chimes nights and weekends unless the schedule says so | new |

## R3 Camera (middle)

Covers the old phase 5 and the console's alarm.

| # | Feature | Status |
|---|---|---|
| 3.1 | Camera driver (esp32-camera) on the `Pins.h` pins; camera power through U21 P04; confirm Q1's polarity | new |
| 3.2 | Cover servo through the PCA9685; "camera live" light | new |
| 3.3 | Cover self-test: snapshot of the printed room number compared with a reference; fault to the console | new |
| 3.4 | Emergency mode from the console alarm or a unit's button | new |
| 3.4a | **Alarm light:** lockdown / emergency flashes LED.OTH in its own pattern (at most 2 per second) with `ALr` on the display, for anyone who can't hear the call | new |
| 3.4b | **Night vision:** an IR illuminator (850 nm, hundreds of mA) on J12 (LED.IR), switched on for night snapshots and emergency video; needs a camera module without a fixed IR-cut filter | new (hardware choice) |
| 3.5 | MJPEG/RTSP (VGA, 10-15 fps) to the NVR; audio always sent before video | new |
| 3.6 | Console: alarm button, failed camera checks, units that need auto-tune | new |
| 3.7 | Optional local recording to the SD card during an emergency | new |
| 3.8 | Check the CPU budget with the camera running | new |

## R4 LoRa (part to develop)

| # | Feature | Status |
|---|---|---|
| 4.1 | Decide the role: alerts and status ("lockdown", "214 on battery"); too slow for voice | decide |
| 4.2 | RFM95 driver on the shared SPI bus (CS GPIO5, IRQ GPIO6, reset U21 P05) | new |
| 4.3 | 3rd-floor repeater for the building and parking lot | new |

## Later

| Feature | Notes |
|---|---|
| Signed audio packets, then encrypted audio | Stops spoofed all-calls, then eavesdropping |
| Listen-in from the office | Needs a privacy decision first |
| Pan/tilt person tracking and a Class 2 laser | ESP-DL on the unit |
| LoRa weather stations with a radiation sensor | Separate project |

### Suggested additions
| Idea | Why |
|---|---|
| Lockdown / all-clear tones | A distinct sound plus a display code everyone recognises, from the office or any unit |
| Fire alarm / bell input | Mute the intercom or start emergency mode from a dry contact on a MISC input |
| Emergency audio recording at the console | A record for after-action review; check school policy first |

## Security

1. **Spoofed audio:** anyone on the same network can send an all-call packet. The biggest risk for a school.
2. **Eavesdropping:** audio is plain multicast.
3. **Changing settings:** network commands are already signed with the site key.
4. **Replacing firmware:** not possible over the network (USB and SD card only, which need the case open).

The intercom VLAN (1.17) deals with 1 and 2 for anyone without access to it and is the main protection for R1.
Signing and then encrypting audio come later.

## Risks

- **Echo cancelling in the gym** may still need ducking (half duplex), even with ESP-SR.
- **ESP-SR availability:** it's a prebuilt Espressif library; if it doesn't link under Arduino-ESP32 3.x, R1 moves
  to ESP-IDF (bigger change, decided early in R1). The simulator keeps the portable canceller either way.
- **Cross-room echo** is only modelled roughly in the simulator; the real numbers come from 1.6f.
- **Ethernet and Wi-Fi with the same IP** is untested in lwIP.
- **CPU budget** with ESP-SR, Opus and the camera at once is unknown until measured.
- **USB and SD-only updates** mean a bug found after install needs a visit to every room; the pilot is there to
  catch them early.
- **I2C bus load:** the panel's key scan uses a few ms every 30 ms; measure it once the AXP2101 and camera share the
  bus.
- **Office PC clocks:** signed commands are refused if the sender's clock is behind the last command a unit took.
- **One key at a time:** the TM1637 key scan can't see two buttons at once, so there are no real combos (auto-tune
  is UP held 5 s). Moving the buttons to the TCA9555 would fix it in a later spin.
- The old camera-on-strapping-pins risk is gone on the `single-sided` board: GPIO45/46 now carry ETH.SCLK and SD.CLK,
  which only the ESP32 drives.
