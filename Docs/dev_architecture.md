# Firmware architecture (development)

How the intercom firmware is put together. Code paths are relative to `firmware/`.

## Three layers

```
src/core/      the intercom itself: plain C++17, no Arduino / ESP-IDF headers   <- tested in the PC simulator
  Hal.h        the interfaces the core needs from a platform
src/board/     ESP32-S3 drivers that are plain C++ (TM1637 panel, pin map, ESP-SR echo canceller)
src/main.cpp   the ESP32-S3 platform: implements Hal with real hardware
sim/           the PC platform: implements Hal with Qt, UDP multicast, simulated rooms; plus tests and tools
```

The core never touches hardware. Everything it needs is behind `Hal` (`src/core/Hal.h`):

| Interface | Job | ESP32-S3 | Simulator |
|---|---|---|---|
| `INet` | send / receive datagrams to the intercom multicast group | `NetworkUDP` on 239.255.42.1:5004 | `QUdpSocket` (same group, real LAN) / in-memory bus in tests |
| `ILinks` | Ethernet / Wi-Fi state, which link to use, address, Wi-Fi credentials | W5500 + Wi-Fi, default netif follows the link | checkboxes |
| `IPower` | PoE or battery, battery % | AXP2101 over I2C | checkbox and slider |
| `IPanel` | buttons and the 3-digit display | TM1637 through the TCA9555 | window buttons, drawn display |
| `IStore` | room number and small text settings in flash | `Preferences` (NVS) | `QSettings` / a map in tests |
| `ILog`, `IDiag` | log lines, board self-test | USB serial, I2C probes | window log |

## One frame (20 ms, 320 samples at 16 kHz)

`Intercom::processFrame(mic, speaker)` (`src/core/Intercom.cpp`), called once per frame by the platform:

1. Panel: buttons -> auto-tune hold, room setup (hidden SETUP), TALK.
2. Echo canceller `capture(mic)` -> clean mic audio.
3. Link selection (`updateLink`): Ethernet when stable 200 ms, else Wi-Fi; FEC follows the link.
4. If talking: gain, Opus (or PCM) encode, audio packet out.
5. `receive()`: audio into each talker's jitter buffer (skipping our room group and calls for other rooms);
   status beacons into the peer list (duplicate-number check); signed commands run and answered.
6. Mix: each jitter buffer decodes its next frame in order (Opus PLC for gaps); the loudest 4 are mixed and limited.
7. Test tone if requested; echo canceller `playback(speaker)`.
8. Once a second: status beacon; power read; display updated (room number, codes).

## Threads on the ESP32-S3

| Task | Core / priority | Does |
|---|---|---|
| audio | core 1, priority 20 | I2S read (paces everything), `processFrame`, I2S write; console and web jobs run here between frames; watchdog 3 s |
| panel | core 0, priority 2 | TM1637 key scan and display over I2C every 30 ms |
| autotune | core 0, priority 1 | auto-tune analysis (seconds of maths) so audio keeps running |
| loop() | Arduino | USB serial line input, web server |

The core is only ever touched from the audio task; other tasks hand work to it through queues.

## Main modules

| File | What |
|---|---|
| `Intercom.*` | the application: talk, receive, mix, links, groups, duplicates, display codes, console commands |
| `Packet.*` | wire format, addressing (room -> IP) |
| `AudioCodec.*` | PCM and Opus encoders / decoders |
| `JitterBuffer.*` | per-talker reordering and playout; decodes in order |
| `Mixer.*` | loudest-N mix with a limiter |
| `NlmsAec.*`, `board/EspSrAec.*` | echo cancellers (portable NLMS; ESP-SR on the board) |
| `Calibrator.*`, `Tuning.*` | auto-tune and the tuning parameters |
| `RoomSetup.*` | room-number setup on the panel |
| `Auth.*` | web password, signed network commands |
| `WebUi.*`, `WebPage.cpp` | the unit's web page and its API |

Echo cancelling in the room is local (reference = this unit's own speaker), so network delay and codecs don't
affect it. Echo between rooms is a network effect; see `Docs/echo_prediction.md`.
