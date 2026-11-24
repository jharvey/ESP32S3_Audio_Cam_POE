// ESP32-S3 (U7) pin map, taken from the schematic netlist after the 2026-11-22 pin remap
// (pins re-assigned to untangle the ratsnest for the single-sided layout), and the 2026-11-24 camera remap:
// the camera's fast lines (D0-D7, PCLK, XCLK) moved to U7's left and bottom edges, nearest the FPC, so they
// reach it on the top layer without crossing USB (pins 25/26). Then SD and Ethernet moved to the right edge
// (pins 6-11, GPIO1-6): their lines leave the bottom-right corner and pass under the camera bundle to the SD
// card and W5500 (lower left) without crossing it.
// Regenerate this if the schematic changes.
#pragma once

namespace pins {

constexpr int RGB_LED_GPIO = 0;  // D9 WS2812B data on GPIO0 via R110 33R (CHIP_BOOT strap: driven only after
                                 // boot; R109 10k pull-up holds it high at reset; C71 debounce cap not fitted)

// I2C bus: PCA9685 0x40 (U20), TCA9555 0x20 (U21), AXP2101 0x34 (U18)
constexpr int I2C_SCL = 43;
constexpr int I2C_SDA = 12;

// Shared SPI: W5500 Ethernet (U9) and RFM95 LoRa (U19)
constexpr int SPI_MOSI = 6;     // GPIO matrix
constexpr int SPI_MISO = 5;
constexpr int SPI_SCLK = 4;
constexpr int ETH_CS = 13;
constexpr int LORA_CS = 11;
constexpr int LORA_DIO0 = 10;  // LORA_IRQ

// PDM microphones U16/U17 (L/R by JP7/JP8) on I2S0
constexpr int MIC_CLK = 9;
constexpr int MIC_DATA = 7;

// Speaker amp MAX98357A (U15) on I2S1
constexpr int SPK_BCLK = 16;  // net /ESP.MIC.SCK (XTAL_32K_N, free: no 32 kHz crystal fitted)
constexpr int SPK_WS = 8;     // net /ESP.I2S.WS
constexpr int SPK_DOUT = 14;  // net /SPK.DIN

// SD card, SDMMC 1-bit (DAT3 pulled up by R29, not on a GPIO)
constexpr int SD_CLK = 2;
constexpr int SD_CMD = 3;    // net /SD.MOSI. GPIO3 is the JTAG-source strap, ignored unless its eFuse is burned
constexpr int SD_D0 = 1;

// Camera (OV2640 / OV5640). No PWDN/RESET GPIOs; power via TCA9555 P03.
constexpr int CAM_XCLK = 45;  // net /CAM.MCLK, U7 pin 51. Strapping pin, but only the ESP32 drives it
constexpr int CAM_SIOD = 15;  // CAM.SDA
constexpr int CAM_SIOC = 17;  // CAM.SCL
constexpr int CAM_VSYNC = 18;
constexpr int CAM_HREF = 21;
constexpr int CAM_PCLK = 41;  // U7 pin 47
constexpr int CAM_D0 = 39, CAM_D1 = 47, CAM_D2 = 48, CAM_D3 = 38;  // pins 44, 37 (SPICLK_P), 36 (SPICLK_N), 43
constexpr int CAM_D4 = 40, CAM_D5 = 42, CAM_D6 = 44, CAM_D7 = 46;  // pins 45, 48, 50 (U0RXD), 52 (strapping)
// GPIO45 (VDD_SPI voltage strap) carries XCLK, an ESP32 output: the camera can't hold it high at reset.
// GPIO46 carries D7, a camera output. GPIO46 only matters with GPIO0 low (download mode): GPIO0 = 0 and
// GPIO46 = 1 is not a valid boot mode. At power-up the camera is off (TCA9555 P04 is an input at reset, R14
// holds Q1 off), so this only bites if the camera is powered when BOOT is held. Keep it that way: power the camera off (CAM_PWR high) before
// any restart. Normal SPI boot (GPIO0 high) ignores GPIO46.
// GPIO3 (SD.MOSI = SD CMD) is the JTAG-source strap; it is ignored unless the STRAP_JTAG_SEL eFuse is burned.

// TCA9555 (U21) port bits
namespace exp {
constexpr int TM_CLK = 6;      // P06  TM1637 CLK (display + key scan)
constexpr int TM_DIO = 7;      // P07  TM1637 DIO
constexpr int PWM_OE = 0;      // P00  PCA9685 /OE, high = outputs LOW = every output switch off
constexpr int CAM_PWR = 4;     // P04  camera power (Q1 via R15)
constexpr int LORA_RST = 5;    // P05
constexpr int PMU_IRQ = 1;     // P01  AXP2101 IRQ, input
constexpr int ETH_INT = 3;     // P03  W5500 INT, input (W5500 runs polled)
constexpr int ETH_RST = 2;     // P02  W5500 reset (via R27)
// Port 1 (bits 8..15): the output switches' fault flags (SY6288 OCB, open-drain, low = over-current /
// thermal shutdown; TCA9555 internal pull-ups). The rotary switches that used these are gone.
constexpr int FLT_WH = 8, FLT_IR = 9, FLT_OTH = 10, FLT_M1 = 11;     // P10..P13
constexpr int FLT_M2 = 12, FLT_M3 = 13, FLT_RC = 14, P17_SPARE = 15;  // P14..P17
}  // namespace exp

// PCA9685 (U20) channels, push-pull (MODE2.OUTDRV = 1). Output switches are SY6288 (EN high = on, 100k
// pull-down: off at power-up, when U20 is unpowered and while /OE is high).
namespace pwm {
constexpr int RC_PWR = 15;                                         // servo 5 V switch (Q2), high = on
constexpr int RC_PAN = 1, RC_TILT = 2, RC_ROT = 4, RC_OTHER = 5;  // servo signals, 4.7k pull-ups
constexpr int LED_WHITE = 11, LED_IR = 14, LED_OTHER = 12;          // lamp switches Q3 Q4 Q5, high = on
constexpr int MISC1 = 8, MISC2 = 9, MISC3 = 10;                    // MISC.OUT switches Q6-Q8 (laser on one of these)
constexpr int EXP11 = 7, EXP12 = 6, EXP13 = 13, EXP14 = 3, EXP15 = 0;  // spare outputs on the expansion header
}  // namespace pwm

}  // namespace pins
