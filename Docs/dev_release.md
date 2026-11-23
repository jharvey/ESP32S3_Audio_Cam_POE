# Release process (development)

## Branches

- `single-sided`: the board (KiCad) and hardware documents.
- `fw/r1-intercom`: R1 firmware, simulator and documents (merges `single-sided` in to stay current).
- Later releases branch from the one before: `fw/r2-room-calls` from R1, `fw/r3-camera` from R2, `fw/r4-lora` from R3.
  A fix found on a later branch goes to the earlier one first.

## Versions

- `kFirmwareVersion` in `firmware/src/core/Config.h` (0.3.0 now). Bump the minor version for each increment that
  goes to units, the patch version for fixes.
- The network protocol version (`kVersion` in `Packet.h`, now 2) only changes when the wire format does; units on
  different protocol versions ignore each other's packets, so all units are updated together.

## Before a release goes to units

1. `ic-selftest` prints `ALL PASS` on Windows and Linux.
2. The firmware builds without new warnings; note RAM and flash use in the release notes.
3. Bench test on two boards: talk both ways, a room group, a duplicate number, failover, auto-tune
   (`bring-up.md` covers the first boards).
4. Pilot rooms (3-5) for a week or two before the whole school.
5. Final documents frozen with the release number; walkthrough videos re-recorded from that firmware; release notes
   written (see `documentation_plan.md`).

## Making and installing a release

- `unit_tool.py release` copies the last build into the releases folder (refuses to overwrite a version).
- USB: `update-unit.bat` / `unit_tool.py update` flashes the newest release, checks version and room, keeps settings,
  logs every update.
- SD card: `firmware.bin` from the release on the card's root; the unit installs it at the next power-up and renames
  it to `firmware.done` (or `firmware.bad`).
- There are no network updates (decided for security); visiting each room is the cost.
