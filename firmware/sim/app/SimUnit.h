// One simulated intercom unit: runs the real firmware core (ic::Intercom) on simulated
// hardware. Implements the core's HAL interfaces with Qt / Windows pieces:
//   network  UDP multicast 239.255.42.1:5004 (several simulators on one PC or LAN talk)
//   audio    synthetic talker or PC mic in, simulated room echo, PC speakers out
//   power    PoE cable, battery, boot time;  links: Ethernet PHY, Wi-Fi association
#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>
#include <vector>

#include "Intercom.h"
#include "NlmsAec.h"
#include "RoomEcho.h"
#include "TestVoice.h"
#include "WinAudio.h"

class QUdpSocket;

enum class MicMode { Test = 0, Pc = 1, Silent = 2 };

struct SimOptions {
    QString name = "214";
    QString dataDir;
    int room = -1;  // -1: from name / saved settings
    int sub = 0;
    MicMode mic = MicMode::Test;
    sim::RoomPreset echo = sim::RoomPreset::Classroom;
    bool listen = false;
    bool poe = true;
    bool wifi = true;
    bool battery = false;
    int hwDelayMs = 12;  // simulated I2S DMA + converter latency on the echo path (auto-tune finds it)
    double autoTalkAt = -1, autoTalkFor = 0;
    double quitAfter = -1;
};

class SimUnit : public QObject,
                public ic::INet,
                public ic::ILinks,
                public ic::IPower,
                public ic::IPanel,
                public ic::IStore,
                public ic::ILog {
    Q_OBJECT
public:
    explicit SimUnit(const SimOptions& opt, QObject* parent = nullptr);
    ~SimUnit() override;
    void start();

    // Controls (UI / command line).
    void setButton(int b, bool down);
    void setPoe(bool on);
    void setWifiInRange(bool on);
    void setHasBattery(bool on);
    void setBattery(double pct);
    void setMicMode(MicMode m);
    void setEcho(sim::RoomPreset p) { room_.setPreset(p); }
    QString command(const QString& line);  // console, same as USB serial / network
    void setListen(bool on);

    // State for the UI.
    const SimOptions& options() const { return opt_; }
    bool powered() const { return powered_; }
    bool booting() const { return powered_ && !ic_; }
    const ic::Intercom* intercom() const { return ic_.get(); }
    const ic::AecStats* aecStats() const { return aec_ ? &aec_->stats() : nullptr; }
    void display(char text[3], uint8_t* dots) const;
    double battery() const { return battery_; }
    bool poe() const { return poe_; }
    bool hasBattery() const { return hasBattery_; }
    bool wifiInRange() const { return wifiInRange_; }
    bool ethernetPhyUp() const { return ethUp_; }
    bool wifiAssociated() const { return wifiAssoc_; }
    bool pcMicOk() const { return in_.isOpen(); }
    bool pcSpeakerOk() const { return out_.isOpen(); }

    // HAL (called by the core).
    void send(const uint8_t* data, size_t len) override;
    size_t recv(uint8_t* buf, size_t cap) override;
    bool ethernetUp() override { return ethUp_; }
    bool wifiUp() override { return wifiAssoc_; }
    void useLink(ic::LinkType l) override { link_ = l; }
    ic::PowerState read() override;
    uint8_t buttons() override { return buttons_ | autoTalk_; }
    void show(const char text[3], uint8_t dots) override;
    ic::UnitId loadUnit() override;
    void saveUnit(const ic::UnitId& id) override;
    void log(const char* msg) override;
    std::string loadText(const char* key) override;
    void saveText(const char* key, const std::string& value) override;

signals:
    void logLine(const QString& line);

private slots:
    void tick();

private:
    void powerUpdate();
    void processFrame(int16_t* mic);
    void printSummary();

    SimOptions opt_;
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 framesRun_ = 0;
    qint64 lastBatt_ = 0;
    QUdpSocket* sock_ = nullptr;

    std::unique_ptr<ic::NlmsAec> aec_;
    std::unique_ptr<ic::Intercom> ic_;
    sim::RoomEcho room_;
    sim::TestVoice voice_;
    WinAudioIn in_;
    WinAudioOut out_;
    MicMode mic_;
    std::vector<int16_t> hwLine_;  // speaker -> echo path delay line
    size_t hwPos_ = 0;

    bool poe_, wifiInRange_, hasBattery_;
    double battery_ = 80;
    bool powered_ = false;
    qint64 bootAt_ = 0;
    bool ethUp_ = false, wifiAssoc_ = false;
    qint64 ethChangeAt_ = 0, wifiChangeAt_ = 0;
    ic::LinkType link_ = ic::LinkType::None;
    uint8_t buttons_ = 0, autoTalk_ = 0;
    char text_[3] = {' ', ' ', ' '};
    uint8_t dots_ = 0;
    int16_t lastSpk_[ic::kFrameSamples] = {};
    uint32_t rng_ = 12345;
    double erleSum_ = 0;
    int erleN_ = 0;
    long sentFrames_ = 0;
};
