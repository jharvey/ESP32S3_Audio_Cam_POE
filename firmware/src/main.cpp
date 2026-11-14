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
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include <atomic>
#include <functional>

#include "Intercom.h"
#include "NlmsAec.h"
#include "WebUi.h"
#include "board/Panel.h"
#include "board/Pins.h"

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

class Expander {
public:
    void begin() {
        I2cLock lock;
        out_ = 0;  // TM1637 CLK/DIO output latches stay 0: they are driven open-drain via cfg_
        out_ |= 1u << pins::exp::ETH_RST | 1u << pins::exp::LORA_RST;
        out_ |= 1u << pins::exp::PWM_OE;   // servo/lamp outputs off until PCA9685 is set up
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
        I2cLock lock;
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

void panelTask(void*) {
    uint32_t shown = 0xffffffff;
    for (;;) {
        {
            I2cLock lock;
            uint8_t code = 0xff;
            if (gTm.readKeys(&code)) gButtons = board::buttonsFromKey(code);
            uint32_t want = gDisplay.load();
            if (want != shown) {
                const char text[3] = {char(want), char(want >> 8), char(want >> 16)};
                gDisplayOk = gTm.show(text, uint8_t(want >> 24), 4);
                shown = want;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));  // ~25 key scans a second; each one is ~5 ms of I2C
    }
}
I2SClass gMic, gSpk;
NetworkUDP gUdp;
Preferences gPrefs;

// ---- HAL for the core ----------------------------------------------------------------
class Board : public ic::INet, public ic::ILinks, public ic::IPower, public ic::IPanel, public ic::IStore, public ic::ILog, public ic::IDiag {
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
    void setAddress(const ic::UnitId& id) override {
        uint8_t ip[4];
        ic::ipForUnit(id, ip);
        ETH.config(IPAddress(ip[0], ip[1], ip[2], ip[3]), IPAddress(ip[0], ip[1], 0, 1), IPAddress(255, 255, 0, 0));
        // TODO: Wi-Fi gets the same address once Wi-Fi is brought up.
    }

    ic::PowerState read() override { return ic::PowerState(); }  // TODO: AXP2101 at 0x34

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
        line(i2cPresent(kPca9685), "I2C: PCA9685 servo/lamp driver (0x40)");
        line(i2cPresent(kAxp2101), "I2C: AXP2101 power manager (0x34)");
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
    for (;;) {
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

}  // namespace

void setup() {
    Serial.begin(115200);
    gPrefs.begin("intercom", false);
    gI2c = xSemaphoreCreateRecursiveMutex();
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
    // ESP-SR replaces it.
    ic::IntercomOptions io;
    io.maxTaps = 1024;
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
    static ic::Intercom intercom(ic::Hal{&gBoard, &gBoard, &gBoard, &gBoard, &gBoard, &gBoard, &gBoard}, gAec, io);
    gIntercom = &intercom;
    intercom.begin();
    static ic::WebUi web(intercom);
    gWeb = &web;

    uint8_t ip[4];
    ic::ipForUnit(intercom.unit(), ip);
    ETH.config(IPAddress(ip[0], ip[1], ip[2], ip[3]), IPAddress(ip[0], ip[1], 0, 1), IPAddress(255, 255, 0, 0));
    gUdp.beginMulticast(IPAddress(239, 255, 42, 1), ic::kAudioPort);

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
