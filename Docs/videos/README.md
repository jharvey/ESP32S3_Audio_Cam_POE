# Videos

Recorded from the simulator (`firmware/sim/record-walkthroughs.ps1`): two unit windows side by side, or a unit and
the office console, with a caption bar explaining each step. Re-record them after a firmware change with
`powershell -ExecutionPolicy Bypass -File firmware\sim\record-walkthroughs.ps1` (one: `-Only 06-office-calls-a-room`).

## How the intercom works (R1)

| Video | Shows |
|---|---|
| [01-all-call.mp4](01-all-call.mp4) | Hold TALK and every room hears you; several talkers at once are mixed |
| [02-gym-group.mp4](02-gym-group.mp4) | Gym units in one room group don't play each other back |
| [03-duplicate-number.mp4](03-duplicate-number.mp4) | Two units set to one room number both show `dUP` |
| [04-auto-tune.mp4](04-auto-tune.mp4) | Auto-tuning the echo canceller after installation |
| [05-portable-unit.mp4](05-portable-unit.mp4) | A portable unit on battery and Wi-Fi |

## How to (R2)

| Video | Shows |
|---|---|
| [06-office-calls-a-room.mp4](06-office-calls-a-room.mp4) | The office calls one room; the room hears a chime, sees `CAL`, and answers the office only |
| [07-calling-the-office.mp4](07-calling-the-office.mp4) | A teacher calls the office with the OFFICE button (`oFC`); the office console shows it and calls back |
| [08-setting-the-room-number.mp4](08-setting-the-room-number.mp4) | Changing a unit's room number with the hidden SETUP button (101 to 305) |
| [09-power-up-light-test.mp4](09-power-up-light-test.mp4) | The 4-second light test at power-up: every segment, the lamps and the RGB light |
| [12-call-lights.mp4](12-call-lights.mp4) | The office calls a room: white, blue and call lights blink with `CAL` until answered; a missed call keeps a short blink until a button is pressed |
| [13-floor-plan-calls.mp4](13-floor-plan-calls.mp4) | The office floor plan (a made-up demo plan): hold a room to talk to it, double-click to ring it; the teacher answers with OFFICE |

## Web console (office PC browser)

| Video | Shows |
|---|---|
| [10-school-status-page.mp4](10-school-status-page.mp4) | The school page on the office PC: every unit by floor, talking (yellow), calling the office (blue), offline (red) |
| [11-talking-from-the-pc.mp4](11-talking-from-the-pc.mp4) | Talking to the school from the office PC microphone; room 214 hears it |
