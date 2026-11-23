# Single-sided layout (branch `single-sided`)

Goal: every JLC-assembled part on one side, on a 4-layer board, with all v0.1b features kept, plus a room-number
switch. Started from `kicad10-port` at 981c517.

## What changed

| Area | Change |
|---|---|
| Assembly | All SMD parts on the top (front) side. 93 former bottom parts were moved to the top and re-placed. |
| Battery holder | BT1 (18650 holder) is on the **back** and is meant to be hand-fitted (it is the only part there). It must be removed from the JLC BOM/position file, or JLC will quote a second side. |
| Floor and room | New SW1 (floor) and SW2 (room on that floor): Nidec Copal SH-7010 style 10-position BCD rotary switches (`Button_Switch_SMD:Nidec_Copal_SH-7010B`). They connect to U21 (TCA9555) port 1, which was unused. No LCSC number yet. |
| Test points | The one-pin 1.00 mm header test points (PMU rails and others) are now 1 mm SMD test pads (`TestPoint:TestPoint_Pad_D1.0mm`). Same nets, still easy to solder a wire to, and they no longer block the inner and bottom layers. |
| Camera breakout | The camera FPC breakout is gone: net ties NT1-NT22 are now plain wires and their 49 test pads (TP10-TP14, TP86-TP129) are deleted, so each FPC1 pin goes straight to its circuit. The unused FPC1 pins (1, 23, 24 and the two mounting tabs) have no-connect flags. This removed about 70 hard-to-route connections around FPC1. To re-map pins for a different camera, cut and jumper the traces instead. |
| Board | 100 x 112 mm before routing (it may grow), 4 layers: Top / GND / VCC / Bottom. COM pour on Top, Bottom and GND. Solid +3V3 plane on VCC (PMU.Vsys is routed, not a plane split). Copper-pour keepouts at both chip antennas. |

| Camera | FPC1 has a clear 20 mm wide lane from its cable opening to the left board edge, between CN1 and CARD1. An OV2640 on the common 21 mm flex lies flat in the lane, lens up, on a bare-copper COM pad (17 x 15 mm, no solder mask, 56 thermal vias to the GND plane) that works as its heatsink: stick it down with a thermal pad. A longer flex can leave the board edge instead, or fold back over a separate heatsink. The silk "CAMERA HEATSINK (COM)" marks the pad. |
| Ethernet | J9 (RJ45) now sits on the bottom edge with the cable entering from the bottom, right next to the T1 magnetics (much shorter MDI pairs than before). J1 USB-C, U1 and U3 moved right to make room. |
| Mounting holes | H1-H3: M3 (3.2 mm, non-plated) at the left edge below the ESP32, top-right and bottom-right corners. They are board-only (not in the schematic or BOM). The top-left corner is the antenna keepout and the bottom-left is the PoE module, so there is no fourth corner hole. U17 moved 8 mm left to clear H2 (mic spacing is now about 75 mm). |
| Spare IO pads | TP132-TP141, 1 mm pads to solder a wire to (see below). |

The schematic changes are the two switches, their labels, a note, the test-point footprint fields, the spare IO
pads, and the removed camera breakout (net ties and their test pads). Nothing else in the circuit changed.

USB-C (J1) stays on the bottom edge next to its power parts. The ESP32 is in the top-left corner and the left edge
near it is taken by H1, CN1 and the camera lane, so there is no edge spot near U7. The ESP32-S3 USB is full speed
(12 Mbit/s), which tolerates the roughly 100 mm route.

### Spare IO / expansion pads

U7 (ESP32-S3R8) has no free GPIO: GPIO33-37 belong to its in-package octal PSRAM and must stay unconnected. The
spare IO that is easy to bring out comes from the I2C expanders:

| Pads | Signal | Notes |
|---|---|---|
| TP132-TP136 | U20 PCA9685 LED11-LED15 | PWM outputs (servo/LED/FET drive), set over I2C |
| TP137 | U21 TCA9555 INT | open drain, active low; add a pull-up if you use it |
| TP138-TP141 | I2C SDA, SCL, +3V3, COM | a row next to the servo headers for adding another I2C device |

They are DNP test points, so JLC places nothing on them.

### Floor and room switches

- SW1 = floor (0-9), SW2 = room on that floor (0-9). Each switch's 1/2/4/8 outputs go to U21 P10-P13 (nets
  FLOOR.1-8) and P14-P17 (nets ROOM.1-8). The switch common goes to COM.
- The signals are active low: read port 1, invert it, then floor = P13..P10 and room = P17..P14.
- **Check:** the switches need pull-ups on port 1. I believe the TI TCA9555 has internal 100 kΩ pull-ups (the
  TCA9535 does not), but confirm this in the datasheet. If it doesn't, add 8 x 100 kΩ to +3V3.
- Suggested IP scheme: static `x.y.z.(100 + 10 x floor + room)`, with floor 0 / room 0 meaning DHCP. The firmware
  should also show the room number (for example "2-7" or "207") on the 7-segment display at boot, so whoever swaps
  a unit can confirm the setting.

## Layout

| Region | Parts |
|---|---|
| Top-left corner | ANT1 (ESP32 chip antenna) with a no-pour keepout; U7 ESP32-S3, U13 flash and Y1 next to it, so the RF feed is short |
| Top centre | DISP1 3-digit display with U11 TM1637; the button row PB1/PB2/PB4/PB5/PB3 underneath |
| Top corners | U16 and U17 mics, about 80 mm apart (good spacing for the two-mic beamforming) |
| Right | U19 LoRa module; ANT3 on the right edge with a keepout; servo headers J5-J8 stacked on the right edge; U20 PCA9685 and the lamp FETs inboard |
| Centre | U18 AXP2101 PMU block; FPC1 camera connector |
| Left edge | H1; CN1 audio jack; the camera lane and heatsink pad; CARD1 microSD; BOOT1 and RST2 below them |
| Bottom | U14 PoE module, D5/D6 bridges, D7 TVS, T1 magnetics, U9 W5500, J9 RJ45 facing down, U1 buck, J1 USB-C, H3 |
| Lower middle | SW1 (floor) and SW2 (room) switches next to U21 |

### Things to check in KiCad before ordering

1. **J1 USB-C orientation.** J1 was a bottom part. After flipping it, I rotated it so the opening should face the
   bottom edge. Check it in the 3D view.
2. **FPC1 and camera.** Checked against the Union Image ZY-XSF3M2640B drawing (the usual 24-pin OV2640 module):
   its gold contacts are on the bottom of the flex, opposite the lens, with the stiffener on the lens side. FPC1
   (AFC01-S24FCC-00) is bottom-contact, so the flex goes in contacts down and the camera lies flat with the lens up.
   Pin 1 lands on pad 1 and the pinout (1 NC, 2 AGND, 3 SDA, 4 AVDD 2.8 V, ... 24 Y0) matches the schematic.
   Confirm on your own module that the contacts are on the side opposite the lens.
3. **ANT1 and ANT3 footprints.** In both footprints, pad 1 (feed) overlaps pad 3 (COM), which DRC reports as a
   short. Check against `Docs/C2925417.pdf` and `Docs/C5370061.pdf`.
4. **Edge connectors.** CN1, CARD1, J9 and J1 sit at the board edge. Check how far each should overhang the outline.
5. **JLC rotation.** The `JLCPCB Rotation Offset` fields are unchanged. Check the flipped parts (especially U1, D9
   and the mics) in JLC's placement preview.
6. **Parts moved for routing (22-23 Nov).** Around U7: Y1, C3, C6, R1 1 mm down; C30, C23 1 mm right. Around U18:
   the C79-C89 cap row 1.2 mm up and spread out, their test points into two rows on the back, C92 and C93 turned
   sideways, C73 flipped, C76, C90, D15, L5 and C96 nudged, C91 and C94 moved out of the way. Check the new
   courtyard overlaps (C92/U18, C73/L5/C75, R66/L5) and that the decoupling still sits close enough to its pins.
   Routing status and what is still open: `routing_handoff.md`.

## USB stitching, Ethernet SPI, U20 and U21 (22 Nov 2026)

- **USB stitching vias.** 27 ground vias (0.6/0.3 mm) along both sides of the D+/D- pair at about 2.5 mm pitch,
  0.95 mm or more from the pair centre, placed wherever pads, tracks and keep-outs allow.
- **Ethernet SPI and control lines** (zone-aware router, these nets only): ETH.SCLK, ETH.CS, ETH.INT_EXP and
  ETH.RST_EXP are complete; ETH.MOSI and ETH.MISO reach the LoRa module and the W5500 resistors but not U7 pins
  9 and 8 (see below).
- **U20 and U21** (router restricted to connections touching these two chips, including their supply and ground
  pins): 42 of 46 connections.
- The router was not allowed to delete any hand-routed net; all of them are still complete and DRC shows no new errors.

Open after this step:

| Net | Where it stops | Why |
|---|---|---|
| ETH.MOSI, ETH.MISO | U7 pins 9, 8 (right side) | R99, R100, C30, C23 sit 1.4 mm from the pins; needs a hand fan-out like the top row |
| FLOOR.1, FLOOR.2, FLOOR.8 | SW1 to U21 pins 13-18 | not analysed yet |
| PMU.IRQ | U21 pin 5 to U18 pin 14 | 60 mm run across the board; search limit reached |
| +3V3 | one pad at U20/U21 | no free spot for a plane via beside it |

Note: this step put 0.12 m of signal track on In2 (the +3V3 plane layer) in 32 segments. In1 is still untouched.

Board totals now: 393 unconnected items; F.Cu 1.20 m, B.Cu 0.71 m, In2 0.12 m, In1 none; 193 vias.

## Rest of the PoE / Ethernet area (22 Nov 2026)

The short local nets were routed with the zone-aware grid router, restricted to these nets only: W5500 crystal,
LED resistors, EXRES, 1V2/TOCAP capacitors, the series resistors on its SPI/INT/RST pins, its test points, every
+3V3A pad (each drops a via into the +3V3A island on In2), and the PoE module's 5 V output (/POE_5V, D11 to
TST-R25). 52 of 56 connections routed; no new DRC errors; none of the hand-routed tracks was touched.

Left open on purpose (all four are links to bottom-side test points):
- Net-(T1-Pad4), Net-(T1-Pad5) to TP61/TP60: these test points sit under the transformer, 1 mm from the
  cable-side tracks, so a via there would break the isolation distance.
- Net-(TP37-Pad1) (U9 pin 40 to TP37) and Net-(U14-ADJ) to TP65: no room for a via beside the pin; TP65 also
  already clashes with BT1's bottom pad.

Still unrouted in this area because they belong to board-wide nets: the ESP32-side SPI lines (ETH.SCLK, MOSI, MISO,
CS, INT, RST from R22-R27 to U7/U21), COM pads, +3V3, VBUS and Vdrive.

Board totals now: 434 unconnected items; 0.78 m of track on F.Cu, 0.36 m on B.Cu, none on In1 or In2; 117 vias.

## W5500 pairs and USB D+/D-, routed by hand (22 Nov 2026)

**W5500 to transformer.** U9 has RX above TX on its pins and T1 has TX above RX, so the pairs must cross.
- RX stays on F.Cu: U9 pins 6/5 to R49/R48 and on through C51/C50 to T1 pins 6/8. RX- runs down a 0.2 mm lane
  between U9's pin ends and R49, then under C53.
- TX leaves U9 pins 2/1 inward, drops through two vias under the chip, runs as a pair on B.Cu and comes up in vias
  beside R46 and R44, 1.5 mm from T1 pins 1/3. No parts were moved for this.
- Both centre-tap nodes are connected. The TX one (R45 to C52/T1 pin 2) runs over the top of R46 and reaches T1
  pin 2 from under the transformer, because C52's ground pad blocks the direct path.
- The +3V3A island on In2 was widened to x = 95.3 so the B.Cu pair stays over one plane.
- To keep 1.75 mm between that centre-tap track and the cable side, the three line-side verticals under T1 moved
  0.6 mm right (x = 99.4, 99.8, 100.2), and one Bob Smith via moved to the right of R60.

**USB.** ESP32 pins 26/25 to U3, R11/R10 and J1, all on F.Cu over the In1 ground plane, no vias, 0.2 mm tracks with
0.2 mm gap. The pair runs up the left side of the LoRa module, across above it at y = 75, up at x = 135 and into
U3 from below. D+ goes to U3 pin 4 and on from pin 3 over the top of U3 to R11. D- goes up a 0.6 mm slot between
TST-R3 and D1 to U3 pin 6 and R10. The two pins of each line on U3 are linked under the part. At J1 the four
D+/D- contacts are joined around the ends of the pads. One ground stitching via (139.2, 69.8) was removed to let
the pair turn. The ESP32-S3's USB is full-speed (12 Mbit/s); the pair is about 60 mm long and not length-tuned.

## Ethernet / PoE line side, routed by hand (22 Nov 2026)

Everything inside the PoE isolation area is routed. The W5500 side of the transformer is not (see the end).

- **Data pairs.** Each pair leaves T1's cable-side pads inward, runs under the transformer and drops straight onto
  the RJ45 pins: TD+/TD- to pins 1/2, RD+ to pin 3, RD- to pin 6. No vias. Going around the outside of T1 would
  have crossed the pairs, because T1's pin order is the reverse of the RJ45's. The verticals stay 1.5 mm from T1's
  W5500-side pads. There is no plane under these pairs (the isolation area has none), so they are not
  impedance-controlled; the runs are 17-21 mm.
- **Centre taps** (TXCM, RXCM) to bridge D6 on F.Cu, 0.3 mm.
- **Spare pairs** (VP on pins 4/5, VN on pins 7/8) join at the RJ45, pick up R60/R59, then go to bridge D5 on B.Cu
  (0.4 mm) through one via at each end, because pins 4/5 sit between the RD+ and RD- drops.
- **Bob Smith node** (R59, R60 to C65, R61, R62) on B.Cu, passing above the RJ45's locating-peg cut-out.
- **Rectifier outputs** D5/D6/D7/C107 to U14 pins 5-8: 0.5 mm tracks on F.Cu, no vias.
- **Frame ground:** the two RJ45 shield tabs are joined on B.Cu; C59/C60/C62/C63/C65 connect to the tabs.
- **Unused transformer taps** (T1 pins 12, 13) go to their bottom test points with a via in each test point.
- The isolation outline gained the strip between T1's underside and the RJ45 pins (96.2-98.6 x 144.6-148.9).

The W5500 side is described in the section above.

## Camera bus, routed by hand (22 Nov 2026)

The camera lines are drawn as a planned bus, not auto-routed. Everything else is still unrouted.

**Why parts moved.** The series resistors sat in rows 0.5 mm apart (0.09 mm clearance for a trace between pads), and
the passives around U7 were 0.4-1.3 mm from its pin rows. U7 is an ESP32-S3R8, so GPIO26 and GPIO33-37 (the side
facing the camera) belong to the in-package PSRAM and cannot be used.

**Pin assignment (U7).** Only six free pins can reach the camera on the top layer without crossing a fixed net.
They carry MCLK (pin 27), D7 (36), HREF (37), VSYNC (43), SCL (44), SDA (45). The other eight lines use the top pin
row: D2 (15), D1 (16), D3 (17), D0 (18), D4 (19), PCLK (21), D5 (23), D6 (24). The nets they displaced moved to
freed pins: SD.MISO (6), SPK.DIN (7), ETH.MISO (8), ETH.MOSI (9), LORA_CS (10), ETH.SCLK (51, GPIO45).

**Parts re-placed.**
- R88-R95, R97, R98: one column at x = 96.25, rows 0.8 mm apart with wider gaps where a line passes straight through.
- R19, R18, C21 (PWDN/RST parts), R17, R16 (I2C pull-ups), TST-R7/8/9 (moved below the bus to y = 111.5).
- At U7: C24, C25, R96, C22 moved up and away from the top pin row; L2, TST-R5, C11 moved down 0.95 mm and J3 down
  1.3 mm to open three trace lanes under the bottom pin row.

**Routing.**
- FPC1 to the resistor column: every line fans up at 45 degrees from the 0.5 mm pin pitch.
- D0-D6 and PCLK drop to B.Cu through a via right after their resistor, run as one 8-trace bus at 0.4 mm pitch
  (y 94.2-97.0), and come up in a single via row at y = 97.6, 3 mm above U7's top pins. The top pins fan out to that
  row so each via sits at least 0.55 mm from its neighbours.
- MCLK, D7 and HREF stay on F.Cu in the corridor above the camera regulators; VSYNC, SCL and SDA stay on F.Cu below them.
- B.Cu runs over the +3V3 plane (In2); the "ESP32 3V3 island" zone was removed so the bus does not cross a plane split.
- The fan-out stubs for the non-camera pins between the camera pins (USB D+/D-, MIC.SCK, the 3.3 V pin) are drawn too.

| Net | Length | Vias |
|---|---|---|
| D0-D6 | 29.6-38.0 mm | 2 each |
| PCLK | 37.4 mm | 2 |
| D7, HREF | 23.6, 23.8 mm | 0 |
| MCLK | 29.0 mm | 0 |
| VSYNC, SCL, SDA | 26.3-29.7 mm | 0 |

Data-to-clock length spread is about 14 mm (roughly 0.1 ns), not length-matched.

Not done / to check: FPC1's power and ground pins (they leave to the left, under the connector); the pull-up rail
Net-(R16-Pad1); /GPIO26 to TP15 (not usable on the R8 chip); pin map in Pins.h updated but not compiled.

## Zones (22 Nov 2026, before signal routing, for review)

Already present: COM pour on F.Cu, B.Cu and In1 (ground plane), +3V3 plane on In2, both over the whole board.

Added:

| Zone | Layers | Outline (mm) | Purpose |
|---|---|---|---|
| PoE isolation (rule area, no pour; tracks, vias and pads allowed) | F.Cu, In1, In2, B.Cu | 14-point outline: under the cable side of T1 from x 98.6, right to 129.2 (just past U14 pins 5-8), down to the board edge, left to J9 and the Bob Smith parts (to x 85.0) | keeps the ground and +3V3 planes out from under the line side |
| Vsys island, net /PMU.Vsys | In2 | 64-80 x 146-158 | all 17 Vsys pads sit above it |
| 3V3A island, net +3V3A | In2 | 73-94 x 132.5-143.5 | all 13 +3V3A pads (W5500) sit above it |
| ESP32 3V3 island, net Net-(U7-VDD3P3_RTC) | In2 | 117-129 x 97-110 | all 11 pads of the ESP32's filtered 3.3 V rail sit above it |

The islands have priority 2 over the +3V3 plane (priority 1) and are not connected to anything until routing adds
vias, so DRC lists them as isolated copper for now (16 notes). No +3V3 pad that needs the plane lies inside an island
except one at U7.

Line-side nets (inside the cut-out): /W_TD_P, /W_TD_N, /W_RD_P, /W_RD_N, /W_VP, /W_VN, /W_TXCM, /W_RXCM,
Net-(C65-Pad2), Net-(C68-Pad1), Net-(D5-+), Net-(D5--), /FGND1.

Open points found while drawing the cut-out:

- C61, C64, D11 and TST-R25 (the PoE module's 5 V output parts, nets /POE_5V, COM, VBUS) sit inside the line side,
  between D6 and U14 pins 7/8, about 1 mm from Net-(D5--). They belong on the output side of U14 (pins 1/2, x = 140).
- C59, C60, C62, C63 (100 nF) connect /FGND1 to COM, and C68 (100 nF) connects the Bob Smith node to COM. These
  bridge the isolation barrier, so they need a voltage rating to match (typically 2 kV parts); the BOM values were
  not checked.
- BT1's B.Cu pad for Net-(J14-Pin_1) and two bottom test points lie under T1 inside the cut-out.
- /FGND1 has no pour of its own; a small frame-ground pour around J9 could be added.

## RF and U20 update (22 Nov 2026, later)

After the right-edge parts were moved down (commit 66f6d82):

- **ANT1** now sits on the right edge below RST2, level with U7 pin 1. Keep-out 145-150 x 106.0-111.9 mm, feed pad
  at the top end, R86 at the bottom end. The feed is one straight 20.7 mm run along y = 107.1 from L3 to the
  antenna, with C15 on it just outside the keep-out and fence vias on both sides. This replaces the ANT1 position
  and feed described in the next section. ANT3 and its feed are unchanged.
- **U20/U21 remap.** Letting signals change chip was tried for the plain on/off outputs (RC.PWR, MISC.OUT1-3,
  CAM.PWR, LORA_RST, ETH.RST): it left 1 crossing, against 0 when every signal stays on its own chip, so nothing
  changed chip. U21 is unchanged. U20 outputs are now: LED0 EXP.PWM15, LED1 RC.PAN, LED2 RC.TILT, LED3 EXP.PWM14,
  LED4 RC.ROT, LED5 RC.OTHR, LED6 EXP.PWM12, LED7 EXP.PWM11, LED8-10 MISC.OUT1-3, LED11 LED.WH, LED12 LED.OTH,
  LED13 EXP.PWM13, LED14 LED.IR, LED15 RC.PWR. This replaces the U20 table below. Pins.h is updated, not compiled.

## RF layout and pin remap (22 Nov 2026)

Done on the hand-placed board (commit 9f7a1d5). Nothing else is routed yet.

### Antenna footprints were wrong

Both antenna symbols had a third pin, wired to ground, whose pad shared the feed terminal of the part. Both parts
have only two terminals (feed, and a far end that goes to ground through a tuning part), so the feed was shorted
to ground. Pin 3 is removed from both symbols, and both footprints now have two full-width pads as in the
datasheets (Docs/C2925417.pdf for ANT1, Docs/C5370061.pdf for ANT3).

### ANT3 (915 MHz, H2U646MHLQ0200) and U19

- Antenna on the right board edge, 0.3 mm in, feed end towards U19's antenna pin.
- Keep-out (no pour, no vias, all four layers): 10 mm deep x 12.8 mm along the edge, as in the layout guide.
- C106 (far-end tuning capacitor) at the top end of the antenna, its ground pad on the keep-out boundary.
- Matching in line directly below the feed pad: C105 (shunt), R85 (series), C104 (shunt).
- Feed about 14 mm long. C13 was nudged 0.4 mm to clear the keep-out.

### ANT1 (2.4 GHz, KH-3216-H0209) and U7

- ANT1 was parked in the top-left corner, about 85 mm from the ESP32's RF pin. It is now on the right board edge
  between ANT3 and RST2, about 30 mm of feed from U7 pin 1.
- Keep-out 5.0 mm deep x 5.9 mm along the edge (datasheet layout 1). R86 (far end to ground) at the top end.
- C12 (shunt) and L3 (series) sit at U7 pin 1; C15 (shunt) sits at the antenna feed, just outside the keep-out.
- C30, C23 and TST-R10 were moved 1.9-3.4 mm to make room for C12/L3 at the pin.
- Not checked: the two keep-outs are 8.5 mm apart on the same edge, and ANT1's keep-out ends 0.7 mm above RST2.

### Feeds

- 0.38 mm track on F.Cu over the In1 ground plane (0.21 mm prepreg, er 4.5), calculated at about 50 ohm. Not
  verified with the board house's impedance calculator.
- A design rule keeps the ground pour 0.4 mm from the feeds so the pour does not lower the impedance.
- 0.6/0.3 mm ground vias fence both feeds at 1.5 mm pitch wherever parts allow, and stitch the ground around both
  keep-outs (54 vias).
- The matching values are the schematic's; they need tuning on a network analyser on the real board.

### Pin remap (schematic, board pad nets and firmware/src/board/Pins.h; no routing)

Nets were re-assigned to pins so that the straight lines from each pin to the nearest pad of its net have the
shortest total length, which removes crossings. Ratsnest crossings at each chip:

| Chip | Before | After |
|---|---|---|
| U7 ESP32-S3 | 125 | 31 |
| U20 PCA9685 | 6 | 0 |
| U21 TCA9555 | 26 | 0 |

Rules used: USB, flash, boot and crystal pins stay. U7 pins 12, 13 and 22 (wired straight to R99, R100, R96) and
pins 49, 50 (I2C, wired straight to J3) stay; most of U7's remaining 31 crossings come from these. GPIO45 and
GPIO46 (strapping pins) now carry only CAM.MCLK and SD.CLK, which the ESP32 alone drives; this also removes the
old boot risk from camera D1/D2 on those pins. On U21 the control lines stay on port 0 and the floor/room
switches on port 1, as the firmware expects.

U7:

| Pin | GPIO | Was | Now |
|---|---|---|---|
| 7 | GPIO2 | CAM.HREF | CAM.D6 |
| 8 | GPIO3 | CAM.MCLK | CAM.D5 |
| 9 | GPIO4 | SD.MISO | CAM.PCLK |
| 10 | GPIO5 | SD.CLK | CAM.D4 |
| 11 | GPIO6 | SD.MOSI | LORA_IRQ |
| 14 | GPIO9 | ETH.MOSI | MIC.CLK |
| 15 | GPIO10 | LORA_IRQ | ETH.MISO |
| 16 | GPIO11 | LORA_CS | ETH.MOSI |
| 17 | GPIO12 | ETH.MISO | ETH.SCLK |
| 18 | GPIO13 | ETH.SCLK | LORA_CS |
| 19 | GPIO14 | ETH.CS | SPK.DIN |
| 21 | GPIO15 | MIC.CLK | CAM.D2 |
| 23 | GPIO17 | CAM.D6 | CAM.D1 |
| 24 | GPIO18 | CAM.D7 | CAM.D3 |
| 27 | GPIO21 | SPK.DIN | CAM.D0 |
| 36 | GPIO47 | CAM.SDA | CAM.D7 |
| 37 | GPIO48 | CAM.SCL | CAM.HREF |
| 43 | GPIO38 | CAM.D5 | CAM.SCL |
| 44 | GPIO39 | CAM.PCLK | CAM.SDA |
| 45 | GPIO40 | CAM.D4 | SD.MISO |
| 47 | GPIO41 | CAM.D0 | SD.MOSI |
| 48 | GPIO42 | CAM.D3 | ETH.CS |
| 51 | GPIO45 | CAM.D1 | CAM.MCLK |
| 52 | GPIO46 | CAM.D2 | SD.CLK |

U20:

| Pin | Output | Was | Now |
|---|---|---|---|
| 8 | LED2 | RC.TILT | EXP.PWM15 |
| 9 | LED3 | RC.ROT | RC.TILT |
| 10 | LED4 | RC.OTHR | RC.ROT |
| 11 | LED5 | LED.WH | EXP.PWM14 |
| 12 | LED6 | LED.IR | RC.OTHR |
| 13 | LED7 | LED.OTH | EXP.PWM12 |
| 17 | LED10 | MISC.OUT3 | EXP.PWM11 |
| 18 | LED11 | EXP.PWM11 | MISC.OUT3 |
| 19 | LED12 | EXP.PWM12 | EXP.PWM13 |
| 20 | LED13 | EXP.PWM13 | LED.WH |
| 21 | LED14 | EXP.PWM14 | LED.OTH |
| 22 | LED15 | EXP.PWM15 | LED.IR |

U21:

| Pin | Port bit | Was | Now |
|---|---|---|---|
| 4 | P00 | TM.CLK | PWM.OE |
| 5 | P01 | TM.DIO | PMU.IRQ |
| 6 | P02 | PWM.OE | ETH.RST_EXP |
| 7 | P03 | CAM.PWR | ETH.INT_EXP |
| 8 | P04 | LORA_RST | CAM.PWR |
| 9 | P05 | PMU.IRQ | LORA_RST |
| 10 | P06 | ETH.INT_EXP | TM.CLK |
| 11 | P07 | ETH.RST_EXP | TM.DIO |
| 13 | P10 | FLOOR.1 | ROOM.1 |
| 14 | P11 | FLOOR.2 | ROOM.4 |
| 15 | P12 | FLOOR.4 | FLOOR.1 |
| 16 | P13 | FLOOR.8 | FLOOR.4 |
| 17 | P14 | ROOM.1 | FLOOR.2 |
| 18 | P15 | ROOM.2 | FLOOR.8 |
| 19 | P16 | ROOM.4 | ROOM.2 |

The firmware pin map is updated to match but has not been compiled or run.

## Earlier routing status (20 Nov 2026, superseded by the hand placement of 21 Nov)


The grid router now stops at **50 unconnected** items (82 -> 61 -> 50). DRC is clean apart from the known
ANT1/ANT3 pad overlap; schematic parity 0. Via size is unchanged (0.6/0.3 mm) and tracks are 0.2/0.3 mm.

| Group | Count | Notes |
|---|---|---|
| RF feeds (/RF1, /RF1-ANT, /LORA1-ANT) | 8 | left for hand routing on purpose (50 ohm feeds) |
| COM and +3V3 pads | 5 | |
| Power rails (PMU.Vsys 2, Va1, Va4, U18-SW, +2V8 2, +1V5) | 8 | |
| Camera (D1, D2 x2, D4, D5, MCLK, PWDN, HREF, SDA, SCL) | 10 | long U7 to FPC1 links |
| Others | 19 | Ethernet SPI 6, USB 3, I2C.SDA 2, I2S 2, flash QSPI 2, single links |

Copper in use: F.Cu 3.0 m of track, B.Cu 1.4 m, In2 (+3V3 plane) 1.3 m in 576 segments, In1 (GND plane) 0.5 m in
120 segments, 704 vias.

How this board was made (each step on top of the one before):

1. **Parts spaced out from the chips.** 43 small passives that sat 1.0-1.4 mm from the U7 and U18 pins were pushed
   straight out to about 3 mm (82 -> 61 together with steps 3-5).
2. **Parts spread over a wider area.** About 120 small front-side parts within 20 mm of U18 and 14 mm of U7 were
   moved radially outward (factor up to 1.35 at the chip, fading to nothing at the edge of the region, 1.4 mm on
   average) and then pushed apart to at least 0.35 mm. Parts on the RF nets were not moved.
3. **Fan-out first, vias away from the chips.** Each U7/U18 signal pin gets a track that only runs outward to a
   via at least 1.0 mm outside the pad row, vias at least 1.25 mm apart. 81 of 89 pins escape this way.
4. **Both inner layers open to signals** (In2 cost 0.8x, In1 1.0x of an outer layer).
5. **Rip-up.** After every second clean-up round, each blocked connection is routed through existing tracks, the
   tracks in the way are deleted and re-routed in the next round.
6. **No straight-line limit.** The router used to give up when a route strayed more than 15 mm from the box around
   its two ends. It may now search the whole board, so routes can go the long way round or along the board edge.
   This step alone took the 61 board to 51-55; it is slow (about 2 hours per round).

Things to review on this board:

- In1 is not a solid ground plane: 0.5 m of signal track cuts slots in it. Return paths for fast signals crossing
  those slots need a look. In2 (+3V3) carries 1.3 m of signal track and is cut up in the same way.
- The PMU (U18) inductors and capacitors and the U7 decoupling capacitors are now several mm from their pins.
  The switching-regulator loops (LX/SW nodes) should be checked against the AXP2101 layout guidance.
- Short local links were routed after the fan-out and some go through vias; not checked one by one.

## Battery plan: LiFePO4 (proposal, not in the schematic yet)

Requirements: rechargeable, at least 2 hours idle plus 30 min talking plus 30 min speaker, long cycle life, low fire
risk.

### Energy estimate

| Mode | Time | Power | Energy |
|---|---|---|---|
| Idle on Wi-Fi (ESP32-S3 modem sleep, display) | 2 h | ~0.35 W | 0.7 Wh |
| Talking (Wi-Fi TX + mics + AEC) | 0.5 h | ~1.2 W | 0.6 Wh |
| Speaker (MAX98357, moderate volume) | 0.5 h | ~1.5 W | 0.75 Wh |
| Converter losses (~15%) | | | 0.3 Wh |
| **Total** | | | **~2.4 Wh** |

One 18650 LiFePO4 cell (3.2 V x 1.5 Ah = 4.8 Wh) gives about 2x margin. A 26650 cell (3.0-4.0 Ah) would give
3-5x, but needs a different holder.

### Why LiFePO4

- 2000+ cycles to 80% capacity, versus about 500-1000 for NiMH.
- It tolerates being held near full charge on standby. NiMH is worn out by the constant trickle charge a backup
  battery sits on.
- It is the safest lithium chemistry: thermal runaway is far less likely than with Li-ion/LiPo.
- One cell instead of three AA cells.

### Circuit changes needed

The AXP2101 can only charge Li-ion (4.0-4.4 V), so the LiFePO4 version needs:

1. **Charger:** a LiFePO4 linear charger with 3.6 V CV and about 0.5-1 A, with an NTC input (reuse NTC1).
   Candidates: CN3058E (ESOP-8, cheap), TI BQ25070, Microchip MCP73123. Set it to stop at 3.6 V and recharge
   below about 3.35 V, rather than float at 3.6 V, for longer life. Block charging below 0 °C.
2. **Power path:** ideal-diode OR of the 5 V input (Vdrive: PoE/USB) and the cell (for example 2 x LM66100), so the
   load runs from 5 V when it is there and from the cell otherwise.
3. **3.3 V rail:** LiFePO4 runs from 2.5 V to 3.6 V, on both sides of 3.3 V, so U1 (buck only) cannot run from it.
   Replace U1 with a buck-boost (for example TPS63802, or TPS63020 for more current) fed from the OR'd rail.
4. **Speaker amp:** the MAX98357 runs from 2.5-5.5 V, so it can stay on the OR'd rail.
5. **Monitoring:** an INA219 (I2C) on the cell for voltage and current. A LiFePO4 voltage curve is too flat for
   voltage-only fuel gauging, so firmware counts coulombs. The PMU interrupt and power key go away with the
   AXP2101. PB3 can become a normal button on U21.
6. **Protection:** use a protected cell, or add a DW01 + dual FET. Its 2.4 V undervoltage cutoff suits LiFePO4, and
   the charger already limits the top end.

This removes U18, L5-L9 and most of the 2.2 µF / 22 µF PMU capacitors, so the board area roughly balances out.
The layout keeps the AXP2101 block in place until you approve this plan.

## Firmware changes this layout needs

- Read the floor and room from U21 port 1 (see above).
- No GPIO changes: everything else keeps its schematic pins.
