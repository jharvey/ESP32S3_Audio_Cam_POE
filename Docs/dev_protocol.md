# Network protocol (development)

What units, the office console and test tools send each other. Source of truth: `firmware/src/core/Packet.h`,
`Packet.cpp` and `Auth.cpp`. Protocol version **2** (status packets carry the unit ID and room group).

## Transport

- UDP multicast to **239.255.42.1, port 5004**, TTL 1, on the intercom VLAN. Every unit sends and receives there.
- Addresses: a unit's IP is **10.42.<floor>.<room on that floor>** (room 214 -> 10.42.2.14; room 200 ->
  10.42.2.100 because .0 isn't usable), netmask 255.255.0.0. The IP is only used for the web page; the intercom
  identifies units by the room number in each packet.
- All multi-byte fields are little-endian.

## Common prefix (7 bytes)

`'I' 'C' | version u8 (2) | type u8 | room u16 | sub u8 (0; was the room letter before protocol 2)`

Types: 1 Audio, 2 Status, 3 Command, 4 Reply.

## Audio (16-byte header + payload), every 20 ms while talking

| Byte | Field |
|---|---|
| 7 | flags: 0x01 all-call, 0x02 from the office console |
| 8-9 | target: 0 = everyone, else a room number (room calls) |
| 10-11 | sequence number (wraps) |
| 12 | level: loudness 0-100 (dBFS + 100), for the loudest-N mix |
| 13 | codec: 0 PCM, 1 Opus |
| 14-15 | PCM: number of samples (320); Opus: payload bytes |
| 16- | payload: PCM 16-bit samples, or one Opus frame (20 ms, 16 kHz mono) |

A unit ignores audio from its own room group (unless it's from the console) and room calls for other rooms.

## Status (24 bytes), once a second and on changes

| Byte | Field |
|---|---|
| 7 | link: 0 none, 1 Ethernet, 2 Wi-Fi |
| 8 | power: 0 PoE, 1 battery |
| 9 | battery % (0 if none) |
| 10 | flags: 0x01 talking, 0x02 emergency, 0x04 camera OK, 0x08 TALK muted by the office, 0x10 TALK held over 2 min |
| 11 | fault shown on the display: 0 none, 1 `E1` no Ethernet, 2 `E2` no network, 3 `Lo` battery, 4 `dUP` |
| 12-15 | uptime, s |
| 16-19 | unit ID (from the board's MAC); two units with the same room and different IDs = duplicate (`dUP`) |
| 20-21 | room group (0 = the unit's own room) |
| 22-23 | reserved |

A unit drops a peer it hasn't heard from for 3.5 s.

## Command and Reply (16-byte header + text)

| Byte | Field |
|---|---|
| 7 | target sub (0xff = any) |
| 8-9 | target room (0xffff = all) |
| 10-15 | reserved |
| 16- | text (up to 640 bytes); long replies are split on line boundaries |

**Commands must be signed** with the site's network key (set with `netkey` over USB on each unit, and in the
office console): the text is `#<nonce> <mac> <command>`, where mac is the first 16 bytes (32 hex characters) of
`HMAC-SHA256(key, "<nonce>|<targetRoom>|<targetSub>|<fromRoom>|<fromSub>|<command>")`. A unit only accepts a
nonce higher than the last one it accepted (kept in flash), so a recorded command can't be replayed. `talk`,
`webpass`, `netkey` and `wifi` are refused over the network even when signed. Neither commands nor audio are
encrypted: keep the intercom on its own VLAN.

## Planned

Room calls with floor targets and replies (R2.0), call-office packets, time and schedule (R2.1), RTSP / ONVIF
camera streams (R3), LoRa messages (R4): see `release_designs.md`.
