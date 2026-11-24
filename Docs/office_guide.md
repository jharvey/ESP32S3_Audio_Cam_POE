# Office guide

For the office: talking to the school, watching the units, and (later) room calls, chimes and emergencies.
Features marked *(planned)* are on the roadmap but not in the units yet.

## Office console

Today: the PC office console (`firmware/sim/console`), a Windows program. *(Planned: the web console, below.)*

- **Room grid:** every unit with its link (Ethernet / Wi-Fi), power (PoE / battery and level), uptime and any
  problem. Red = needs someone now (offline, duplicate number, TALK held, battery low); orange = working but degraded
  (on Wi-Fi with PoE power, muted). Units listed in the rooms file but never heard are shown too. The line above
  the grid counts the units that need attention.
- **All-call:** hold the button to talk to every room with the PC microphone.
- **Talk to a room:** select a room and hold the button; only that room hears you.
- **Commands:** send a console command to one unit, a room or all units (signed with the network key), for example
  `get`, `set volume_db -6`, `save`, `cal`, `selftest result`.
- **Open web page** of the selected unit.
- **Mute TALK 15 min / Unmute:** stops the selected unit's TALK (a stuck button, misuse). The room still hears
  all-calls and other rooms. The mute ends by itself after 15 minutes.
- **Coupling test:** run once after installation (and after moving units), with the school quiet: every unit
  plays a short sweep in turn and works out which rooms hear each other (see the installation guide).

## School status page (web console, step 1)

Open `http://<office unit's address>/school` in Chrome, Edge or Firefox on the office PC (same login as the
unit's own page; the office unit is room 105: `http://10.42.1.5/school`). It shows every unit, floor by floor,
updated every 2 seconds:
- green OK; yellow talking; blue calling the office; orange degraded (on Wi-Fi with PoE power, muted);
- red: offline (with when it was last heard), duplicate number, TALK held, battery low, output shorted (`oL`);
- the line at the top counts units online, offline, calling, talking and needing attention.

Any unit can show this page, so it still works if the office unit is the one that failed.

**Floor plan:** press **Load a DXF floor plan…** and pick the school's DXF drawing. Each layer that has room
numbers in its labels (like `ART 101`) becomes a floor tab, and every unit is shown as a coloured dot on its room
(same colours as above); a tab gets a red or blue dot when something on that floor needs attention or is calling.
Units whose number isn't a room on the drawing (the gym units 299, 298, ...) are listed above the map: click one,
then click where it hangs (one per corner of the gym). Click any dot to move it. The drawing and the positions are
kept in this browser only (they are not stored on the unit and are not part of the firmware), so load the DXF once
on each office PC; positions clicked on one PC have to be clicked again on another.

**Talking from the PC:** hold **Hold to talk to everyone** (or the space bar) and speak into the PC's microphone
or headset: the unit sends it to every room as an office all-call, and the meter shows your voice level. Rooms
that answer are heard on the office unit's own speaker. Use a headset if the PC is next to the office unit.

The first time, the browser asks to use the microphone: allow it. Browsers only allow the microphone on secure
pages, and the unit's page is plain `http://` on the intercom network, so set this once on the office PC:
- **Chrome / Edge:** open `chrome://flags/#unsafely-treat-insecure-origin-as-secure` (Edge:
  `edge://flags/#unsafely-treat-insecure-origin-as-secure`), enable it, enter the office unit's address (for
  example `http://10.42.1.5`, the office unit, room 105) and restart the browser.
- **Firefox:** in `about:config` set `media.devices.insecure.enabled` and `media.getusermedia.insecure.enabled`
  to `true`.

*(Planned: HTTPS on the units, so no setting is needed; picking one room or a floor on this page (with the
room calls of R2).)*

## Watching for problems

| Shown | Meaning |
|---|---|
| Unit offline (red) | No network or no power |
| On Wi-Fi with PoE power (`E1` on the unit) | The network cable or switch port is faulty |
| Battery and low | A portable unit away from its cable |
| `oL` output shorted (red) | A lamp, MISC or servo cable is shorted or overloaded; the unit switched that output off and keeps working. Check the cable |
| Duplicate (`dUP`) | Two units on one room number |
| TALK held over 2 minutes (red) | A button may be stuck: **Mute TALK 15 min**, then have the button checked |
| Muted (orange) | The office muted the unit's TALK; it ends by itself or with **Unmute** |

## Planned

- **Room calls (R2.0):** call one room, a floor or a group; the room hears a chime and sees `CAL`; its reply comes
  only to the office. Rooms can call the office with a CALL button.
- **Timed events (R2.1):** lunch chime, bells, class changes; a schedule kept on every unit, days off, quiet hours.
- **Emergency mode (R3):** an alarm button opens the camera covers, starts the video to the NVR and flashes the
  alarm light in every room.
