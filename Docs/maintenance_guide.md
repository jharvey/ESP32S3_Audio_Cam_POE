# Maintenance guide

Updating, tuning and replacing units. Features marked *(planned)* are on the roadmap but not in the units yet.

## Firmware updates

Network updates are deliberately not possible (security): updates need USB or the SD card, so the case has to be
opened.

- **USB:** `firmware/tools/update-unit.bat` (or `unit_tool.py update`). It flashes the newest release, checks the unit
  comes back with the new version and the same room number, keeps all settings, and logs every update.
- **SD card:** copy the release's `firmware.bin` to the card's root and power the unit up. It installs it, renames
  the file to `firmware.done` (or `firmware.bad` if it failed) and restarts.
- Update a few pilot rooms first; a bug found after the full install means visiting every room.

## Echo and audio settings

| Task | Command |
|---|---|
| Re-tune a room (quiet room) | `unit_tool.py tune auto`, or hold UP 5 s on the unit |
| Save a room's settings to a file | `unit_tool.py tune get --file room214.txt` |
| Load settings from a file | `unit_tool.py tune set --file room214.txt --save` |
| One setting | `set volume_db -6` then `save` (USB, web page or office console) |
| Undo the last auto-tune or save | `undo` |
| Back to factory settings | `defaults` then `save` |

Useful settings: `volume_db`, `mic_gain_db`, `opus` (1 = compressed audio, 0 = raw for testing), `bitrate_kbps`,
`duck` (half duplex for very echoey rooms), `nlp_db` (how hard leftover echo is suppressed).

## Replacing a broken unit

1. Take the replacement out of stock (already flashed; `unit_tool.py setup --flash` if not).
2. Mount it, connect the PoE cable.
3. Set its room number with the SETUP button under the front cover (teacher's guide), or `unit_tool.py setup --unit 214`.
4. Gym units: also `unit_tool.py setup --group 299`.
5. Run `unit_tool.py tune auto`, or load the room's saved settings with `tune set`.
6. The office console logs the new unit (a new unit ID on that room number).

## Power-up light test

Every time a unit powers up it runs a 4-second light test, so a dead light shows at a glance:
each display segment in turn on all three digits, then the dots, then `8.8.8.`; then `L1` white lamp,
`L2` IR lamp, `L3` call light, and `L4`-`L6` the RGB LED in red, green and blue. A segment that never lights,
or a lamp / LED that stays dark during its step, is broken. The MISC outputs (one may drive the laser) and servo
power are not part of the test. `boottest run` repeats it; `boottest off` / `boottest on` turn it off or on.

## Faults

| On the unit | Likely cause | Check |
|---|---|---|
| `E1` | Cable or switch port | Cable, PoE port, switch |
| `E2` | No network | Switch, VLAN, Wi-Fi settings |
| `dUP` | Two units on one number | The office console shows both unit IDs |
| `Lo` | Battery low | Charge on PoE |
| No sound | Speaker, amp or volume | `selftest` (plays a tone the mic must hear) |
| Echo complaints | Tuning, case, speaker | `tune auto` in a quiet room; check the case seals |

## Parts

- Speaker: 4-8 ohm, 2-3 inch full range, 3 W or more, in a sealed back box, on J10. A better speaker can simply
  replace the stock one; re-run `tune auto` afterwards. Gain jumpers JP3-JP6 set the amp gain (3-15 dB).
- Buttons: TS-1187A (100,000 presses). Seven of them are TALK buttons, so one worn switch doesn't stop the unit.
