# Documentation

The classroom intercom for OVCS: an ESP32-S3 board on PoE with two-way audio, a covered emergency camera, and an
office console. Firmware lives in `firmware/` (PC simulator in `firmware/sim/`).

How the documents are organised (development vs final, per release): [documentation_plan.md](documentation_plan.md).

## For people using and running it

| Document | For |
|---|---|
| [teacher_guide.md](teacher_guide.md) | Teachers: talking, what the display shows, setting a room number |
| [office_guide.md](office_guide.md) | The office: console, room grid, faults, planned room calls and chimes |
| [quick_start.md](quick_start.md) | One page: one unit from the box to talking |
| [code_card.html](code_card.html) | Printable card for beside each unit: buttons and display codes |
| [install_guide.md](install_guide.md) | Installers: network, mounting, first setup, echo tuning, check list |
| [maintenance_guide.md](maintenance_guide.md) | Upkeep: updates (USB / SD), tuning files, replacing a unit, faults |
| Videos | Simulator walkthroughs, see `videos/README.md` (recorded from the PC simulator) |

## Plans and designs

| Document | What |
|---|---|
| [roadmap.md](roadmap.md) | Every feature, built or planned, by release and increment, with status |
| [release_designs.md](release_designs.md) | Designs for the coming increments: Opus, web console, room calls, timed events, camera stream, emergency, LoRa |
| [firmware_plan.md](firmware_plan.md) | The original firmware plan and goals |
| [echo_prediction.md](echo_prediction.md) | Echo predicted from the board layout; echo between rooms, simulated |
| [network_sim_results.md](network_sim_results.md) | Whole-school network load test (35-40 units, emergency video) |

## Development

| Document | What |
|---|---|
| [dev_architecture.md](dev_architecture.md) | How the firmware is put together |
| [dev_protocol.md](dev_protocol.md) | Network packets and command signing |
| [dev_build_test.md](dev_build_test.md) | Building, the simulator, tests, videos |
| [dev_release.md](dev_release.md) | Branches, versions, releasing |

## Hardware

| Document | What |
|---|---|
| [single_sided_layout.md](single_sided_layout.md) | The single-sided board: parts, regions, what to check before ordering |
| [routing_handoff.md](routing_handoff.md) | Routing status and how to continue |
| [case_and_cost_notes.md](case_and_cost_notes.md) | 3D case suggestions and JLCPCB cost reductions |
| [bring-up.md](bring-up.md) | First-board checklist |
| [JLC_rotation_check.md](JLC_rotation_check.md), [U8_removal_plan.md](U8_removal_plan.md) | Older hardware notes |

## Decisions (23 Nov 2026)

| Topic | Decision |
|---|---|
| Releases | R1 intercom, R2 room calls and timed events, R3 camera, R4 LoRa; each split into increments tested before the next |
| Addressing | Room number = IP: 10.42.<floor>.<room>, netmask 255.255.0.0 (214 -> 10.42.2.14) |
| Gym | Four units, one per corner, numbered 299, 298, 297, 296 and joined by a room group (`group 299` on each, or automatically by the coupling test). Units in the group never play each other, and the other three go quiet while one talks, so its microphone does not send their speakers back out as echo |
| Room number setup | Hidden SETUP button under the front cover (held 3 s), then UP/DOWN/MODE; MODE on the front can't change it. The SW1/SW2 dials stay on the board but aren't used |
| Display | The room number stays on; codes flash briefly: `E1` (PoE, no cable link), `E2` (no network), `Lo` (battery), `dUP` (duplicate number, alternates every second) |
| Duplicates | Each unit sends an ID from its MAC; two units on one number both show `dUP` |
| Buttons | PB1 and PB7-PB12 are all TALK (any one talks); 1 kOhm in series with each new button keeps contact current under 1 mA |
| Stuck TALK | No timeout (emergencies need long talks); the office sees "TALK held" and can mute that unit |
| Link rules | PoE + Ethernet link = wired; PoE without link = Wi-Fi + `E1`; battery = Wi-Fi |
| Compression | Opus, 16 kHz mono, about 24 kbit/s; about 100 ms mouth to ear; PCM for debugging |
| Updates | USB and SD card only; no network updates |
| Security | Intercom VLAN first; signed network commands already; signed then encrypted audio later |
| Web console | Office PC (Chrome, Edge, Firefox) on the office unit over HTTPS; 40 rooms |
| Camera stream | RTSP / ONVIF, MJPEG VGA 10-15 fps, AAC-LC stereo from the two mics (G.711 fallback); go2rtc for H.264-only NVRs |
| Gym noise | ESP-SR noise suppression and AGC on sent audio; automatic speaker volume from room noise |
| Lights | Lamps plug into the lamp headers: J11 (LED.OTH, a red or amber JLC LED or beacon) pulses for room calls and flashes (at most 2 per second) for alarms, never like the fire strobe; J12 (LED.IR) can drive an IR illuminator for night vision; J13 (LED.WH) the white lamp |
| Vias | 0.6 mm / 0.3 mm drill minimum, enforced in the design rules |
