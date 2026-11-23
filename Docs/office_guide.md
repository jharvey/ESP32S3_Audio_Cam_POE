# Office guide

For the office: talking to the school, watching the units, and (later) room calls, chimes and emergencies.
Features marked *(planned)* are on the roadmap but not in the units yet.

## Office console

Today: the PC office console (`firmware/sim/console`), a Windows program. *(Planned: the web console, below.)*

- **Room grid:** every unit with its link (Ethernet / Wi-Fi), power (PoE / battery and level) and uptime. Offline
  units in red; units listed in the rooms file but never heard are shown too.
- **All-call:** hold the button to talk to every room with the PC microphone.
- **Talk to a room:** select a room and hold the button; only that room hears you.
- **Commands:** send a console command to one unit, a room or all units (signed with the network key), for example
  `get`, `set volume_db -6`, `save`, `cal`, `selftest result`.
- **Open web page** of the selected unit.

## Web console *(planned, R1.7)*

A page on the office unit, opened in Chrome, Edge or Firefox on the office PC:
- a grid of the 40 rooms with their status and faults (`dUP` and faults in red);
- pick a room, a floor or everyone, hold Talk and speak with the PC's microphone; hear the room answer;
- the first time, accept the unit's certificate in the browser (the microphone needs HTTPS).

## Watching for problems

| Shown | Meaning |
|---|---|
| Unit offline (red) | No network or no power |
| On Wi-Fi with PoE power (`E1` on the unit) | The network cable or switch port is faulty |
| Battery and low | A portable unit away from its cable |
| Duplicate (`dUP`) | Two units on one room number |
| TALK held over 2 minutes *(planned)* | A button may be stuck; the office can mute that unit until it's released |

## Planned

- **Room calls (R2.0):** call one room, a floor or a group; the room hears a chime and sees `CAL`; its reply comes
  only to the office. Rooms can call the office with a CALL button.
- **Timed events (R2.1):** lunch chime, bells, class changes; a schedule kept on every unit, days off, quiet hours.
- **Emergency mode (R3):** an alarm button opens the camera covers, starts the video to the NVR and flashes the
  alarm light in every room.
