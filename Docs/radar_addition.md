# People sensor (mmWave radar): what was added

Written overnight on 2026-11-28. Board and schematic changes are NOT committed; the firmware is committed and pushed.

## The two options

| | HLK-LD2450 (planned) | HLK-LD2410C (low-cost option) |
|---|---|---|
| Gives | up to 3 people, each with a position | someone is there or not, and how far |
| Still people | weaker: it tracks movement | good |
| Size | 15 x 44 mm, on a cable | 16 x 22 mm, on a 5-pin header |
| Connector on our board | J22, JST ZH 1.5 mm 4-pin (C157997) | J21, 1x05 2.54 mm header, marked do-not-fit |
| Module at LCSC | not found: buy separately | C19723500, about 3.25 USD |

Fit one or the other. The firmware works out which one is attached from the data.

## Schematic

- GPIO10 is now `RADAR.TX` (data from the sensor). It was the LoRa interrupt.
- LoRa interrupt (U19 pin 6) now goes to U22 P14. The LD2410C's OUT pin goes to U22 P13.
- J21 pins: 1 TX, 2 RX (not connected), 3 OUT, 4 GND, 5 VBUS.
- J22 pins: 1 VBUS, 2 module RX (not connected), 3 module TX, 4 GND.
- Both sit below U22 on the sheet. The VBUS symbol on J21 hangs below its wire; move it if it bothers you.

## Board

- J21 and J22 are parked to the right of the board outline (x = 162 mm). They need placing and routing.
- New footprint `Project-specific:HLK-LD2410C` with the 22 x 16 mm module outline and a STEP 3D model (J22 also has a STEP model in Project_Specific/Libs.pretty/3D, since KiCad ships none for JST ZH)
  (`Project_Specific/Libs.pretty/3D/HLK-LD2410C.wrl`).
- KiCad's checks: no new schematic errors (two old "pin not connected" errors on U22 are gone). Board/schematic
  mismatches: the same 5 as before the change. The new pads show as unrouted, as expected.

## Check before ordering

1. **J22 cable order.** The pin order is from the LD2450 manual's photo and assumes a 1:1 cable. Swapped ends would
   put 5 V on ground. Check a real module and cable with a meter.
2. **VBUS voltage.** Both modules want 5 V. VBUS sits behind D1; confirm it stays near 5 V on PoE and on USB.
3. **LD2410C outline.** The position of the pin row on the module (centred, 1.3 mm from the edge) was measured off
   the manual's drawing, not from a part.
4. **Where the sensor looks.** It must face the room with no metal or copper pour in front. The LD2450 manual asks
   for wall mounting at 1.5 to 2 m height.
5. **JLC stock** for C157997 and C19723500 was not checked.

## Firmware (pushed, commit 72a00b2 on fw/r3-net-update)

- Reads either module on GPIO10 at 256000 baud. Nothing is sent to the module; set it up with Hi-Link's phone app.
- Each unit sends what it sees with its status. The office page draws people as small black dots in the room.
- "Plan scale" button on the plan: type the real length of floor 1's drawing in metres. Without it the dots line up
  in the middle of the room instead of where the people stand.
- `radar` command on a unit prints what the sensor sees.
- Tested: simulator self-test (both frame types, broken-up data, time-outs, data reaching the office page), stress
  test, firmware build, page script syntax.
- Not tested: a real sensor, and the dots in a real browser. Left and right may come out mirrored until a real
  module is tried.

## Open decisions

- A full classroom shows at most 3 dots (LD2450 limit).
- Dots are shown all the time. An option "only during an emergency" would be easy to add if the school prefers.
- LoRa: the firmware does not use the LoRa interrupt yet. When LoRa is added it must read DIO0 through U22 P14.

## Added later

- **JP9** (solder jumper, open by default) sits in the line from J21 pin 1 (the LD2410C's TX) to `RADAR.TX`. Close it only
  on a board that has the LD2410C soldered to J21. Leave it open when a radar is plugged into J22, so an unused or
  unpowered module on J21 cannot load the data line. Only one radar may drive `RADAR.TX` at a time.
- The case has a snap-in frame for an LD2410C behind the lower face (wired to J22 by a 4-wire JST ZH pigtail).
- The PoE isolation zone was redrawn around the PoE parts' present positions; J21 and J22 are outside it.
