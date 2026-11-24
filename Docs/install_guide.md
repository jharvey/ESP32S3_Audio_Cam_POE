# Installation guide

For whoever puts the intercom in: network, mounting, first setup and tuning. Features marked *(planned)* are on
the roadmap but not in the units yet. Hardware bring-up of new boards is in [bring-up.md](bring-up.md).

## 1. Network

- **Own VLAN for the intercom**, with only the units, the office PC and the NVR on it. This is the main security
  measure: audio is plain multicast, so anyone on the same network could listen or send an all-call.
- **IGMP snooping** on the switches (the audio is multicast to 239.255.42.1, port 5004).
- **Audio priority** (DSCP / QoS) above video; a gigabit port for the NVR.
- **Addresses:** 10.42.<floor>.<room>, netmask 255.255.0.0, so room 214 is 10.42.2.14 and room 7 is 10.42.0.7.
  Keep 10.42.0.1 for the gateway. No DHCP: each unit takes its address from its room number.
- **Wi-Fi** (portable units and failover): a WPA2/WPA3 network that only reaches the intercom VLAN.
- **PoE:** 802.3af (class 0-3) switch ports.

## 2. Mounting

- Single-gang box or surface mount, at head height, speaker facing the room, away from corners.
- **Gym:** two to four units around the room; give each its own number counting down from 299 (299, 298, ...) and
  put them in one group (step 3).
- Keep units in neighbouring rooms on different walls from the shared wall where you can: a voice played next door
  comes back through the wall about 20 dB down (through an open door only about 10 dB down).

## 3. First setup (USB)

With the unit on USB (`firmware/tools/update-unit.bat` / `unit_tool.py`):

```
unit_tool.py setup --flash --unit 214 --webpass <8+ chars> --netkey <16+ chars> --selftest
unit_tool.py setup --unit 298 --group 299            # a gym unit
unit_tool.py setup --wifi <ssid> <password>          # portable units
```

- `--netkey` must match the office console's key (signed network commands).
- `--webpass` turns on the unit's web page (off until a password is set).
- The room number can also be set on the unit itself (teacher's guide). Two units on one number show `dUP`.

## 4. Echo tuning

- After the unit is in its case and on the wall: `unit_tool.py tune auto` (or hold UP for 5 s on the unit). Keep the
  room quiet; it plays a short noise burst and sets the echo canceller for that room.
- Save a room's settings and copy them to similar rooms:
  `unit_tool.py tune get --file room214.txt`, then `unit_tool.py tune set --file room214.txt --save` on another unit.
- Every setting can also be changed one at a time over USB, the web page or the office console (`set key value`,
  then `save`). `help` lists them.
- **Coupling test** (after all units are on the walls and tuned, with the rooms quiet): press **Coupling test** in
  the office console. Each unit plays a short sweep in turn (about 1 s each) while the others listen. Then:
  - units in the same room (the gym) group themselves (room group = their highest number), so they don't play
    each other back;
  - rooms that hear each other through a wall or an open door play each other about 6 dB quieter
    (`couple_duck_db`), which cuts the talker's own voice coming back from next door;
  - the console log shows each unit's result, e.g. `102 -20 dB (through wall/door); 214 not heard`.
  Run it again after moving units. `couple clear` on a unit forgets its result; `group off` undoes a group.

## 5. Check list per room

- Display shows the room number, no `E1` / `E2` / `dUP`.
- TALK from the room is heard in the office; an all-call from the office is heard in the room.
- Auto-tune result `e` 15 or more (classroom); the gym will be lower.
- The code card is on or next to the unit.
