# v0.1b: remove the second ESP32 (U8)

U8 was a second ESP32-S3 used only as a port expander. It could not have worked as wired: it is the same
ESP32-S3R8 as U7, whose GPIO33-37 belong to the in-package octal PSRAM, yet MISC.OUT1-3 and the PMU I2C were
on those pins. This change replaces it with two I2C chips on U7, and switches the microphones to PDM.

## New parts

| Ref | Part | LCSC | Job |
|---|---|---|---|
| U20 | PCA9685PW (TSSOP-28) | C2678753 | 16-channel 12-bit PWM, I2C 0x40 |
| U21 | TCA9555PWR (TSSOP-24) | C465732 | 16-bit I/O expander, I2C 0x20 |
| R (x2) | 4.7k 0402 | C25900 (basic) | I2C pull-ups to +3V3 |
| R (x4) | 4.7k 0402 | C25900 (basic) | servo signal pull-ups (PCA9685 runs open-drain) |
| C (x2) | 100nF 0402 | basic | decoupling |
| U16, U17 | MSM261DGT003 (LGA-6 2x4, top port, PDM) | C48227730 | replaces out-of-stock MSM261S4030H0R (I2S) |
| (U2 spot) | 0 ohm link | basic | U2 sat in series VBUS -> Vdrive |

## U7 pin changes

GPIO9/10/11/15 were re-ordered on 15 Nov 2026 to match the PCB fan-out under U7 (pins 14-16 and 21).
The SPI bus now runs through the GPIO matrix, which is fine for the W5500 at the speeds used.

| GPIO | Was | Now |
|---|---|---|
| 43 (U0TXD) | UART to U8 / J3 | I2C.SCL (J3 stays as an I2C/debug header) |
| 44 (U0RXD) | UART to U8 / J3 | I2C.SDA |
| 9 | ETH.RST (via R27) | ETH.MOSI (SPI bus shared by W5500 and RFM95) |
| 15 | CAM.PWR (via R15) | MIC.CLK (PDM clock to both mics) |
| 10 | ETH.INT (via R26) | LORA_IRQ (RFM95 DIO0) |
| 11 | W5500 MOSI, TM1637 shared | LORA_CS |
| 12 / 13 | W5500 MISO / SCLK, TM1637 shared 13 | SPI MISO / SCLK, shared by W5500 and RFM95; TM1637 moved off |
| 7 | I2S mic data | PDM mic data (both mics, L/R by solder jumper) |
| 8, 16, 21 | I2S WS / BCLK / DOUT shared with mics | speaker amp only (I2S1) |

Console and flashing use native USB (GPIO19/20). W5500 runs in polled mode (ESP-IDF >= 5.2).

## PCA9685 (U20) channels - open-drain mode (MODE2.OUTDRV = 0)

| Ch | Net | Load |
|---|---|---|
| 0 | RC.PWR | Q2 gate (servo 5 V) via existing 1M; low = on, released = off |
| 1-4 | RC.PAN, RC.TILT, RC.ROT, RC.OTHR | servo signals, 4.7k pull-up to +3V3 |
| 5-7 | LED.WH, LED.IR, LED.OTH | Q3-Q5 lamp P-FETs, same scheme |
| 8-10 | MISC.OUT1-3 | Q6-Q8 P-FETs, same scheme |
| 11-15 | spare | |

Open-drain fixes the v0.1 bug where a 3.3 V GPIO could not fully turn off a 5 V P-FET: released, the 10M
pull-up takes the gate to the source. /OE is driven by the TCA9555 (high at boot = outputs off).

## TCA9555 (U21) pins

| Pin | Net | Dir |
|---|---|---|
| P00 | TM.CLK (TM1637 CLK, keeps R38 pull-up) | out |
| P01 | TM.DIO (TM1637 DIO, keeps R39 pull-up) | in/out |
| P02 | PWM.OE (PCA9685 /OE) | out |
| P03 | CAM.PWR (Q1 gate via R15) | out |
| P04 | LORA_RST | out |
| P05 | PMU.IRQ (AXP2101 IRQ) | in |
| P06 | ETH.INT_EXP (W5500 INT via R26) | in |
| P07 | ETH.RST_EXP (W5500 RST via R27) | out |
| P10-P17 | spare | |

Channel and port order was chosen to match the PCB placement (fewest crossings): RC.PWR sits next to the
servo nets on the side towards R47, and PWM.OE sits on P02, directly under U20's /OE pin.

## Removed

U8 and everything only it used: U10 flash, Y2, Y3, U8 RF network (ANT2, C98, C99, L10) and +3V3_RF2,
second USB-C (J2, U4, D2, R12, R13, TST-R4), BOOT2, RST3, J4, JP1, JP2, U2 (MT9221 current sensor),
the voltage-sense dividers (R68-R73), U8 decoupling, and U8-only test points.

U12 (DS18B20) and D9 (WS2812) were removed here too, then restored (see below).

## Restored after U8 removal

U7 has no free GPIO (GPIO26 and GPIO33-37 belong to the in-package PSRAM), and neither part can run through
the TCA9555 because of its timing, so:

| Ref | Part | LCSC | Connection |
|---|---|---|---|
| D9 | WS2812B 1615 RGB status LED (bottom side, original spot) | C5349954 | DIN on GPIO0 (CHIP_BOOT), shared with the BOOT button. DOUT unused. Powered from +3V3 through TST-R19 (0 ohm), C58 100nF. |
| U12 | LM75BDP,118 temperature sensor, TSSOP-8 (top side, old DS18B20 spot) | C33099 (Extended) | I2C.SCL/SDA, address 0x48 (A0-A2 low). OS output unused. C49 100nF. Replaces the DS18B20, which needed its own GPIO. |

GPIO0 is a strapping pin: it must read high at reset for a normal boot. The LED's data input is high
impedance, so the internal pull-up still wins and boot is unaffected; the LED may show a random colour
until the firmware first writes it. Drive the LED only after boot, and never hold GPIO0 low.

## Firmware notes

- I2C on GPIO43/44 at 400 kHz: PCA9685 0x40, TCA9555 0x20, AXP2101 0x34.
- PCA9685: 50 Hz, OUTDRV=0 (open-drain), use full-on/full-off bits for lamps and outputs.
- Mics: I2S0 in PDM RX mode, CLK GPIO15, DATA GPIO7, stereo (L/R jumpers JP7/JP8).
- Speaker: I2S1 TX, BCLK GPIO16, WS GPIO8, DOUT GPIO21.
- LoRa (RFM95/SX1276): SPI MOSI/MISO/SCLK on GPIO9/12/13 shared with W5500, CS GPIO11, DIO0 GPIO10, RST via TCA9555 P04.
- Vdrive is now VBUS directly (no current sensor).
- Status LED: WS2812 on GPIO0 via RMT (one pixel, GRB).
- PCB temperature: LM75B at I2C 0x48, read the 11-bit temperature register (0.125 C per step).
