# Case and JLC cost notes (23 Nov 2026)

Suggestions for the 3D-printed case and for lowering the JLCPCB order cost. Board positions are from
`ESP32S3_Audio_Cam_POE.kicad_pcb` (mm, board origin at the top-left corner, about 100 x 112 mm).

## 3D case

### Sound (matters most for echo)
- **Separate, sealed chambers** for the speaker and for each mic; gasket (foam or TPU) between the case and the
  board around each mic port. Sound reaching a mic through the inside of the case can't be cancelled well.
- **Mic ports over U16 (64.0, 55.8) and U17 (136.7, 55.6)**, top corners, about 1 mm holes with acoustic mesh.
  Check the MSM261DGT003 datasheet for which side its port is on (top or through the board) before placing them.
- **Speaker away from the mics, facing the room.** It plugs into J10 (64.4, 91.1) on the left, near U16. A speaker
  at the bottom centre puts both mics about 100 mm from it (`firmware/tools/echo_predict.py --speaker X,Y`). In a
  classroom the reverb dominates, so this helps less than the sealed chambers (see `echo_prediction.md` on the
  firmware branch).
- **A closed back volume for the speaker** (30-50 cm3 for a 40-50 mm speaker); an open back sounds thin and leaks
  sound into the mic chambers.
- Re-run auto-tune after the board goes in the case (`unit_tool.py tune auto`).

### Front panel
- **Display window** over DISP1 (78.6-116.7 x 50.9-72.7), with a smoked or tinted insert.
- **Button plungers** for PB1, PB2, PB4, PB5 (row at y 78.5), PB7 (73.5, 78.5), PB8/PB9 (103/111.5, 84.2) and
  PB10-PB12 (column at x 73.5, y 59.5 / 65.5 / 71.5). Any of PB1 and PB7-PB12 talks, so one big TALK cap can press
  two or three of them.
- **Hidden SETUP button PB6 (83, 84.2):** no plunger; reachable only with the front cover off.
- **Room number** printed or engraved on the front, and on the inside of the camera cover (the cover self-test
  reads it).

### Camera
- Lens opening in front of FPC1's camera, with the **servo-driven cover** (PCA9685 output) and its closed position
  over the printed room number.
- The camera lies on the bare COM heatsink pad: leave room for a thermal pad, and a short air path.
- An IR-clear window if the IR LED (LED.IR) is used for night images.

### Mounting, heat, access
- Fits a single-gang box or surface mount; the RJ45 (J9) enters from the bottom edge.
- **PoE module U14 and the bridge diodes get warm:** vents low and high on that side, away from the mic chambers.
- Access with the cover off: USB-C J1 (right edge), the microSD slot (left edge), the battery holder BT1 on the back.
- PETG or ASA rather than PLA (warm rooms, sun near windows). Tamper-resistant screws for hallway units.

## JLCPCB cost

1. **Extended parts cost a setup fee each** (about $3 per unique extended part per order); basic parts don't.
   The design has about 60 unique LCSC parts; upload the BOM to JLC to see which are extended, then move passives
   to basic values where the value can change (e.g. one 10 uF size, 0402 basics instead of 0603/0805 where the
   rating allows). Each one moved saves the fee on every order.
2. **Board size:** 100 x 112 mm. JLC's lowest price tiers stop at 100 x 100 mm; trimming 12 mm can cut the bare-board
   price noticeably at small quantities. Worth checking once routing is done.
3. **Keep to JLC's standard 4-layer process:** 1.6 mm, JLC04161H-7628 stackup (their impedance control is free with
   it), lead-free HASL rather than ENIG, through vias only (now enforced at 0.6/0.3 mm). U18's footprint has 16
   thermal holes of 0.2 mm under its exposed pad: check whether JLC treats them as via-in-pad (a paid option on
   4 layers); 0.3 mm holes may avoid it.
4. **Single-sided assembly** is already the case (BT1 is hand-fitted on the back): no second-side setup or stencil.
5. **Through-hole parts** (BT1, the 1 mm and 2.54 mm headers J5-J8, J10-J16) cost extra to assemble at JLC; leaving
   them for hand soldering is cheaper for a pilot run.
6. **Quantity:** price per board drops sharply from 5 to 30-50 boards; for 40 rooms plus spares, order the pilot
   (5) first, then one run of about 50, panelized.
7. **Biggest single parts:** the PoE module (DP9900M, C5380106), the ESP32-S3, the W5500 and the camera
   connector. Compare the PoE module against an Ag9900M-class alternative before the large order.
8. **Test points** are DNP already: no cost.

## Lamp outputs and LEDs (checked 23 Nov)

| Output | Drives | On-board LED |
|---|---|---|
| LED.WH | Q3 -> lamp header J13 | D12 white through 10 kOhm (indicator, ~0.3 mA) |
| LED.IR | Q4 -> lamp header J12 | D13 **IR** (IR26-21C) through 10 kOhm |
| LED.OTH | Q5 -> lamp header J11 | none |
| MISC1-3 | Q6-Q8 -> J17-J19 | D16-D18 **IR** through 10 kOhm |

- Lamps plug into the headers; the AO3401A switches carry several amps. J11 is the call / alarm light (a red or
  amber LED or beacon). J12 can drive an IR illuminator for night vision (850 nm, with a camera module that has no
  fixed IR-cut filter).
- **Check:** D13 and D16-D18 are infrared LEDs used as "output on" indicators at about 0.3 mA. Nobody can see
  infrared, and that current lights nothing. If they are meant as visible indicators, a JLC basic visible LED
  (green or red 0603) fits better; if they were meant to light the scene for the camera, they need to be IR
  illuminator parts with far more current.
- Status LEDs (23 Nov 2026): D3 yellow = Ethernet link, D4 green = Ethernet activity (blinks on transmit and
  receive; the W5500 has one activity output for both), D14 **blue** = charging (KT-0805B, C2293, with R66 now
  1 kOhm so it lights from Vsys), D15 green = PMU power good, D8 green = USB power, D10 red = 3.3 V. D3/D4 were
  wired backwards (the W5500 LED pins are active low: they pull low when lit): now anode to +3V3, cathode through
  R34/R35 to the pin. D9 (WS2812B RGB) is separate and unchanged. These are not the lamp outputs.
- Lamp-output indicators: D12 white (white lamp, J13), D13 IR (IR illuminator, J12), J11 has none. D16-D18 (MISC1-3
  outputs, J17-J19) are now plain green 0805 KT-0805G (C2297, a basic part already on the board) instead of IR LEDs:
  they just show the output is on.
- Output switches (23 Nov 2026): SY6288AAAC current-limited switches instead of bare P-FETs on all seven outputs
  (lamps, MISC, servo power). A shorted output draws at most its limit (0.8-1.6 A) and then cycles on thermal
  shutdown, and the firmware switches it off on the fault flag, so a cable short does not take down the 5 V rail.
  0.6 A parts because the PoE module (DP9900M-5V) gives about 1.8 A at 5 V in total (9 W, 6 W at 70 C). Rotary
  switches SW1/SW2 removed (room number is set on the panel); their expander inputs read the fault flags.
