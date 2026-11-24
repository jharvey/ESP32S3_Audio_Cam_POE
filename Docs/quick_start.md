# Quick start: one unit

Setting up a single intercom unit, from the box to talking. The full steps (network, gym groups, Wi-Fi, tuning)
are in the [installation guide](install_guide.md).

## You need

- The unit, its front cover and a PoE network cable on the intercom network (802.3af switch port).
- A PC with a USB-C cable and the intercom tools (`firmware/tools/update-unit.bat`).
- The school's network key (at least 16 characters; the same one the office console uses) and a web password
  for the unit (at least 8 characters).

## Steps

1. **Connect USB** and run, with this unit's room number:
   ```
   update-unit.bat setup --flash --unit 214 --webpass <web password> --netkey <network key> --selftest
   ```
   It installs the latest firmware, sets the room number and keys, and runs the self-test. It must end with
   `SELF-TEST PASSED`.
2. **Mount the unit** on the wall (head height, speaker facing the room) and **plug in the PoE cable**. Unplug USB.
3. **Watch it start.** It shows the room number, `214`, within a few seconds. (From firmware R2 it first tests
   its lights for about 4 seconds: every display segment in turn, `8.8.8.`, then `L1`-`L6` for the lamps and the
   RGB light. A segment or light that stays dark is broken.) If it shows `---`, no room number is
   set (step 1, or set it on the unit: hold the hidden SETUP button under the cover for 3 s).
4. **Tune the echo canceller.** Keep the room quiet and hold **UP** for 5 seconds. The display shows `c` and a
   percentage for about 5 s, then `e` and a number: 15 or more is good in a classroom.
5. **Test.** Hold **TALK** and speak: the office console shows the room talking and the office hears it. Ask the
   office for an all-call: the room hears it. Ask the office to call just this room: you hear a chime and see `CAL`.
6. **Put the code card** next to the unit.

## If something is wrong

| You see | Do |
|---|---|
| `E1` flashing briefly | The network cable or switch port: check the PoE cable is in (the unit has fallen back to Wi-Fi) |
| `E2` flashing briefly | No network at all: cable, switch port, VLAN |
| `dUP` and the number | Another unit already has this number: fix one of them |
| Nothing at all | No PoE power: try another switch port or a PoE injector |
| `e` below 10 after tuning | Room too noisy or the unit is loose on the wall: tighten, wait for quiet, tune again |

The unit's own web page is at `http://10.42.<floor>.<room on the floor>` (room 214: `http://10.42.2.14`), with
the web password from step 1. The office's whole-school page is `/school` on any unit.
