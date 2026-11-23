# Release designs (draft, 23 Nov 2026)

Working designs for the increments after R1.1, so each one starts from an agreed shape. The order and status are
in [roadmap.md](roadmap.md); the overall plan is [firmware_plan.md](firmware_plan.md). Everything here is a draft:
change it before building the increment, not after.

Wire format today (protocol 2): every packet starts `'I' 'C' ver type | room u16 | sub u8`, then a per-type
header. Audio already carries `flags` (all-call, from console), a `target` room (0 = everyone) and a `codec` byte.

## R1.6 Compression (Opus)

- **Codec:** Opus, 16 kHz mono, 20 ms frames (320 samples), VBR target 24 kbit/s, complexity 5 to start, signal
  type voice. In-band FEC on when the unit is on Wi-Fi (expected loss 5 %), off on Ethernet.
- **Packet:** `codec = 1` (Opus); `nsamples` stays 320; the payload is the Opus frame with its length in the header
  (reuse `nsamples` as the byte count when `codec = 1`, or add a `len u16`; decide while coding). PCM (`codec = 0`)
  stays for bring-up and debugging, chosen with a console setting (`set codec opus|pcm`).
- **Receive:** one decoder per talker, next to its jitter buffer. A missing frame is concealed by Opus (PLC) instead
  of the current fade; with FEC the next packet can rebuild the lost one.
- **Code layout:** an `AudioCodec` interface in `src/core` (encode, decode, conceal) with `PcmCodec` and
  `OpusCodec`. Firmware: libopus through the arduino-libopus port; simulator: libopus built from source with CMake.
- **Tests:** round trip in `ic-selftest` (PESQ-style level check against the input), 10 % random loss on the
  in-memory bus with and without FEC, mixed PCM/Opus units in one test.
- **Measure on the board:** CPU per frame with ESP-SR running; if the S3 runs short, G.722 (64 kbit/s) instead.
- **Delay budget (mouth to ear, about 100 ms):** 20 ms mic frame + under 1 ms wired network + 60 ms jitter buffer
  (3 frames) + about 20 ms speaker output; Opus adds a few ms of look-ahead (PCM and Opus end up within about 5 ms).
  Opus takes the same CPU time every frame, so it adds no jitter of its own. Wired-only units could run a 2-frame
  jitter buffer (-20 ms). Echo in the room is cancelled against the local speaker signal, so codec delay doesn't
  matter there; echo between rooms (about 100 ms, a slap-back) is the same with PCM or Opus and is handled in R1.4.
- **Scale:** 40 rooms. At most 4 talkers are mixed; about 24 kbit/s each plus 40 status beacons a second.

## R1.7 Web console

- **Client:** a normal Windows PC on the same network, Chrome, Edge or Firefox (all support getUserMedia,
  WebSockets and AudioWorklet on HTTPS). Sized for 40 rooms: a 40-tile grid.
- **Where:** the office unit serves it; any unit can (it's the same firmware), but only units with a console
  password set (`consolepass` over USB, like `webpass`) enable the talk features.
- **Page:** room grid (one tile per unit from the status beacons: room, link, power, battery, faults, dUP), a
  target picker (one room, a floor, all), a Talk button (hold, or toggle with a confirmation), volume meters, and a
  log of who talked when.
- **Audio path, office PC -> rooms:** browser `getUserMedia` -> AudioWorklet resampled to 16 kHz mono, 20 ms frames
  -> WebSocket to the office unit (binary frames, PCM) -> the unit sends them as console audio (`kAudioFromConsole`,
  `target` = the room, or 0 for all) over multicast, Opus-coded once R1.6 is in.
- **Audio path, room -> office PC:** while a call is open, the office unit forwards that room's audio to the browser
  over the same WebSocket; the page plays it through an AudioWorklet with a small jitter buffer.
- **Security:** same login as the unit's web page, plus the console password; the WebSocket only opens after login.
  Browsers allow the microphone only on HTTPS (or localhost): the office unit serves HTTPS with its own certificate
  (made at setup, accepted once in the office browser), or Chrome is set to treat the unit's address as secure.
- **Firmware:** esp_http_server with WebSocket support (in the framework: `CONFIG_HTTPD_WS_SUPPORT`), and
  esp_https_server for the certificate. Plain HTTP stays for the status page.
- **Simulator:** the same page served by the simulator's web server on localhost, which browsers treat as secure, so
  the whole flow (grid, talk, reply) can be tested before boards exist.
- **Relation to the Qt office console:** the web console replaces it for day-to-day use; the Qt console stays as a
  test tool.

## R2.0 Room calls

- **Targets:** a room (`target` = room number), a room group (all units in the group play it), a floor (new flag
  `kAudioFloor`, `target` = floor digit), or all.
- **Call tone:** before a room call the target plays a short two-tone chime (stored in flash), and the display shows
  `CAL` for the length of the call.
- **Answer:** pressing TALK during a room call, or within 10 s after it, replies to the caller only (`target` = the
  caller's room, flag `kAudioReply`), not to the whole school.
- **Call the office:** a button (one of the spare key positions) sends a new `Call` packet to the office unit(s); the
  web console shows "214 is calling" with a chime until answered or cleared.
- **Priority:** all-call and emergency always win; a room call never interrupts them.
- **Tests:** the existing in-memory bus: who hears a room call, a floor call, a reply, and an all-call during a call.

## R2.1 Timed events

- **Time:** NTP from the school network when it's there; otherwise the office unit is the time source and sends the
  time in its status beacon. Units keep time through power cuts with the AXP2101's RTC.
- **Schedule:** a small table (weekday mask, time, sound, target) kept on every unit, pushed from the web console as a
  signed command; each unit plays its own chimes, so they still work with the office PC off.
- **Days off:** a date list (holidays, snow days) and "skip today" from the console.
- **Sounds:** a few built-in chimes in flash; more as files on the SD card.
- **Priority:** any call or emergency cuts a chime; quiet hours block chimes nights and weekends.

## R3.0 Video in the simulator

- **Stream:** MJPEG, VGA, 10-15 fps, one stream per unit, sent only in emergency mode or on request. Each frame is
  split into UDP datagrams (new `Video` packet type: frame id, part, count) to the NVR / console, unicast.
- **Audio first:** each unit sends its audio frame before any video part in the same 20 ms slot (the network load
  test showed audio dropouts without this).
- **Simulator:** each simulated unit renders a test picture (room number, time, a moving bar) as JPEG; one unit can
  use the PC webcam. The web console gets a video grid that shows every streaming unit.
- **Tests:** frame loss and reassembly on the in-memory bus; audio loss with 35 simulated streams (reuse `ic-netsim`).

## R3.2 Emergency mode

- **Start:** the console's alarm button (signed command to all or chosen units) or a unit's button combination.
- **Each unit:** opens the cover servo, lights a "camera live" indicator, starts the stream, shows `ALr` on the
  display; audio keeps priority.
- **End:** only from the console; covers close, streams stop.
- **Cover self-test:** a low-res snapshot of the inside of the cover (printed room number) compared with the one
  stored at install; a mismatch is a fault on the console.

## R4.0 LoRa protocol

- **Role:** alerts and status when the wired network is down or for units out of Wi-Fi range (parking lot):
  lockdown / all-clear, "unit 214 on battery", short fault codes. Too slow for voice.
- **Messages:** small binary packets (type, source room, sequence, payload <= 32 bytes) signed with the site key,
  so a stranger can't send a lockdown.
- **Repeater:** a 3rd-floor unit re-sends messages it hasn't seen (sequence check, hop count 2).
- **Simulator:** a LoRa channel model (airtime, collisions, range by floor) to size the message rate.
