# Routing handoff (22 Nov 2026)

State of the single-sided board and how to pick the work up in a fresh session. Details of each step are in
`single_sided_layout.md`.

## In the repo now
- Hand-routed and protected: both RF feeds with fence vias, the camera bus (all lines on B.Cu except VSYNC, SCL,
  SDA), Ethernet line side, W5500 TX/RX pairs, USB D+/D- with stitching vias.
- Router-routed: W5500 local nets, PoE 5 V output, Ethernet SPI/control lines, U20, U21, SD card (14 of 16).
- 71 connections open after the whole-board run (`all.sh WA N1`, round 6, board `WA6S`); it was 376 before.
  DRC has no errors from the routing; the remaining errors are placement items that were
  already there (test-point clearances, U19 dual-net pads, parts in the camera heatsink area, BT1 cut-out).
- The run did not change any hand-routed net (checked item by item against 19ac70b) and moved no parts.
  Totals: F.Cu 2.47 m, B.Cu 1.32 m, In2 0.74 m of signal track, In1 none; 552 vias, all 0.6/0.3 mm.
- Open by area: 38 at U18 (PMU), 16 at U7, 9 on +3V3 / COM / VBUS, 3 at U21 and J5, 5 test-point links
  (TP60, TP61, TP37, TP65 on purpose; TP35 was routed before the run and is open again).

## U7 bottom row (pins 46-56), 22 Nov 2026
- Y1, C3, C6 and R1 moved 1.0 mm down to open the area under the bottom pin row.
- Routed: OSC_IN, OSC_OUT, the C3/Y1 node and the crystal grounds; I2C SCL and SDA straight to J3 on F.Cu;
  ETH.CS and ETH.SCLK through vias at (120.55, 109.45) and (122.07, 109.6) onto In2. C11's ground pad and U21
  pin 21 (ground) got their own vias after the new tracks cut the top pour that fed them.
- Pin 47 (SD.MOSI) and pin 52 (SD.CLK) escape upward: a short stub from the pad's inner end to a via at
  y = 107.1, between the pin row and the exposed pad, then In2 west to the SD card (router, about 57 mm each).
  These two vias are 0.15 mm from the pins and the pad, an exception to the 1 mm via keep-off. U7's bottom-side
  copper pad (pad 57 on B.Cu) was trimmed from 4.5 to 4.3 mm on this board to make room; the top pad is unchanged.
  The SD card keeps its own bus, separate from the W5500.

## Whole-board run on VM105 (finished; `ssh kicad-vm`, folder `~/pcb/ss`)
- `all.sh WA N1`: six whole-board rounds (route, clean, stitch ground islands, rip-up after rounds 2 and 4).
  Progress is in `all.out`; it prints `ALLDONE` at the end. It cannot delete or rip the hand-routed nets.
- Result boards: `WA1S` ... `WA6S` (one per round), each with its own `drc.json`. The last finished round is the
  one to take. To bring it into the repo:
  `scp kicad-vm:pcb/ss/WA6S/ESP32S3_Audio_Cam_POE.kicad_pcb .`
  then check in KiCad and commit.

## Known hard spots (finish by hand in KiCad or in a new session)
- U7 pins with no track before this run: 2, 3 (3.3 V), 4 (EN), 5 (BOOT), 7, 10, 11, 12, 13, 14, 28 (GPIO26, leave
  open), 29 (VDD_SPI), 30-35 (flash), 46 (3V3_RF), 47 (SD.MOSI), 49, 50 (I2C), 52 (SD.CLK), 53, 54 (crystal),
  55, 56. Parts sit 0.4-1.4 mm from the right and bottom pin rows, so those pins need a hand fan-out like the
  one drawn for the top row (see "Camera bus, routed by hand").
- ETH.MOSI / ETH.MISO at U7 pins 9 / 8; FLOOR.1/2/8 into U21; PMU.IRQ (U21 to U18).
- Test-point links left open on purpose: TP60, TP61, TP37, TP65.

## Rules that must hold
- No signal tracks on In1 (ground plane). No pour inside the "PoE isolation" area; only line-side nets inside it.
- Vias 0.6/0.3 mm, tracks 0.2 mm minimum, 0.127 mm clearance. Keep vias about 1 mm away from U7 and U18 pads.
- Firmware pin map: `firmware/src/board/Pins.h` matches the schematic as of commit 8696399; not compiled.
