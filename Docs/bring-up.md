# First-board bring-up checklist

For the first 2–3 assembled boards. Each step says what to run and what a pass looks like.
Commands go in the USB console (115200 baud on the native USB-C port, e.g. `pio device monitor`),
or come from `firmware/tools/update-unit.bat`.

## 0. Before power

- [ ] Visual check: ESP32-S3 module, W5500, TCA9555 (U21), PCA9685 (U20), TM1637 (U11), AXP2101, amp, mics.
- [ ] Resistance from +3V3 and +5V to ground: no short.
- [ ] Which ESP32-S3 module was fitted? The eFuse step below assumes the **N16R8 (3.3 V flash)**. Don't burn it on a 1.8 V-flash module.

## 1. First power on USB

- [ ] The board enumerates as a USB serial port (Espressif VID 303A).
- [ ] `pio run -t upload`, or `update-unit.bat setup --flash` once a release exists.
- [ ] The console prints `unit 0, ip 10.42.0.0, aec NLMS, default tuning: ...` at boot.
- [ ] `version` replies `firmware 0.2.0` (or newer).

## 2. Strapping-pin fix (camera on GPIO45/46)

The camera stays powered and drives GPIO45 during a reset, which can select 1.8 V flash and stop the board booting.

- [ ] Burn the flash-voltage eFuse once per board: `update-unit.bat setup --burn-efuse` (asks you to type BURN). **This is permanent.**
- [ ] Reset with the camera fitted 10 times (EN button, or power cycle): it must boot every time.
- [ ] Long term: move camera D1/D2 off GPIO45/46 in the next board spin.

## 3. Board self-test

- [ ] `selftest`, then `selftest result` after 2 s. Every line should start with `ok`:
  - I2C: TCA9555 (0x20), PCA9685 (0x40), AXP2101 (0x34)
  - TM1637 display answers
  - Ethernet link (plug a PoE cable in first)
  - PSRAM size (should be 8192 KB)
  - speaker→mic: the tone is heard at least 15 dB above the quiet room
- [ ] Anything `FAIL`: check that chip's supply, I2C pull-ups and solder joints.

## 4. Front panel

- [ ] The display shows the room number (`  0` before it's set). Blinking `---` means no network link.
- [ ] Press each button and check what it does:
  - TALK (PB1): the first decimal point lights while held, and the other board plays this one's mic.
  - MODE (PB2) held 3 s: room setup starts (the first digit blinks).
  - UP (PB4) / DOWN (PB5): change digits in setup.
  - UP alone held 5 s: auto-tune starts (`c..` on the display).
- [ ] If a button does the wrong thing, fix the table in `firmware/src/board/Panel.h`. The PB → function mapping is a guess until the case labels are final.
- [ ] Known limit: the TM1637 reports one key at a time, so two-button combos can't work.

## 5. Audio

- [ ] `tone 2`: a clear 1 kHz tone from the speaker, no buzz or distortion.
- [ ] `talk 5` on board A with board B on the same switch: B plays A's mic for 5 s.
- [ ] Both directions; then both at once (each board's own voice must not come back).
- [ ] `cal` in a quiet room, then `cal result`. Note the delay, RT60 and ERLE ("echo reduction"). Compare with the simulator's 12 ms / 0.5 s / ~19 dB.
- [ ] Only one PDM mic is used so far. Check which of the two it is.

## 6. Network

- [ ] `unit 214`: the console says `unit 214 ip 10.42.20.14`, and the board answers ping at that address without a reboot.
- [ ] Set the keys: `webpass <password>` and `netkey <key>` (or `update-unit.bat setup --unit 214 --webpass ... --netkey ...`).
- [ ] Browse to `http://10.42.20.14`: log in, check the status page updates every second, run the test tone and auto-tune from the page.
- [ ] Office console (`intercom-console --key <key>`): both boards show, `stats` to one board gets a reply, all-call is heard on both.
- [ ] Pull the PoE cable: the board shows OFFLINE in the console within 4 s.

## 7. Record

- [ ] Write down per board: module type, eFuse burned (y/n), self-test result, auto-tune result, anything reworked.
- [ ] Send the auto-tune numbers back into the simulator presets if they differ a lot.

## Not on the first boards yet

Wi-Fi failover, battery level (AXP2101 driver), the second mic, ESP-SR echo canceller, the camera and cover servo.
See [roadmap.md](roadmap.md).
