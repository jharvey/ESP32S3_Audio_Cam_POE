# Intercom firmware roadmap

Every firmware feature, built or planned, in release order. The design detail is in
[firmware_plan.md](firmware_plan.md); how to build, use and tune it is in [firmware/README.md](../firmware/README.md).

Last updated: 23 Nov 2026.

## Releases and branches

| Release | Branch | What it is |
|---|---|---|
| R0 Foundations | (done, on `single-sided` and earlier) | Intercom core, PC simulator, office console, tools, network load test |
| R1 Intercom | `fw/r1-intercom` | A PoE intercom: hold TALK and every unit hears you, wired or Wi-Fi |
| R2 Camera | `fw/r2-camera`, cut from R1 | Camera, cover servo, emergency video |
| R3 LoRa | `fw/r3-lora`, cut from R2 | LoRa alerts and repeaters; role still to define |
| Later | | Tracking, laser, Opus and ESP-SR, encryption, console extras |

Each release branch is cut from the one before, so it starts from working code. A fix found on a later branch
goes back to the earlier one first.

Status: **done** = built and tested (in the simulator unless it says hardware), **code** = written and compiling,
not run on a board, **new** = to write, **decide** = needs a decision first.

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
| 1.6 | NLMS echo cancelling on the board | done (sim) / code |
| 1.7 | Watchdog and automatic restart; log the reset reason | new |
| 1.8 | Confirm the button mapping (`Panel.h`) against the case labels | new |

### Network, power and addressing
| # | Feature | Status |
|---|---|---|
| 1.9 | Room number = IP address, fixed IP (no DHCP wait): 10.42.&lt;floor·10 + letter&gt;.&lt;room % 100&gt;, so 214 → 10.42.20.14 and 299b → 10.42.22.99. Kept as built (decided 23 Nov); the 10.42 prefix can be changed over USB | done (sim) / code |
| 1.10 | Setup over USB: room number, IP prefix, Wi-Fi name and password, stored in flash (panel setup stays) | code (room) / new (rest) |
| 1.11 | Wi-Fi brought up with the same IP; Wi-Fi units hear and talk like wired ones (multicast first, wired relay if too lossy) | new |
| 1.12 | Link rules. PoE power and Ethernet link: wired. PoE power, no Ethernet link: Wi-Fi, display alternates room and `E1`. No PoE (battery): Wi-Fi. `E2` if Wi-Fi is down too. Switch within 1 s | done (logic) / new (board) |
| 1.13 | AXP2101: power source, battery %, charging in the status beacons; `Lo` on the display | new |
| 1.14 | Check parking-lot Wi-Fi coverage (may need an outdoor access point) | new (site) |

### Updates and install
| # | Feature | Status |
|---|---|---|
| 1.15 | Firmware updates over USB (`update-unit.bat`): newest release, checks version and room after, keeps settings, logs | code |
| 1.16 | Firmware updates from SD card: `firmware.bin` applied at boot, then renamed | new |
| 1.17 | Intercom VLAN with IGMP snooping and audio priority (DSCP); separate WPA2/WPA3 Wi-Fi for it | new (network setup) |
| 1.18 | Move the web interface's first-login password check out of the audio task | new |
| 1.19 | 3D-printed case with separate speaker and mic chambers; re-run auto-tune in the case | new |
| 1.20 | Pilot: 3-5 rooms for a few weeks | new |

### R1 decisions
- **Addressing (decided 23 Nov):** keep 10.42 and the built scheme; teachers set the room number on the panel.
- **Gym (decided 23 Nov):** gym units are 299a, 299b, 299c, 299d, set on the panel like any room (the letter is
  the fourth field). Same number = one room group, so they don't play each other back. Up to 4 units; no new code.
- **Floor/room switches (SW1/SW2).** One digit each, so they can't hold a 3-digit room. Proposed: ignore in R1.
- **Display codes.** Proposed: `E1`, `E2`, `Lo` as above.

## R2 Camera (middle)

Covers the old phase 5 and the console's alarm.

| # | Feature | Status |
|---|---|---|
| 2.1 | Camera driver (esp32-camera) on the `Pins.h` pins; camera power through U21 P04; confirm Q1's polarity | new |
| 2.2 | Cover servo through the PCA9685; "camera live" light | new |
| 2.3 | Cover self-test: snapshot of the printed room number compared with a reference; fault to the console | new |
| 2.4 | Emergency mode from the console alarm or a unit's button | new |
| 2.5 | MJPEG/RTSP (VGA, 10-15 fps) to the NVR; audio always sent before video | new |
| 2.6 | Console: alarm button, failed camera checks, units that need auto-tune | new |
| 2.7 | Optional local recording to the SD card during an emergency | new |
| 2.8 | Check the CPU budget with the camera running | new |

## R3 LoRa (part to develop)

| # | Feature | Status |
|---|---|---|
| 3.1 | Decide the role: alerts and status ("lockdown", "214 on battery"); too slow for voice | decide |
| 3.2 | RFM95 driver on the shared SPI bus (CS GPIO5, IRQ GPIO6, reset U21 P05) | new |
| 3.3 | 3rd-floor repeater for the building and parking lot | new |

## Later

| Feature | Notes |
|---|---|
| Opus codec | Cuts audio bandwidth about 10× |
| ESP-SR audio front end with both mics | Beamforming, noise suppression, AGC; NLMS stays as the fallback |
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
| Call-the-office button | One press rings only the office, for non-urgent calls |
| Time sync (NTP or from the console) | Timestamps in logs and recordings |

## Security

1. **Spoofed audio:** anyone on the same network can send an all-call packet. The biggest risk for a school.
2. **Eavesdropping:** audio is plain multicast.
3. **Changing settings:** network commands are already signed with the site key.
4. **Replacing firmware:** not possible over the network (USB and SD card only, which need the case open).

The intercom VLAN (1.17) deals with 1 and 2 for anyone without access to it and is the main protection for R1.
Signing and then encrypting audio come later.

## Risks

- **Echo cancelling in the gym** may still need ducking (half duplex), even with ESP-SR.
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
