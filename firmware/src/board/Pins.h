// ESP32-S3 (U7) pin map, taken from the v0.1b schematic netlist (2026-11-12).
// Regenerate this if the schematic changes.
#pragma once

namespace pins {

// I2C bus: PCA9685 0x40 (U20), TCA9555 0x20 (U21), AXP2101 0x34 (U18)
constexpr int I2C_SCL = 43;
constexpr int I2C_SDA = 44;

// Shared SPI: W5500 Ethernet (U9) and RFM95 LoRa (U19)
constexpr int SPI_MOSI = 11;
constexpr int SPI_MISO = 12;
constexpr int SPI_SCLK = 13;
constexpr int ETH_CS = 14;
constexpr int LORA_CS = 9;
constexpr int LORA_DIO0 = 15;  // LORA_IRQ

// PDM microphones U16/U17 (L/R by JP7/JP8) on I2S0
constexpr int MIC_CLK = 10;
constexpr int MIC_DATA = 7;

// Speaker amp MAX98357A (U15) on I2S1
constexpr int SPK_BCLK = 16;  // net /ESP.MIC.SCK
constexpr int SPK_WS = 8;     // net /ESP.I2S.WS
constexpr int SPK_DOUT = 21;  // net /SPK.DIN

// SD card, SDMMC 1-bit (DAT3 pulled up by R29, not on a GPIO)
constexpr int SD_CLK = 5;
constexpr int SD_CMD = 6;
constexpr int SD_D0 = 4;

// Camera (OV2640 / OV5640). No PWDN/RESET GPIOs; power via TCA9555 P03.
constexpr int CAM_XCLK = 3;
constexpr int CAM_SIOD = 47;  // CAM.SDA
constexpr int CAM_SIOC = 48;  // CAM.SCL
constexpr int CAM_VSYNC = 1;
constexpr int CAM_HREF = 2;
constexpr int CAM_PCLK = 39;
constexpr int CAM_D0 = 41, CAM_D1 = 45, CAM_D2 = 46, CAM_D3 = 42;
constexpr int CAM_D4 = 40, CAM_D5 = 38, CAM_D6 = 17, CAM_D7 = 18;
// NOTE: GPIO45 and GPIO46 are strapping pins. GPIO45 selects the VDD_SPI voltage at reset;
// if the camera (now always powered) drives D1 high during a reset, the chip picks 1.8 V
// flash and fails to boot. Burn the VDD_SPI_FORCE / VDD_SPI_TIEH eFuses at provisioning,
// or move those camera data lines in the next board spin.

// TCA9555 (U21) port bits
namespace exp {
constexpr int TM_CLK = 0;      // P00  TM1637 CLK (display + key scan)
constexpr int TM_DIO = 1;      // P01  TM1637 DIO
constexpr int ETH_RST = 2;     // P02  W5500 reset (via R27)
constexpr int CAM_PWR = 3;     // P03  camera power (Q1 via R15)
constexpr int LORA_RST = 4;    // P04
constexpr int PMU_IRQ = 5;     // P05  AXP2101 IRQ, input
constexpr int PWM_OE = 6;      // P06  PCA9685 /OE, high = outputs off
constexpr int ETH_INT = 7;     // P07  W5500 INT, input (W5500 runs polled)
}  // namespace exp

// PCA9685 (U20) channels, open-drain (MODE2.OUTDRV = 0)
namespace pwm {
constexpr int RC_PAN = 0, RC_TILT = 1, RC_ROT = 2, RC_OTHER = 3;  // servo signals, 4.7k pull-ups
constexpr int RC_PWR = 4;                                          // servo 5 V switch (Q2), low = on
constexpr int LED_WHITE = 5, LED_IR = 6, LED_OTHER = 7;            // lamp P-FETs, low = on
constexpr int MISC1 = 8, MISC2 = 9, MISC3 = 10;                    // MISC.OUT P-FETs (laser on one of these)
}  // namespace pwm

}  // namespace pins
