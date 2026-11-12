# Firmware plan (draft)

## Goals

- **Site:** OVCS, Cornish, Maine. A small K-12 school in a wood building: 3 floors, about 35 rooms.
  Large echoey rooms like the gym get several units in one room.
- **Replaces FRS radios.**
  - Wall units run on PoE, so there are no batteries to go dead.
  - Wired Ethernet keeps working when police bring a Stingray. A Stingray only impersonates a cell tower, so it doesn't affect wired, Wi-Fi or FRS.
  - Wired Ethernet is also immune to RF jamming, which would affect Wi-Fi units.
- **Live video only in an emergency.** The camera stays powered, but a physical cover blocks the lens the rest of the time.
  The cover is printed with the room number, so a snapshot of it shows the camera is working.
  A servo opens the cover on alarm.
- **A few portable units** run on Wi-Fi and battery for trips to the parking lot. Plugging into PoE switches them back to wired.
  They must boot fast and switch over fast.

## Build order

1. **Audio core:** mics in, echo cancellation, encode, network, mix, speaker.
2. **Network:** PoE and Wi-Fi failover, room number as IP address.
3. **Office console:** all-call, talk to one room, listen-in, status of every unit.
4. **Emergency mode:** camera power, cover servo, RTSP video to the NVR and console.
5. **Tracking:** pan/tilt person tracking and the laser (MISC.OUT).

Each step runs in the PC simulator first (same approach as the T-Deck Pro sim), then on hardware.

## 1. Audio

### Talk model
- Press the talk button and every unit hears you.
- Several people can press at once. Every unit mixes all active talkers, capped at the 4 loudest.
- Codec: Opus at 16 kHz, 20 ms frames, about 24 kbit/s per talker.
- **Wired transport:** UDP multicast on an intercom VLAN, with IGMP snooping on the switches.
- **Wi-Fi transport:** Wi-Fi multicast is slow and lossy, so Wi-Fi units unicast to a wired relay unit.
- Each unit keeps a jitter buffer per talker.

### Echo cancellation (required)

Pipeline on each unit:

```
PDM mics x2 (16 kHz) --+
                       +--> ESP-SR AFE: AEC -> beamform/BSS -> noise suppression -> AGC -> VAD --> Opus --> net
speaker mix ----------+   (reference)
   |
   +--> I2S1 --> amp --> speaker
```

- **Echo cancellation (AEC):** ESP-SR's AFE (Espressif's audio front end) runs on the S3 using the 8 MB of PSRAM.
  - The reference signal is exactly what the unit sends to its speaker.
  - The reference is time-aligned to the mic samples. A calibration chirp at install time measures the delay.
- **Both PDM mics feed the AFE:** two-mic beamforming / source separation picks up the talker over room noise.
- **Residual echo suppression and noise suppression** remove what the AEC leaves behind.
- **AGC** keeps levels even between someone close to the unit and someone across a room.
- **VAD (voice detection)** stops sending silence, which saves bandwidth when many buttons are held at once.

### Auto-tune and manual override (built)
- Every unit can measure its own room and set its echo-canceller settings:
  - Start it from the panel (MODE + UP 3 s) or the console (`cal`).
  - It measures delay, reverb time, coupling and noise, then replays the recording to choose settings.
- Every setting can also be read and changed by hand, then saved, over USB serial or the network.
- Details are in [firmware/README.md](../firmware/README.md).

### Large echoey rooms (gym)
- The gym's reverb tail (1–2 s) is longer than any AEC filter can cancel, and every unit in the gym plays the same audio.
- **Room groups:** units with the same room number form a group.
  - Units in a group never play each other's mic audio back.
  - Only the pressing unit's mic is sent.
- **Ducking fallback:** if residual echo stays high (measured during calibration), the unit drops to half duplex while its button is held. Its speaker ducks by about 20 dB while it talks.
- **Case design matters as much as software.**
  - Separate sealed chambers for speaker and mics in the 3D-printed case.
  - Gasket the mic ports.
  - Point the speaker away from the mics.

### Simulator
- ESP-SR is a prebuilt library for the ESP32 only and won't run on a PC.
- The sim uses SpeexDSP (or WebRTC AEC3) behind the same interface, so the pipeline, room groups and ducking logic all get tested.
- **Echo test:** the sim convolves each unit's speaker output with a room impulse response (classroom about 0.6 s, gym about 2 s) and adds it to the mic signal.
- Final AEC tuning is done on real hardware.

## 2. Network and power

- **Room number = IP address.**
  - The board has no DIP switches. You set the number on the unit: hold a button combo, then step the digits with PB1/PB2/PB4/PB5 (read through the TM1637) while the 3-digit display shows the number.
  - The number is stored in flash. Room 214 becomes x.x.x.214, and a letter suffix marks group members (214a, 214b).
  - A fixed IP skips the DHCP wait.
- **Failover on portable units**
  - Wi-Fi stays connected in the background, so switching is just a change of sending link.
  - The W5500 link state is polled every 100 ms. Target is under 1 s to switch.
  - Battery switchover is handled by the hardware power-OR. The AXP2101 reports battery charge.
- **Fast boot**
  - Audio comes up first. The camera stays off unless in emergency mode.
  - Target is talking within 2 s of power-on.
- **LoRa (RFM95): not defined yet.**
  - Likely role: alerts and status only (e.g. "lockdown", "unit 214 on battery"). It's too slow for voice.
  - Idea: a 3rd-floor unit acts as a LoRa repeater for coverage around the building and the parking lot.
  - Later, the repeaters may also serve as LoRa weather stations with a radiation sensor. That's a separate project.

## 3. Office console

A PC app (Qt, the same toolkit as the simulator):
- Room grid with online / PoE / Wi-Fi / battery status for each unit.
- All-call, talk to one room or group, listen-in.
- Alarm button that starts emergency mode on all or selected units.
- Video grid during emergencies.

It talks to simulated units and real units the same way.

## 4. Emergency mode

- Starts from the console alarm, or from a unit's button combo.
- Each unit then:
  1. Opens the cover servo (PCA9685).
  2. Lights a "camera live" indicator.
  3. Starts RTSP MJPEG (VGA, 10–15 fps) for the NVR and console.
- **Outside emergencies:** the camera stays powered (TCA9555 P03 on) and covered.
- **Camera self-test**
  - The inside of the cover is printed with the room number.
  - The unit takes a periodic low-res snapshot of the cover and compares it with a reference stored at install time. That proves the camera, its power and the cover position all work, with no live view of the room.
  - The console can show the latest cover snapshot, which shows the room number, for a visual check.
  - A failed match raises a fault on the console. Possible causes: camera dead, cover open, or cover missing.
- Cover open/closed is confirmed by the camera image itself (room number visible = closed).

## 5. Tracking and laser

- On-device person detection (ESP-DL, a few fps) steers pan/tilt. The detection-to-aim path stays on the unit for speed.
- The laser module is switched by one MISC.OUT P-FET.
- **Safety:** Class 2 (≤1 mW) only. Aim at the torso and never the head; tracking keeps aim below the detected face box.

## Network load test

**Done:** see [network_sim_results.md](network_sim_results.md).

Findings:
- Each unit must send audio ahead of its own video. Without that, 720p-size frames cause 6.9 % audio dropouts; with it, latency stays around 1 ms.
- The NVR port must be gigabit.
- 100 Mbit/s floor uplinks are enough for VGA video.

The original plan follows.

The load test simulates the whole school (35–45 units, probably on the server downstairs) during an emergency, with every cover open and every camera streaming. Audio must stay clean.

| Stream | Per unit | All 35 rooms |
|---|---|---|
| Audio, raw PCM now (only talkers send) | 262 kbit/s per talker | ~1 Mbit/s with 4 talkers (multicast) |
| Audio, Opus later | ~24 kbit/s per talker | ~0.1 Mbit/s |
| Video, MJPEG VGA 10–15 fps, 25–40 KB/frame | 2–5 Mbit/s | 70–175 Mbit/s |

- **Simulated video:** sim units send dummy MJPEG frames at the real size and rate when emergency mode starts.
- **Measurements:** audio loss, jitter-buffer concealment and latency, with and without video.
- **Network design to check:**
  - gigabit uplinks to the office and NVR,
  - audio priority over video (DSCP / switch QoS),
  - one stream per unit, to the NVR only; the console views through the NVR,
  - lower resolution or frame rate for idle rooms,
  - little or no video over Wi-Fi.

## Open questions / to verify

- Whether ESP-SR AFE is usable from Arduino-ESP32 3.x under PlatformIO, or whether this needs ESP-IDF directly.
- Polled W5500 support under Arduino-ESP32 3.x.
- CPU and RAM budget with AFE, Opus and the camera running at the same time (the camera is only on in emergencies).
- Whether outdoor Wi-Fi covers the parking lot (needs an outdoor AP if not).
- LoRa protocol and repeater design.
- **Hardware issue to fix:** camera D1/D2 sit on GPIO45/46, which are strapping pins. With the camera always powered, a
  reset while the camera drives GPIO45 high selects 1.8 V flash and the board won't boot.
  Fix either way:
  - burn the VDD_SPI eFuses at provisioning, or
  - move those two lines in the next board spin.
- Ethernet and Wi-Fi on the same subnet with the same IP: lwIP routing and IGMP per interface need a hardware test.
- Auto-tune analysis takes ~0.5 s on a PC at 1024 taps; on the S3 it must run in a low-priority task.
- Network console commands need a PIN or key before deployment.

Simulator status: see [firmware/README.md](../firmware/README.md).

Firmware goes in `firmware/` in this repo, and the simulator goes in `firmware/sim/`.
The hardware (KiCad files) is still a draft: the placement is done, but the 63 new connections are unrouted.
