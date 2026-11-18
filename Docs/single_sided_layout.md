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
