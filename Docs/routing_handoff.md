# Routing handoff (23 Nov 2026)

State of the single-sided board and how to pick the work up in a fresh session. The layout itself (parts, regions,
what changed from v0.1b) is in `single_sided_layout.md`.

## In the repo now (`single-sided`, 3efd5d7)
- 52 connections open, no DRC errors from the routing. The DRC errors left are placement items that were already
  there (test-point clearances, U19 dual-net pads, parts in the camera heatsink area, BT1 cut-out), plus new
  courtyard overlaps around U18 (see below).
- Hand-routed: both RF feeds with fence vias, the camera bus (all lines on B.Cu except VSYNC, SCL, SDA), Ethernet
  line side, W5500 TX/RX pairs, USB D+/D- with stitching vias, and the hand escapes at U7 and U18 described below.
- Everything else is router copper from VM105 (`route8.py` / `route9.py`).
- No signal tracks on In1; all vias 0.6/0.3 mm.

## In progress: longest-first re-route (VM105, `~/pcb/ss`)
- Start board: today's board with every router-made track and via ripped up (413 open). Hand copper was found by
  comparing against each router run's input and output, and is kept.
- All hand copper is marked **Locked** in KiCad (`(locked yes)`), on the protected nets and on the 250 hand items
  on other nets (U7 and U18 escapes, crystal, I2C, SD vias). Locked items are also safe from KiCad's own tools.
- Run: `all10.sh WL L0`, six rounds (route, clean, stitch ground islands, rip-up after rounds 2 and 4). Each
  round routes the longest connections first. Progress in `all10.out`, `ALLDONE` at the end; result boards
  `WL1S` ... `WL6S`, each with its own `drc.json`.
- New scripts next to the old ones (which are unchanged): `route10.py` (`ORDER=desc` sorts longest first),
  `rip10.py` (same order; locked tracks are fixed obstacles and never ripped), `fixclr10.py` (never deletes a
  locked item).
- First result: round 1 routed VBUS (89 mm) first, which failed in every earlier run.
- To take a result: copy the best `WL?S` board, run DRC, check the protected nets with `cmp.py`, then commit.

## Trial: kicad-rules-based-router (owner's router, in development)
- Private repo `jharvey/kicad-rules-based-router`. KiCad-native, Python standard library only, run as
  `python -m kicad_rbr <command>`. Test only on copies of the board.
- What it adds over the VM105 router: fan-out (BGA dog-bones), push and shove of unlocked tracks, negotiated
  rip-up, pairs, a triage of which nets it can route, and a KiCad DRC check before writing.
- Trial: `triage <copy> --round all --to copy`, run on a copy of the same 413-open ripped board. Its triage put
  125 nets as easy, 156 doubtful and 8 better by hand. It also listed some locked, already-routed nets as easy
  (camera data lines, USB CC, W_VN/VP), so check the result for duplicate copper on those nets.
- Compare with the VM105 run by open connections, DRC errors, In1 tracks and untouched hand copper.

## U7 bottom row (pins 46-56), 22 Nov 2026
- Y1, C3, C6 and R1 moved 1.0 mm down to open the area under the bottom pin row.
- Routed: OSC_IN, OSC_OUT, the C3/Y1 node and the crystal grounds; I2C SCL and SDA straight to J3 on F.Cu;
  ETH.CS and ETH.SCLK through vias at (120.55, 109.45) and (122.07, 109.6) onto In2. C11's ground pad and U21
  pin 21 (ground) got their own vias after the new tracks cut the top pour that fed them.
- Pin 47 (SD.MOSI) and pin 52 (SD.CLK) escape upward: a short stub from the pad's inner end to a via at
  y = 107.1, between the pin row and the exposed pad, then In2 west to the SD card. These two vias are 0.15 mm
  from the pins and the pad, an exception to the 1 mm via keep-off. U7's bottom-side copper pad (pad 57 on B.Cu)
  was trimmed from 4.5 to 4.3 mm on this board to make room; the top pad is unchanged.
  The SD card keeps its own bus, separate from the W5500.

## U7 right side and U18 fan-out, 23 Nov 2026
- U7 right side: C30/C23 1 mm right; LoRa IRQ/CS lanes east above C30; inner vias at x 123.9 for ETH.MOSI,
  ETH.MISO, SD.MISO, CHIP_PU (In2 under the chip); outer vias for SPK.DIN, CHIP_BOOT.
- U18 top: cap row C79-C89 moved 1.2 mm up and spread to 0.8 mm pitch over the pins; F.Cu fan; one via per rail
  with its test point moved to two rows on B.Cu (TP76 to the right at 75.1, 146.6).
- U18 right: L5 0.15 mm down, C92 turned sideways between the lanes, C73 flipped, C76 moved right, Vo3 via B.Cu,
  CHGLED via B.Cu. C91 parked at (80.6, 151.0) for the router.
- U18 left: C93 sideways between the Vsys lanes, C90 and D15 next to their lanes, C96 0.05 mm down, PB3 via.
  C94 parked at (58.5, 150.5) for the router.
- New courtyard overlaps around U18 (C92/U18, C73/L5/C75, R66/L5); check in KiCad.

## LED change, 23 Nov 2026 (after the WB/WC/WD routing runs were started)

D3 and D4 turned 180 degrees in place (now anode to +3V3 with a via, cathode to R34/R35), a COM track joins C36 to
C37 (C36's ground used to reach the pour through D4), D14 blue C2293, R66 1 kOhm. The routing runs on VM105 started
from the board before this: after taking a routed result, run
`"C:\Program Files\KiCad\10.0\bin\python.exe" tools\pcb_led_fix.py ESP32S3_Audio_Cam_POE.kicad_pcb` on it
(it skips what is already done) and check DRC near D3/D4.

D16-D18 then became green 0805s: `tools\pcb_misc_led_green.py` swaps the footprints, moves /MISC.OUT1 and /MISC.OUT3
(they ran between the old IR pads) below the LEDs, and adds a ground via at the D16/D18 cathodes. On a routed copy it
warns if those two tracks are routed differently. Left: a thermal-spoke warning on J18 pin 2.

## Protected outputs and rotary switches removed, 23 Nov 2026

The seven P-FET output switches (Q2 servo power, Q3 white, Q4 IR, Q5 J11, Q6-Q8 MISC1-3) are now SY6288AAAC
current-limited power switches (SOT-23-5, C111829): 0.6 A, limit 0.8-1.6 A, thermal shutdown, soft start,
EN active high with a 100 kOhm pull-down (off at power-up with no firmware), OCB fault flag. The old gate-source
resistors are the EN pull-downs (now 100 kOhm, to COM through the old 0-ohm links TST-R17/22/23/24/30/31/32, whose
supply end is now COM); the gate series resistors are 1 kOhm. SW1/SW2 (rotary switches) are gone; U21 P10-P16 are
the fault lines /FLT.WH, /FLT.IR, /FLT.OTH, /FLT.M1, /FLT.M2, /FLT.M3, /FLT.RC (TCA9555 internal pull-ups), P17 is
spare (/EXP.P17). Scripts: tools/sch_protected_outputs.py (done), tools/pcb_protected_outputs.py and
tools/pcb_drop_drc_conflicts.py (re-run on a routed copy). After this: unconnected 115 (was 74); new DRC items
only three thermal-spoke warnings on the grounded 0-ohm links. Needs routing: each switch's IN (Vdrive), GND,
OUT, EN, and the seven fault lines to U21.

## Open, needs hand work
- New front-panel buttons PB6-PB12 and their 1 kΩ resistors R109-R115 (commit 73bbe0d): 21 short connections
  near the display (K1/K2, BTNn, SEGn), plus CHIP_BOOT under PB8 (two router segments removed). Not routed yet.
- U18 bottom row (pins 31-40). L9/C97 sit too close. Plan: move L9 and C95 1 mm down and C97 to the left of L9,
  then fan J16/VDCDC5 left, SW down between, and vias for J14, Vsys 34/36, VBUS, IRQ, SCL, SDA.
- SPK.DIN and I2S.WS into U15 (fine-pitch amp; needs a hand fan-out like U7 and U18).
- ETH.MISO/MOSI to the W5500. In2 below U7 is walled off by the SD and flash lines; start the W5500 branch from
  the LoRa module pins instead (U19 pads 10/11 at 120.95, 80.75/82.75, same bus). R24.1/R25.1 (76.4/77.6, 142.29)
  are boxed in by the U9-side vias and the ETH.SCLK/CS diagonals above them: give them a hand escape first, or
  move R22-R27 down about 0.5 mm.
- LORA_CS (lane end boxed in at 130.0, 103.66), FLASH_QSPI_DO (U7 pin 34), RC.PAN.
- Test-point links left open on purpose: TP60, TP61, TP37, TP65.

## Router notes (VM105)
- `route9.py` takes `ANCH="net@x,y,layers;..."` to move a net's end onto a hand-drawn stub end instead of a
  fine-pitch pad. The router fails at 0.4 mm-pitch pins (U7, U18, U15): draw escapes first.
- Earlier whole-board run: `all.sh WA N1` (boards `WA1S` ... `WA6S`), shortest connections first; superseded by
  the longest-first run above.

## Rules that must hold
- No signal tracks on In1 (ground plane). No pour inside the "PoE isolation" area; only line-side nets inside it.
- Vias 0.6/0.3 mm, tracks 0.2 mm minimum, 0.127 mm clearance. Keep vias about 1 mm away from U7 and U18 pads,
  except the escape vias described above.
- Hand copper is Locked; routers must not move or delete it.
- Firmware pin map: `firmware/src/board/Pins.h` matches the schematic as of commit 8696399; not compiled. No pins
  were remapped in the U7 work above.
