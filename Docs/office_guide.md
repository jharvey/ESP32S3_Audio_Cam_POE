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
- **Call selected room:** select a room and hold the button; only that room hears you (all units of a room group,
  such as the gym). The room hears a two-note chime and shows `CAL`; start speaking after the chime (about half a
  second). When the room holds TALK within 10 s, only the office hears the answer ("214 answers the office" in
  the log).
- **Call its floor:** every room on the selected room's floor (214 -> all 2xx rooms).
- **Rooms calling the office:** a room that holds DOWN 2 s shows blue, "CALLING THE OFFICE", with a beep. Call
  it back to answer; that clears it.
- **Calls list:** every call, newest first: rooms calling the office ("answered by the office after 12 s", or
  **MISSED** in red if the room stopped calling before anyone called back) and the office's calls ("answered by
  214" or "no answer"). The line above the room grid counts missed calls. Every call is also added to
  `intercom-calls.csv` next to the console program, for a record you can open in Excel.
- **Commands:** send a console command to one unit, a room or all units (signed with the network key), for example
  `get`, `set volume_db -6`, `save`, `cal`, `selftest result`.
- **Open web page** of the selected unit.
- **Mute TALK 15 min / Unmute:** stops the selected unit's TALK (a stuck button, misuse). The room still hears
  all-calls and other rooms. The mute ends by itself after 15 minutes.
- **Coupling test: doors closed / ... doors open:** run both once after installation (and after moving units),
  with the school quiet: every unit plays a short sweep in turn and works out which rooms hear each other. The two
  results are kept apart; rooms are played quieter by the louder of the two, and units that hear each other like
  their own speaker (the gym) are grouped from the doors-closed run.

**Installer tests on the school page:** the **Installer tests** panel starts the same coupling tests (doors closed
or open) on every unit, and **Auto-tune the selected room** on one room (click its card first). They can be
protected with a tests password, set on the office unit's USB console: `testpass <password>` (`testpass off`
removes it). No password is set at first, so anyone with the web login can run them.

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
then click where it hangs (one per corner of the gym). **Move dots** rearranges them. The drawing and the positions
are kept in this browser only (they are not stored on the unit and are not part of the firmware), so load the DXF
once on each office PC; positions clicked on one PC have to be clicked again on another.

On the map, a room is anywhere inside its walls (or just outside them):
- **Click** a room: picks it, like clicking its card (for calls, commands and auto-tune).
- **Hold** a room: talk to it from the PC microphone while you hold; release to stop. It hears a chime first.
- **Double-click** a room: ring it. Its unit chimes every 2 seconds and blinks its lights for 10 seconds; the
  teacher answers with TALK and you hear them on the office unit. Nobody answering leaves a missed-call blink
  on the unit. Double-clicking the gym rings all the gym units.

**Talking from the PC:** hold **Hold to talk to everyone** (or the space bar) and speak into the PC's microphone
or headset: the unit sends it to every room as an office all-call, and the meter shows your voice level. Rooms
that answer are heard on the office unit's own speaker. Use a headset if the PC is next to the office unit.

**Calling one room or a floor (R2):** click the room's card (it gets a blue outline), choose **The selected room**
or **The selected room's floor** in the list, and hold **Hold to call room 214**. Only that room (all units of a
room group, e.g. the gym) or that floor hears you, after a chime; the room's answer comes back to the office unit.

The first time, the browser asks to use the microphone: allow it. Browsers only allow the microphone on secure
pages, and the unit's page is plain `http://` on the intercom network, so set this once on the office PC:
- **Chrome / Edge:** open `chrome://flags/#unsafely-treat-insecure-origin-as-secure` (Edge:
  `edge://flags/#unsafely-treat-insecure-origin-as-secure`), enable it, enter the office unit's address (for
  example `http://10.42.1.5`, the office unit, room 105) and restart the browser.
- **Firefox:** in `about:config` set `media.devices.insecure.enabled` and `media.getusermedia.insecure.enabled`
  to `true`.

*(Planned: HTTPS on the units, so no setting is needed.)*

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

- **Timed events (R2.1, in code, not yet in the units):** each unit keeps the schedule and plays its own chimes.
  Easiest from the school page's **Timed events** panel: pick the time, days, sound and who hears it
  (everyone, the selected room or its floor) and press Add; Delete number, Skip today / Resume today and
  days off work the same way. Every change is sent to all units, so their schedules stay identical. Set the
  time zone once first, in the Commands panel: `tz -300 us` to all units (US Eastern with daylight saving).
  The same commands work from the PC console: `sched add 11:30 mon-fri lunch`, `sched add 8:00 mon-fri bell
  floor 2`, `sched`, `sched del 2`, `dayoff add 2026-12-24`, `skip today`. Units get the time from the school's
  time server (`ntp`), or from the PC console every 10 minutes.
- **Commands panel (school page, R2):** send any console command to all units or the selected room (for
  example `stats`, `version`, `group 299`); the replies appear underneath. The page's unit signs them with its
  network key, so set `netkey` on it (USB) first; it also needs the clock.
- **Emergency mode (R3):** an alarm button opens the camera covers, starts the video to the NVR and flashes the
  alarm light in every room.
