// ESP32-S3 entry point: board drivers behind the core's HAL interfaces.
//
// STATUS: compiles (Arduino-ESP32 3.2, see platformio.ini), not yet run on a board. The core
// in src/core is tested in the PC simulator; the drivers here are marked TODO where hardware
// behaviour still has to be confirmed on a real board.
#include <Arduino.h>
#include <ESP_I2S.h>
#include <ETH.h>
#include <NetworkUdp.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Update.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include <atomic>
#include <functional>

#include <esp_netif.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "Intercom.h"
#include "NlmsAec.h"
#include "WebUi.h"
#include "board/EspSrAec.h"
#include "board/Panel.h"
#include "board/Pins.h"
#include "board/Tm1637.h"

namespace {

// ---- I2C chips ---------------------------------------------------------------------
constexpr uint8_t kTca9555 = 0x20;
constexpr uint8_t kPca9685 = 0x40;
constexpr uint8_t kAxp2101 = 0x34;

// One lock for the I2C bus: the panel task, the audio task (self-test) and setup all use it.
SemaphoreHandle_t gI2c;
struct I2cLock {
    I2cLock() { xSemaphoreTakeRecursive(gI2c, portMAX_DELAY); }
    ~I2cLock() { xSemaphoreGiveRecursive(gI2c); }
};

bool i2cPresent(uint8_t addr) {
    I2cLock lock;
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void i2cWrite(uint8_t addr, uint8_t reg, uint8_t v) {
    I2cLock lock;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(v);
    Wire.endTransmission();
}

uint8_t i2cRead(uint8_t addr, uint8_t reg, bool* ok) {
    I2cLock lock;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    *ok = Wire.endTransmission(false) == 0 && Wire.requestFrom(addr, uint8_t(1)) == 1;
    return *ok ? uint8_t(Wire.read()) : 0;
}

// AXP2101 power manager. Register use from the datasheet; to confirm on the first board.
//   0x00 status 1: bit 5 = VBUS good (PoE or USB power present), bit 3 = battery present
//   0x18 bit 3 = fuel gauge on;  0xA4 = battery percent (fuel gauge)
class Pmu {
public:
    void begin() {
        bool ok;
        uint8_t v = i2cRead(kAxp2101, 0x18, &ok);
        if (ok) i2cWrite(kAxp2101, 0x18, uint8_t(v | 0x08));
        ok_ = ok;
    }
    ic::PowerState read() {
        ic::PowerState s;
        bool ok;
        uint8_t st = i2cRead(kAxp2101, 0x00, &ok);
        if (!ok) return s;  // no answer: report PoE, no battery (the safe default)
        s.source = (st & 0x20) ? ic::PowerSource::PoE : ic::PowerSource::Battery;
        s.hasBattery = st & 0x08;
        if (s.hasBattery) {
            uint8_t pct = i2cRead(kAxp2101, 0xA4, &ok);
            s.batteryPct = ok ? std::min<uint8_t>(pct, 100) : 0;
        }
        return s;
    }
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};
Pmu gPmu;

class Expander {
public:
    void begin() {
        I2cLock lock;
        out_ = 0;  // TM1637 CLK/DIO output latches stay 0: they are driven open-drain via cfg_
        out_ |= 1u << pins::exp::ETH_RST | 1u << pins::exp::LORA_RST;
        out_ |= 1u << pins::exp::PWM_OE;   // /OE high: PCA9685 outputs LOW (MODE2 OUTNE = 00) = all switches off
        out_ |= 1u << pins::exp::CAM_PWR;  // TODO: confirm Q1 polarity (camera stays powered)
        write();
        // Port 0 directions: 1 = input. PMU_IRQ and ETH_INT are inputs; TM1637 lines start
        // released (input, pulled up by R38/R39).
        cfg_ = uint8_t(1u << pins::exp::PMU_IRQ | 1u << pins::exp::ETH_INT | 1u << pins::exp::TM_CLK | 1u << pins::exp::TM_DIO);
        writeCfg();
        i2cWrite(kTca9555, 7, 0xff);  // port 1 spare: inputs
    }
    void set(int bit, bool on) {
        I2cLock lock;
        if (on) out_ |= uint8_t(1u << bit);
        else out_ &= uint8_t(~(1u << bit));
        write();
    }
    // Open-drain line: high = release (input), low = drive the 0 in the output latch.
    void openDrain(int bit, bool high) {
        uint8_t c = high ? uint8_t(cfg_ | (1u << bit)) : uint8_t(cfg_ & ~(1u << bit));
        if (c == cfg_) return;
        cfg_ = c;
        writeCfg();
    }
    // Input port 1 (P10..P17): the output switches' fault flags. 0xFF if the read fails.
    uint8_t port1() {
        I2cLock lock;
        Wire.beginTransmission(kTca9555);
        Wire.write(1);  // input port 1
        Wire.endTransmission(false);
        if (Wire.requestFrom(kTca9555, uint8_t(1)) != 1) return 0xff;
        return uint8_t(Wire.read());
    }
    bool input(int bit) {
        Wire.beginTransmission(kTca9555);
        Wire.write(0);  // input port 0
        Wire.endTransmission(false);
        if (Wire.requestFrom(kTca9555, uint8_t(1)) != 1) return true;
        return (Wire.read() >> bit) & 1;
    }

private:
    void write() { i2cWrite(kTca9555, 2, out_); }
    void writeCfg() { i2cWrite(kTca9555, 6, cfg_); }
    uint8_t out_ = 0, cfg_ = 0xff;
};

class Pwm {
public:
    // The output switches (SY6288) turn on when U20 drives their EN high. The PCA9685 powers up with
    // every output LOW (datasheet 7.5), /OE high also drives them LOW (OUTNE = 00), and each EN has a
    // 100k pull-down, so everything is off from power-up without firmware. safeOff() restates that
    // first thing at boot (all channels full off, push-pull) in case U20 kept state through an ESP32 reset.
    void safeOff() {
        i2cWrite(kPca9685, 0xFD, 0x10);  // ALL_LED_OFF_H: full off on every channel (the reset value)
        i2cWrite(kPca9685, 0x01, 0x04);  // MODE2: OUTDRV = 1 push-pull, INVRT = 0, OUTNE = 00
        on_ = tripped_ = 0;
    }
    void begin() {
        safeOff();
        i2cWrite(kPca9685, 0x00, 0x10);  // MODE1: sleep (prescale can only be set asleep)
        i2cWrite(kPca9685, 0xFE, 121);   // 25 MHz / (4096 * 50 Hz) - 1
        i2cWrite(kPca9685, 0x01, 0x04);  // MODE2: push-pull, /OE high = all LOW (off)
        i2cWrite(kPca9685, 0x00, 0x20);  // MODE1: awake, auto-increment
        delay(1);
        // LOW = switches off: servo power, lamps and MISC outputs.
        for (int ch : {pins::pwm::RC_PWR, pins::pwm::LED_WHITE, pins::pwm::LED_IR, pins::pwm::LED_OTHER,
                       pins::pwm::MISC1, pins::pwm::MISC2, pins::pwm::MISC3})
            fullOff(ch);
    }
    // Push-pull: "full on" drives the pin high (switch on), "full off" drives it low.
    // A channel whose switch reported a fault stays off until it is switched off and on again.
    void fullOn(int ch) {
        if (tripped_ & (1u << ch)) return;
        on_ |= uint16_t(1u << ch);
        reg(ch, 0x1000, 0x0000);
    }
    void fullOff(int ch) {
        on_ &= uint16_t(~(1u << ch));
        tripped_ &= uint16_t(~(1u << ch));
        reg(ch, 0x0000, 0x1000);
    }
    bool isOn(int ch) const { return on_ & (1u << ch); }
    // Over-current / thermal fault on an output that is on: switch it off and keep it off.
    void trip(int ch) {
        reg(ch, 0x0000, 0x1000);
        on_ &= uint16_t(~(1u << ch));
        tripped_ |= uint16_t(1u << ch);
    }
    uint16_t tripped() const { return tripped_; }
    void servoUs(int ch, int us) { reg(ch, 0, uint16_t(us * 4096L / 20000)); }  // 50 Hz frame

private:
    void reg(int ch, uint16_t on, uint16_t off) {
        I2cLock lock;
        Wire.beginTransmission(kPca9685);
        Wire.write(uint8_t(0x06 + 4 * ch));
        Wire.write(uint8_t(on));
        Wire.write(uint8_t(on >> 8));
        Wire.write(uint8_t(off));
        Wire.write(uint8_t(off >> 8));
        Wire.endTransmission();
    }
    volatile uint16_t on_ = 0, tripped_ = 0;
};

Expander gExp;
Pwm gPwm;

// ---- front panel: TM1637 through the expander, in its own low-priority task -----------
class ExpanderLines : public board::Tm1637Lines {
public:
    void clk(bool high) override { gExp.openDrain(pins::exp::TM_CLK, high); }
    void dio(bool high) override { gExp.openDrain(pins::exp::TM_DIO, high); }
    bool readDio() override { return gExp.input(pins::exp::TM_DIO); }
};
ExpanderLines gTmLines;
board::Tm1637 gTm(gTmLines);
std::atomic<uint8_t> gButtons{0};
std::atomic<uint32_t> gDisplay{0x00202020};  // 3 chars + dots, written by the audio task
std::atomic<bool> gDisplayOk{false};
std::atomic<uint8_t> gLamps[3];  // lamp levels from the core (audio task), applied by the panel task
std::atomic<uint32_t> gRgb{0};     // RGB LED colour 0xRRGGBB, applied by the panel task
std::atomic<bool> gButtonsOk{false};

// Output switch faults: each SY6288 pulls its OCB line low on over-current or thermal shutdown
// (shorted lamp cable, stalled servos). The switch already limits the current; firmware then turns
// that output off so a short can't keep loading the 5 V rail, and says so.
struct OutputFault {
    int ch, bit;
    const char* name;
};
const OutputFault kOutputs[] = {
    {pins::pwm::LED_WHITE, pins::exp::FLT_WH, "white lamp (J13)"}, {pins::pwm::LED_IR, pins::exp::FLT_IR, "IR lamp (J12)"},
    {pins::pwm::LED_OTHER, pins::exp::FLT_OTH, "call light (J11)"}, {pins::pwm::MISC1, pins::exp::FLT_M1, "MISC1 (J17)"},
    {pins::pwm::MISC2, pins::exp::FLT_M2, "MISC2 (J18)"},          {pins::pwm::MISC3, pins::exp::FLT_M3, "MISC3 (J19)"},
    {pins::pwm::RC_PWR, pins::exp::FLT_RC, "servo power"},
};
std::atomic<uint16_t> gOutputFaults{0};  // bit = PCA9685 channel tripped since boot (for the self-test)

void checkOutputFaults() {
    static uint8_t seen[8] = {0};
    uint8_t p1 = gExp.port1();
    if (p1 == 0xff) {  // read failed or no fault anywhere
        for (auto& s : seen) s = 0;
        return;
    }
    for (size_t i = 0; i < sizeof(kOutputs) / sizeof(kOutputs[0]); i++) {
        const OutputFault& o = kOutputs[i];
        bool fault = !(p1 & (1u << (o.bit - 8)));
        // Soft-start into a capacitive load can flag briefly: act on two scans in a row (~60 ms).
        seen[i] = (fault && gPwm.isOn(o.ch)) ? uint8_t(seen[i] + 1) : 0;
        if (seen[i] == 2) {
            gPwm.trip(o.ch);
            gOutputFaults = uint16_t(gOutputFaults.load() | (1u << o.ch));
            Serial.printf("OUTPUT FAULT: %s over-current or overheated, switched off\n", o.name);
        }
    }
}

void panelTask(void*) {
    uint32_t shown = 0xffffffff;
    for (;;) {
        checkOutputFaults();
        {
            I2cLock lock;
            // Buttons: U22 input ports 0 and 1 in one read (pressed = 0).
            Wire.beginTransmission(board::kButtonExpanderAddr);
            Wire.write(0);
            Wire.endTransmission(false);
            if (Wire.requestFrom(board::kButtonExpanderAddr, uint8_t(2)) == 2) {
                uint16_t in = uint16_t(Wire.read());
                in = uint16_t(in | uint16_t(Wire.read()) << 8);
                gButtons = board::buttonsFromInputs(in);
                gButtonsOk = true;
            } else {
                gButtons = 0;  // U22 missing: no stuck TALK
                gButtonsOk = false;
            }
            uint32_t want = gDisplay.load();
            if (want != shown) {
                const char text[3] = {char(want), char(want >> 8), char(want >> 16)};
                gDisplayOk = gTm.show(text, uint8_t(want >> 24), 4);
                shown = want;
            }
        }
        {
            static uint8_t applied[3] = {0, 0, 0};
            static const int ch[3] = {pins::pwm::LED_WHITE, pins::pwm::LED_IR, pins::pwm::LED_OTHER};
            for (int i = 0; i < 3; i++) {
                uint8_t v = gLamps[i].load() ? 1 : 0;  // on / off: the 50 Hz servo PWM would flicker a lamp
                if (v == applied[i]) continue;
                applied[i] = v;
                if (v) gPwm.fullOn(ch[i]);
                else gPwm.fullOff(ch[i]);
            }
            static uint32_t rgbShown = 0xffffffff;
            uint32_t rgb = gRgb.load();
            if (rgb != rgbShown) {
                rgbShown = rgb;
                rgbLedWrite(pins::RGB_LED_GPIO, uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb));  // RMT, GRB order handled
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));  // ~30 button reads a second; each one is a 2-byte I2C read
    }
}
I2SClass gMic, gSpk;
NetworkUDP gUdp;
Preferences gPrefs;

// ---- HAL for the core ----------------------------------------------------------------
class Board : public ic::INet, public ic::ILinks, public ic::IPower, public ic::IPanel, public ic::IStore, public ic::ILog, public ic::IDiag,
              public ic::ILamps {
public:
    void set(ic::Lamp lamp, uint8_t pct) override { gLamps[int(lamp)] = pct; }
    void setRgb(uint8_t r, uint8_t g, uint8_t b) override { gRgb = uint32_t(r) << 16 | uint32_t(g) << 8 | b; }
    // Network: one UDP socket on the multicast group. Ethernet and Wi-Fi carry the same IP
    // (10.42.<floor>.<room>); useLink() makes the active one lwIP's default interface, which is
    // where multicast goes out, and re-joins the group so IGMP is sent on it.
    // To check on hardware: unicast replies (web page) while both links are up.
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
    bool wifiUp() override { return wifiOn_ && WiFi.status() == WL_CONNECTED; }
    void useLink(ic::LinkType link) override {
        esp_netif_t* nif = nullptr;
        if (link == ic::LinkType::Ethernet) nif = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (link == ic::LinkType::WiFi) nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (nif) esp_netif_set_default_netif(nif);
        gUdp.stop();
        gUdp.beginMulticast(IPAddress(239, 255, 42, 1), ic::kAudioPort);
    }
    void setAddress(const ic::UnitId& id) override {
        uint8_t ip[4];
        ic::ipForUnit(id, ip);
        IPAddress me(ip[0], ip[1], ip[2], ip[3]), gw(ip[0], ip[1], 0, 1), mask(255, 255, 0, 0);
        ETH.config(me, gw, mask);
        if (wifiOn_) WiFi.config(me, gw, mask);
    }
    void setWifi(const std::string& ssid, const std::string& pass) override {
        WiFi.disconnect(true);
        wifiOn_ = !ssid.empty();
        if (!wifiOn_) {
            WiFi.mode(WIFI_OFF);
            return;
        }
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);  // power save adds 100+ ms of latency to multicast
        WiFi.setAutoReconnect(true);
        uint8_t ip[4];
        ic::ipForUnit(gIntercomUnit, ip);
        WiFi.config(IPAddress(ip[0], ip[1], ip[2], ip[3]), IPAddress(ip[0], ip[1], 0, 1), IPAddress(255, 255, 0, 0));
        WiFi.begin(ssid.c_str(), pass.c_str());  // stays connected in the background for fast failover
    }
    ic::UnitId gIntercomUnit;  // the unit's current id, for the Wi-Fi address

    ic::PowerState read() override { return gPmu.read(); }

    // Panel: the panel task does the slow I2C work; these just exchange the latest state.
    uint8_t buttons() override { return gButtons.load(); }
    void show(const char* t, uint8_t dots) override {
        gDisplay = uint32_t(uint8_t(t[0])) | uint32_t(uint8_t(t[1])) << 8 | uint32_t(uint8_t(t[2])) << 16 | uint32_t(dots) << 24;
    }

    // "selftest": which chips answer and what the links are doing.
    std::string boardTest() override {
        std::string r;
        auto line = [&r](bool ok, const char* what) {
            r += ok ? "ok " : "FAIL ";
            r += what;
            r += "\n";
        };
        line(i2cPresent(kTca9555), "I2C: TCA9555 I/O expander (0x20)");
        line(i2cPresent(board::kButtonExpanderAddr), "I2C: TCA9555 button expander U22 (0x21)");
        line(i2cPresent(kPca9685), "I2C: PCA9685 servo/lamp driver (0x40)");
        {
            uint8_t p1 = gExp.port1();
            bool ok = p1 != 0xff || i2cPresent(kTca9555);
            uint16_t tr = gOutputFaults.load();
            for (const auto& o : kOutputs) {
                bool flag = !(p1 & (1u << (o.bit - 8)));
                if (flag && !gPwm.isOn(o.ch)) ok = false;  // a switch that is off should never flag
                if (tr & (1u << o.ch)) {
                    char t[80];
                    snprintf(t, sizeof(t), "output %s tripped (over-current / overheated) since boot", o.name);
                    line(false, t);
                }
            }
            line(ok, "output switch fault lines idle (SY6288 OCB, U21 P10-P16)");
        }
        line(i2cPresent(kAxp2101), "I2C: AXP2101 power manager (0x34)");
        {
            ic::PowerState p = gPmu.read();
            char pb[80];
            snprintf(pb, sizeof(pb), "power: %s, battery %s %u %%", p.source == ic::PowerSource::PoE ? "PoE/USB" : "battery",
                     p.hasBattery ? "present" : "absent", unsigned(p.batteryPct));
            line(true, pb);
        }
        line(gDisplayOk.load(), "TM1637 display answers");
        line(ETH.linkUp(), "Ethernet link (PoE cable)");
        char b[96];
        snprintf(b, sizeof(b), "PSRAM %u KB, free heap %u KB", unsigned(ESP.getPsramSize() / 1024), unsigned(ESP.getFreeHeap() / 1024));
        line(ESP.getPsramSize() > 0, b);
        return r;
    }

    ic::UnitId loadUnit() override {
        ic::UnitId id;
        id.room = gPrefs.getUShort("room", 0);
        gIntercomUnit = id;
        return id;
    }
    void saveUnit(const ic::UnitId& id) override {
        gPrefs.putUShort("room", id.room);
        gIntercomUnit = id;
    }
    std::string loadText(const char* key) override { return gPrefs.getString(key, "").c_str(); }
    void saveText(const char* key, const std::string& v) override { gPrefs.putString(key, v.c_str()); }
    void log(const char* m) override { Serial.println(m); }

private:
    bool wifiOn_ = false;
};

Board gBoard;
// Echo canceller: ESP-SR's esp_aec, with the portable NLMS canceller as the fallback if it can't
// start. Next step (roadmap 1.6a): the full ESP-SR audio front end with both mics.
ic::NlmsAec gNlms(1024, 0);
board::EspSrAec* gEspAec = nullptr;
ic::Intercom* gIntercom = nullptr;

// USB serial console: loop() collects lines, the audio task runs them between frames so the
// core is only ever touched from one thread. Same commands as over the network ("help").
struct ConsoleLine { char text[160]; };
QueueHandle_t gConsole;

// Web interface on port 80. The WebServer runs in loop(); each request is handed to the audio
// task (which owns the core) and loop() waits for the answer.
WebServer gHttp(80);
ic::WebUi* gWeb = nullptr;
struct WebJob {
    const ic::HttpRequest* req;
    ic::HttpResponse* resp;
    SemaphoreHandle_t done;
};
QueueHandle_t gWebJobs;

void onHttp() {
    ic::HttpRequest req;
    req.method = gHttp.method() == HTTP_POST ? "POST" : gHttp.method() == HTTP_GET ? "GET" : "OTHER";
    req.path = gHttp.uri().c_str();
    req.authorization = gHttp.header("Authorization").c_str();
    req.xIntercom = gHttp.header("X-Intercom").c_str();
    if (gHttp.hasArg("plain")) req.body = gHttp.arg("plain").c_str();
    ic::HttpResponse resp;
    resp.status = 503;
    resp.body = "busy";
    static SemaphoreHandle_t done = xSemaphoreCreateBinary();
    WebJob job{&req, &resp, done};
    if (xQueueSend(gWebJobs, &job, 0) == pdTRUE) xSemaphoreTake(done, portMAX_DELAY);
    gHttp.sendHeader("Cache-Control", "no-store");
    gHttp.sendHeader("X-Frame-Options", "DENY");
    if (resp.askLogin) gHttp.sendHeader("WWW-Authenticate", ("Basic realm=\"" + resp.realm + "\"").c_str());
    gHttp.send(resp.status, resp.contentType.c_str(), resp.body.c_str());
}

void audioTask(void*) {
    static int16_t mic[ic::kFrameSamples], spk[ic::kFrameSamples];
    esp_task_wdt_add(nullptr);  // a hung audio loop restarts the unit (see setup)
    for (;;) {
        esp_task_wdt_reset();
        ConsoleLine line;
        if (xQueueReceive(gConsole, &line, 0) == pdTRUE) {
            Serial.println(gIntercom->command(line.text).c_str());
        }
        WebJob job;
        if (xQueueReceive(gWebJobs, &job, 0) == pdTRUE) {
            *job.resp = gWeb->handle(*job.req);
            xSemaphoreGive(job.done);
        }
        // Blocks until 20 ms of mic samples are in: the I2S clock paces everything.
        if (gMic.readBytes(reinterpret_cast<char*>(mic), sizeof(mic)) != sizeof(mic)) continue;
        gIntercom->processFrame(mic, spk);
        gSpk.write(reinterpret_cast<uint8_t*>(spk), sizeof(spk));
    }
}

const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power on";
        case ESP_RST_SW: return "software restart";
        case ESP_RST_PANIC: return "crash";
        case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
        case ESP_RST_BROWNOUT: return "brown-out (power dip)";
        case ESP_RST_EXT: return "reset button";
        default: return "other";
    }
}

// Firmware update from the SD card: /firmware.bin is written to the other OTA slot, renamed to
// /firmware.done (or /firmware.bad), and the unit restarts into it. No card or no file: nothing.
void sdUpdate() {
    SD_MMC.setPins(pins::SD_CLK, pins::SD_CMD, pins::SD_D0);
    if (!SD_MMC.begin("/sdcard", true)) return;  // 1-bit mode
    File f = SD_MMC.open("/firmware.bin");
    if (!f) {
        SD_MMC.end();
        return;
    }
    size_t size = f.size();
    Serial.printf("SD card: updating from /firmware.bin (%u bytes)\n", unsigned(size));
    bool ok = Update.begin(size) && Update.writeStream(f) == size && Update.end(true);
    f.close();
    SD_MMC.rename("/firmware.bin", ok ? "/firmware.done" : "/firmware.bad");
    SD_MMC.end();
    Serial.printf("SD card: update %s\n", ok ? "written, restarting" : (String("FAILED: ") + Update.errorString()).c_str());
    if (ok) {
        delay(200);
        ESP.restart();
    }
}

}  // namespace

void setup() {
    // Lamps, MISC outputs and servo power off before anything else (see Pwm::safeOff): the
    // PCA9685 turns them all on at power-up. Until a board revision adds an inverting stage, they
    // are on from power-up until here (ESP32 boot, about 0.3 s).
    gI2c = xSemaphoreCreateRecursiveMutex();
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL, 400000);
    gPwm.safeOff();
    Serial.begin(115200);
    Serial.printf("intercom firmware %s, reset: %s\n", ic::kFirmwareVersion, resetReason());
    sdUpdate();
    gPrefs.begin("intercom", false);
    gExp.begin();
    gPwm.begin();
    gPmu.begin();
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
    // ESP-SR replaces it.
    ic::IntercomOptions io;
    io.maxTaps = 1024;
    uint64_t mac = ESP.getEfuseMac();
    io.unitUid = uint32_t(mac ^ (mac >> 32));  // unique per board: finds two units set to the same room
    // Clock for timed events: SNTP in the background (the school's server if "ntp" was set over USB,
    // else public pools). Until it syncs, time() is near 1970 and the office console's "time" is used.
    {
        static String ntp = gBoard.loadText("ntp").c_str();
        configTime(0, 0, ntp.length() ? ntp.c_str() : "pool.ntp.org", "time.google.com");
    }
    io.utcNow = [] { return int64_t(time(nullptr)); };
    gConsole = xQueueCreate(4, sizeof(ConsoleLine));
    gWebJobs = xQueueCreate(1, sizeof(WebJob));
    // Auto-tune's analysis runs in a low-priority task on core 0, so audio keeps going.
    io.runBackground = [](std::function<void()> job) {
        auto* j = new std::function<void()>(std::move(job));
        xTaskCreatePinnedToCore(
            [](void* arg) {
                auto* f = static_cast<std::function<void()>*>(arg);
                (*f)();
                delete f;
                vTaskDelete(nullptr);
            },
            "autotune", 16384, j, 1, nullptr, 0);
    };
    gEspAec = new board::EspSrAec();
    ic::EchoCanceller& aec = gEspAec->ok() ? static_cast<ic::EchoCanceller&>(*gEspAec) : gNlms;
    Serial.printf("echo canceller: %s\n", gEspAec->ok() ? "ESP-SR" : "NLMS (ESP-SR did not start)");
    static ic::Intercom intercom(ic::Hal{&gBoard, &gBoard, &gBoard, &gBoard, &gBoard, &gBoard, &gBoard, &gBoard}, aec, io);
    gIntercom = &intercom;
    intercom.begin();
    static ic::WebUi web(intercom);
    gWeb = &web;

    uint8_t ip[4];
    ic::ipForUnit(intercom.unit(), ip);
    ETH.config(IPAddress(ip[0], ip[1], ip[2], ip[3]), IPAddress(ip[0], ip[1], 0, 1), IPAddress(255, 255, 0, 0));
    gUdp.beginMulticast(IPAddress(239, 255, 42, 1), ic::kAudioPort);

    // Task watchdog: the audio task must check in every frame; 3 s without it restarts the unit.
    esp_task_wdt_config_t wdt = {3000, 0, true};
    esp_task_wdt_reconfigure(&wdt);
    xTaskCreatePinnedToCore(audioTask, "audio", 16384, nullptr, 20, nullptr, 1);
    xTaskCreatePinnedToCore(panelTask, "panel", 4096, nullptr, 2, nullptr, 0);

    static const char* kHeaders[] = {"Authorization", "X-Intercom"};
    gHttp.collectHeaders(kHeaders, 2);
    gHttp.onNotFound(onHttp);  // every path goes to WebUi, which routes and checks the login
    gHttp.begin();
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
    gHttp.handleClient();
    delay(5);
}
