# Firmware feature list (23 Nov 2026)

Three releases, one branch each. Each release builds on the one before. The design detail is in
[firmware_plan.md](firmware_plan.md); progress is in [roadmap.md](roadmap.md).

| Release | Branch | What it is |
|---|---|---|
| R1 Intercom | `fw/r1-intercom` | A PoE intercom: hold TALK and every unit hears you, wired or Wi-Fi |
| R2 Camera | `fw/r2-camera` (from R1) | Camera, cover servo, emergency video |
| R3 LoRa | `fw/r3-lora` (from R2) | LoRa alerts and repeaters; role still to define |

Branches are cut from the release before, so each one starts from working code. Fixes found on a later branch
go back to the earlier one first.

## R1 Intercom (first, keep it simple)

Status column: **sim** = done and tested in the PC simulator, **code** = written, not run on a board,
**new** = to write.

| # | Feature | Detail | Status |
|---|---|---|---|
| 1.1 | Bring-up | USB console, TCA9555, PCA9685, TM1637 panel, one PDM mic in, speaker out; self-test lines | code |
| 1.2 | Talk | Hold TALK: the mic goes out as raw PCM (16 kHz, 20 ms frames) on UDP multicast; every unit mixes the 4 loudest talkers and plays them | sim |
| 1.3 | Addressing | Room number = last digits of the IP address. Default 10.10.10.&lt;room&gt;/24, fixed IP (no DHCP wait). Room 1-254 | new (was 10.42.x.y) |
| 1.4 | Setup over USB | Room number, IP prefix, Wi-Fi name and password, set with the USB tool and stored in flash | code (room only) |
| 1.5 | Wi-Fi | Wi-Fi units hear and talk like wired ones. Try multicast first; if it is too lossy, Wi-Fi units unicast to a wired relay unit | new |
| 1.6 | Link rules | PoE power and Ethernet link: wired. PoE power but no Ethernet link: Wi-Fi, and the display alternates the room number with an error code (e.g. `E1`). No PoE (battery): Wi-Fi. Switch within 1 s | sim (failover) / new (rules, error) |
| 1.7 | Power readout | AXP2101: power source, battery %, charging; shown in the status beacons; low-battery warning on the display | new |
| 1.8 | Echo cancelling | NLMS canceller on the board first (already in the core); ESP-SR AFE later (R1.x) | sim |
| 1.9 | Updates | USB (existing tool) and SD card (`firmware.bin` on the card, applied at boot, then renamed). No network updates in R1 | code (USB) / new (SD) |
| 1.10 | Status beacons | Every unit lists the others with link, power and battery (USB console; office console later) | sim |

Left out of R1 on purpose: Opus, ESP-SR AFE, room groups (gym), the floor/room switches, network updates,
audio encryption, the office console's alarm and video.

### R1 decisions still open
- **Room groups (gym).** With room = last digits of the IP, two units in one room can't share a number. Options:
  give them different numbers and a group ID set over USB, or leave groups to R1.x.
- **Floor/room switches (SW1/SW2).** Each is one digit, so they can't hold a 3-digit room number. Options: ignore
  them in R1; use them as an override (floor·10 + room); or leave them unfitted.
- **Error codes on the display.** Proposed: `E1` no Ethernet link on PoE, `E2` no Wi-Fi either, `Lo` low battery.

## R2 Camera (middle)

| # | Feature | Detail |
|---|---|---|
| 2.1 | Camera driver | esp32-camera on the pins in `Pins.h` (D7 = GPIO48, HREF = GPIO47); camera power through U21 P04; confirm Q1's polarity |
| 2.2 | Cover servo | Open and close the lens cover through the PCA9685 |
| 2.3 | Cover snapshot self-test | Low-res snapshot of the inside of the cover (printed room number) compared with a reference; fault on mismatch |
| 2.4 | Emergency mode | From the console or a button: open the cover, light the indicator, stream MJPEG (VGA, 10-15 fps) to the NVR |
| 2.5 | Audio before video | Audio is always sent before video frames (network load test finding) |
| 2.6 | SD recording | Optional local recording during an emergency |

## R3 LoRa (part to develop)

| # | Feature | Detail |
|---|---|---|
| 3.1 | Role | Decide what LoRa is for: alerts and status ("lockdown", "214 on battery"), too slow for voice |
| 3.2 | Driver | RFM95 on the shared SPI bus (CS GPIO5, IRQ GPIO6, reset U21 P05) |
| 3.3 | Repeater | A 3rd-floor unit relays LoRa messages around the building and parking lot |

## Later (after R3)

Opus codec; ESP-SR AFE with two-mic beamforming; room groups; office console alarm and video; signed network
updates; audio encryption; pan/tilt tracking and the laser.

## Security

Risks on the wired network, roughly in order:
1. **Spoofed audio.** Anyone on the same network can send an "all-call" packet. Biggest risk for a school.
2. **Eavesdropping.** Audio is plain multicast, so anyone on the network can listen.
3. **Changing a unit's settings.** Network commands are already signed with a per-site key (`netkey`).
4. **Replacing the firmware.** Not possible over the network in R1 (USB and SD card only, which need the case open).

What to do:
- **Put the intercom on its own VLAN**, with only the units, the office console and the NVR on it. This deals with
  1 and 2 for anyone without access to that VLAN, costs no firmware work, and is the main protection for R1.
- Keep updates to USB and SD card in R1, as planned.
- Later: sign audio packets with the same site key (stops spoofing), then encrypt them (stops listening).
- Wi-Fi units: a separate SSID with WPA2/WPA3 for the intercom VLAN only.
