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

## School status page (web console, step 1)

Open `http://<office unit's address>/school` in Chrome, Edge or Firefox on the office PC (same login as the
unit's own page; for example, if the office unit is set to room 100: `http://10.42.1.100/school`). It shows every unit, floor by floor,
updated every 2 seconds:
- green OK; yellow talking; blue calling the office; orange degraded (on Wi-Fi with PoE power, muted);
- red: offline (with when it was last heard), duplicate number, TALK held, battery low, output shorted (`oL`);
- the line at the top counts units online, offline, calling, talking and needing attention.

Any unit can show this page, so it still works if the office unit is the one that failed.

*(Planned, next step: pick a room, a floor or everyone on this page, hold Talk and speak with the PC's
microphone, and hear the room answer. The first time, accept the unit's certificate in the browser: the
microphone needs HTTPS.)*

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
  Set it with commands to "all units": `tz -300 us` once, then e.g. `sched add 11:30 mon-fri lunch`,
  `sched add 8:00 mon-fri bell floor 2`; `sched` lists, `sched del 2` removes; `dayoff add 2026-12-24`;
  `skip today` for a snow day. The console keeps the units' clocks right when the school has no time server.
- **Emergency mode (R3):** an alarm button opens the camera covers, starts the video to the NVR and flashes the
  alarm light in every room.
