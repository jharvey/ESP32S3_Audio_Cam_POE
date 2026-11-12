// ESP32-S3 entry point: board drivers behind the core's HAL interfaces.
//
// STATUS: first draft, NOT COMPILED YET (needs the Arduino-ESP32 3.x platform, see
// platformio.ini). The core in src/core is tested in the PC simulator; the drivers here
// are written against the Arduino 3.x APIs and are marked TODO where hardware behaviour
// still has to be confirmed on a real board.
#include <Arduino.h>
#include <ESP_I2S.h>
#include <ETH.h>
#include <NetworkUdp.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

#include "Intercom.h"
#include "NlmsAec.h"
#include "board/Pins.h"

namespace {

// ---- I2C chips ---------------------------------------------------------------------
constexpr uint8_t kTca9555 = 0x20;
constexpr uint8_t kPca9685 = 0x40;

void i2cWrite(uint8_t addr, uint8_t reg, uint8_t v) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(v);
    Wire.endTransmission();
}

class Expander {
public:
    void begin() {
        out_ = 0;
        out_ |= 1u << pins::exp::TM_CLK | 1u << pins::exp::TM_DIO;  // TM1637 idle high
        out_ |= 1u << pins::exp::ETH_RST | 1u << pins::exp::LORA_RST;
        out_ |= 1u << pins::exp::PWM_OE;                              // servo/lamp outputs off until PCA9685 is set up
        out_ |= 1u << pins::exp::CAM_PWR;  // TODO: confirm Q1 polarity (camera stays powered)
        write();
        // Port 0 directions: 1 = input. PMU_IRQ and ETH_INT are inputs.
        i2cWrite(kTca9555, 6, uint8_t(1u << pins::exp::PMU_IRQ | 1u << pins::exp::ETH_INT));
        i2cWrite(kTca9555, 7, 0xff);  // port 1 spare: inputs
    }
    void set(int bit, bool on) {
        if (on) out_ |= uint8_t(1u << bit);
        else out_ &= uint8_t(~(1u << bit));
        write();
    }

private:
    void write() { i2cWrite(kTca9555, 2, out_); }
    uint8_t out_ = 0;
};

class Pwm {
public:
    void begin() {
        i2cWrite(kPca9685, 0x00, 0x10);  // MODE1: sleep (prescale can only be set asleep)
        i2cWrite(kPca9685, 0xFE, 121);   // 25 MHz / (4096 * 50 Hz) - 1
        i2cWrite(kPca9685, 0x01, 0x00);  // MODE2: OUTDRV = 0, open-drain
        i2cWrite(kPca9685, 0x00, 0x20);  // MODE1: awake, auto-increment
        delay(1);
        for (int ch = pins::pwm::RC_PWR; ch <= pins::pwm::MISC3; ch++) fullOff(ch);  // released = P-FETs off
    }
    // Open-drain: "full on" pulls the line low (P-FET on); "full off" releases it.
    void fullOn(int ch) { reg(ch, 0x1000, 0x0000); }
    void fullOff(int ch) { reg(ch, 0x0000, 0x1000); }
    void servoUs(int ch, int us) { reg(ch, 0, uint16_t(us * 4096L / 20000)); }  // 50 Hz frame

private:
    void reg(int ch, uint16_t on, uint16_t off) {
        Wire.beginTransmission(kPca9685);
        Wire.write(uint8_t(0x06 + 4 * ch));
        Wire.write(uint8_t(on));
        Wire.write(uint8_t(on >> 8));
        Wire.write(uint8_t(off));
        Wire.write(uint8_t(off >> 8));
        Wire.endTransmission();
    }
};

Expander gExp;
Pwm gPwm;
I2SClass gMic, gSpk;
NetworkUDP gUdp;
Preferences gPrefs;

// ---- HAL for the core ----------------------------------------------------------------
class Board : public ic::INet, public ic::ILinks, public ic::IPower, public ic::IPanel, public ic::IStore, public ic::ILog {
public:
    // Network: one UDP socket on the multicast group.
    // TODO: Ethernet and Wi-Fi as two netifs on the same subnet need care in lwIP (route
    // and IGMP join per interface). First hardware test: Ethernet only.
    void send(const uint8_t* d, size_t n) override {
        gUdp.beginPacket(IPAddress(239, 255, 42, 1), ic::kAudioPort);
        gUdp.write(d, n);
        gUdp.endPacket();
    }
    size_t recv(uint8_t* buf, size_t cap) override {
        int n = gUdp.parsePacket();
        return n > 0 ? size_t(gUdp.read(buf, cap)) : 0;
    }
    bool ethernetUp() override { return ETH.linkUp(); }
    bool wifiUp() override { return WiFi.status() == WL_CONNECTED; }
    void useLink(ic::LinkType) override {}  // TODO: set the default netif

    ic::PowerState read() override { return ic::PowerState(); }  // TODO: AXP2101 at 0x34

    // TODO: TM1637 display + key scan bit-banged through TCA9555 P00/P01. Each edge is an
    // I2C write, so run it in its own low-priority task, display only on change.
    uint8_t buttons() override { return 0; }
    void show(const char*, uint8_t) override {}

    ic::UnitId loadUnit() override {
        ic::UnitId id;
        id.room = gPrefs.getUShort("room", 0);
        id.sub = gPrefs.getUChar("sub", 0);
        return id;
    }
    void saveUnit(const ic::UnitId& id) override {
        gPrefs.putUShort("room", id.room);
        gPrefs.putUChar("sub", id.sub);
    }
    std::string loadText(const char* key) override { return gPrefs.getString(key, "").c_str(); }
    void saveText(const char* key, const std::string& v) override { gPrefs.putString(key, v.c_str()); }
    void log(const char* m) override { Serial.println(m); }
};

Board gBoard;
// TODO: replace with the ESP-SR AFE (AEC + NS + AGC) once it builds; NLMS is the fallback.
ic::NlmsAec gAec(1024, 0);
ic::Intercom* gIntercom = nullptr;

// USB serial console: loop() collects lines, the audio task runs them between frames so the
// core is only ever touched from one thread. Same commands as over the network ("help").
struct ConsoleLine { char text[160]; };
QueueHandle_t gConsole;

void audioTask(void*) {
    static int16_t mic[ic::kFrameSamples], spk[ic::kFrameSamples];
    for (;;) {
        ConsoleLine line;
        if (xQueueReceive(gConsole, &line, 0) == pdTRUE) {
            Serial.println(gIntercom->command(line.text).c_str());
        }
        // Blocks until 20 ms of mic samples are in: the I2S clock paces everything.
        if (gMic.readBytes(reinterpret_cast<char*>(mic), sizeof(mic)) != sizeof(mic)) continue;
        gIntercom->processFrame(mic, spk);
        gSpk.write(reinterpret_cast<uint8_t*>(spk), sizeof(spk));
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    gPrefs.begin("intercom", false);
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL, 400000);
    gExp.begin();
    gPwm.begin();
    gExp.set(pins::exp::PWM_OE, false);  // enable PCA9685 outputs (all released)

    gMic.setPinsPdmRx(pins::MIC_CLK, pins::MIC_DATA);
    gMic.begin(I2S_MODE_PDM_RX, ic::kSampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    gSpk.setPins(pins::SPK_BCLK, pins::SPK_WS, pins::SPK_DOUT);
    gSpk.begin(I2S_MODE_STD, ic::kSampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);

    // W5500 in polled mode: no INT pin on a GPIO (irq = -1), reset through the expander.
    gExp.set(pins::exp::ETH_RST, false);
    delay(2);
    gExp.set(pins::exp::ETH_RST, true);
    delay(10);
    ETH.begin(ETH_PHY_W5500, 1, pins::ETH_CS, -1, -1, SPI3_HOST, pins::SPI_SCLK, pins::SPI_MISO, pins::SPI_MOSI);

    // NLMS cost grows with taps; 1024 (64 ms) is the planned real-time limit on the S3 until
    // ESP-SR replaces it. TODO: auto-tune's analysis (~0.5 s on a PC, several s on the S3) must
    // move to a low-priority task so it does not stall the audio task.
    ic::IntercomOptions io;
    io.maxTaps = 1024;
    gConsole = xQueueCreate(4, sizeof(ConsoleLine));
    static ic::Intercom intercom(ic::Hal{&gBoard, &gBoard, &gBoard, &gBoard, &gBoard, &gBoard}, gAec, io);
    gIntercom = &intercom;
    intercom.begin();

    uint8_t ip[4];
    ic::ipForUnit(intercom.unit(), ip);
    ETH.config(IPAddress(ip[0], ip[1], ip[2], ip[3]), IPAddress(ip[0], ip[1], 0, 1), IPAddress(255, 255, 0, 0));
    gUdp.beginMulticast(IPAddress(239, 255, 42, 1), ic::kAudioPort);

    xTaskCreatePinnedToCore(audioTask, "audio", 16384, nullptr, 20, nullptr, 1);
}

void loop() {
    static ConsoleLine line;
    static size_t n = 0;
    while (Serial.available()) {
        char c = char(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            line.text[n] = 0;
            if (n) xQueueSend(gConsole, &line, 0);
            n = 0;
        } else if (n + 1 < sizeof(line.text)) {
            line.text[n++] = c;
        }
    }
    delay(10);
}
