// ESP32-S3 (U7) pin map, taken from the schematic netlist after the 2026-11-22 pin remap
// (pins re-assigned to untangle the ratsnest for the single-sided layout).
// Regenerate this if the schematic changes.
#pragma once

namespace pins {

constexpr int RGB_LED_GPIO = 0;  // D9 WS2812B data on GPIO0 (CHIP_BOOT strap: driven only after boot)

// I2C bus: PCA9685 0x40 (U20), TCA9555 0x20 (U21), AXP2101 0x34 (U18)
constexpr int I2C_SCL = 43;
constexpr int I2C_SDA = 44;

// Shared SPI: W5500 Ethernet (U9) and RFM95 LoRa (U19)
constexpr int SPI_MOSI = 4;     // GPIO matrix
constexpr int SPI_MISO = 3;
constexpr int SPI_SCLK = 45;   // strapping pin: driven only by the ESP32, no pull-up
constexpr int ETH_CS = 42;
constexpr int LORA_CS = 5;
constexpr int LORA_DIO0 = 6;   // LORA_IRQ

// PDM microphones U16/U17 (L/R by JP7/JP8) on I2S0
constexpr int MIC_CLK = 9;
constexpr int MIC_DATA = 7;

// Speaker amp MAX98357A (U15) on I2S1
constexpr int SPK_BCLK = 16;  // net /ESP.MIC.SCK (XTAL_32K_N, free: no 32 kHz crystal fitted)
constexpr int SPK_WS = 8;     // net /ESP.I2S.WS
constexpr int SPK_DOUT = 2;   // net /SPK.DIN

// SD card, SDMMC 1-bit (DAT3 pulled up by R29, not on a GPIO)
constexpr int SD_CLK = 46;   // strapping pin: driven only by the ESP32, no pull-up
constexpr int SD_CMD = 41;
constexpr int SD_D0 = 1;

// Camera (OV2640 / OV5640). No PWDN/RESET GPIOs; power via TCA9555 P03.
constexpr int CAM_XCLK = 21;
constexpr int CAM_SIOD = 40;  // CAM.SDA
constexpr int CAM_SIOC = 39;  // CAM.SCL
constexpr int CAM_VSYNC = 38;
constexpr int CAM_HREF = 47;  // U7 pin 37, SPICLK_P = GPIO47
constexpr int CAM_PCLK = 15;
constexpr int CAM_D0 = 13, CAM_D1 = 11, CAM_D2 = 10, CAM_D3 = 12;
constexpr int CAM_D4 = 14, CAM_D5 = 17, CAM_D6 = 18, CAM_D7 = 48;  // D7: U7 pin 36, SPICLK_N = GPIO48
// GPIO45 and GPIO46 are strapping pins. After the remap they carry ETH.SCLK and SD.CLK, which only the
// ESP32 drives and which have no pull-ups, so nothing can hold them high during a reset.
// GPIO3 (ETH.MISO) is the JTAG-source strap; it is ignored unless the STRAP_JTAG_SEL eFuse is burned.

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
