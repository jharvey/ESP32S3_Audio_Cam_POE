#include "SimUnit.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>
#include <QUdpSocket>
#include <cmath>
#include <cstdio>

namespace {
constexpr qint64 kBootMs = 1500;       // power-on to audio running (target on the S3: < 2 s)
constexpr qint64 kEthAutonegMs = 1500; // W5500 link-up after the cable goes in
constexpr qint64 kWifiAssocMs = 1200;  // association when in range (kept up in the background after that)
const QHostAddress kGroup(QString::fromLatin1(ic::kMulticastGroup));
}  // namespace

SimUnit::SimUnit(const SimOptions& opt, QObject* parent)
    : QObject(parent), opt_(opt), mic_(opt.mic), poe_(opt.poe), wifiInRange_(opt.wifi), hasBattery_(opt.battery) {
    room_.setPreset(opt.echo);
    hwLine_.assign(size_t(std::max(0, opt.hwDelayMs)) * ic::kSampleRate / 1000, 0);
    QDir().mkpath(opt_.dataDir);
}

SimUnit::~SimUnit() = default;

void SimUnit::start() {
    sock_ = new QUdpSocket(this);
    if (!sock_->bind(QHostAddress::AnyIPv4, ic::kAudioPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
        log(qPrintable("sim: UDP bind failed: " + sock_->errorString()));
    sock_->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
    sock_->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    if (!sock_->joinMulticastGroup(kGroup)) log(qPrintable("sim: multicast join failed: " + sock_->errorString()));

    setMicMode(mic_);
    setListen(opt_.listen);
    clock_.start();
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &SimUnit::tick);
    timer_.start(5);
    powerUpdate();
}

// ---- controls --------------------------------------------------------------------
void SimUnit::setButton(int b, bool down) {
    if (down) buttons_ |= uint8_t(1u << b);
    else buttons_ &= uint8_t(~(1u << b));
}
void SimUnit::setPoe(bool on) { poe_ = on; powerUpdate(); }
void SimUnit::setWifiInRange(bool on) { wifiInRange_ = on; }
void SimUnit::setHasBattery(bool on) { hasBattery_ = on; powerUpdate(); }
void SimUnit::setBattery(double pct) { battery_ = std::max(0.0, std::min(100.0, pct)); powerUpdate(); }
QString SimUnit::command(const QString& line) {
    if (!ic_) return "unit is off or booting";
    log(qPrintable("> " + line));
    QString reply = QString::fromStdString(ic_->command(line.toStdString()));
    for (const QString& l : reply.split('\n', Qt::SkipEmptyParts)) log(qPrintable("  " + l));
    return reply;
}

void SimUnit::setMicMode(MicMode m) {
    mic_ = m;
    if (m == MicMode::Pc && !in_.isOpen() && !in_.open()) log("sim: no PC microphone available");
    if (m != MicMode::Pc) in_.close();
}

void SimUnit::setListen(bool on) {
    if (on && !out_.isOpen() && !out_.open()) log("sim: no PC speaker available");
    if (!on) out_.close();
}

// ---- power and links ---------------------------------------------------------------
void SimUnit::powerUpdate() {
    bool want = poe_ || (hasBattery_ && battery_ > 0.5);
    if (want == powered_) return;
    powered_ = want;
    qint64 now = clock_.isValid() ? clock_.elapsed() : 0;
    if (powered_) {
        bootAt_ = now;
        log(poe_ ? "sim: power on (PoE), booting" : "sim: power on (battery), booting");
    } else {
        log("sim: power lost - unit off");
        ic_.reset();
        aec_.reset();
        ethUp_ = wifiAssoc_ = false;
        link_ = ic::LinkType::None;
        text_[0] = text_[1] = text_[2] = ' ';
        dots_ = 0;
    }
}

ic::PowerState SimUnit::read() {
    ic::PowerState p;
    p.source = poe_ ? ic::PowerSource::PoE : ic::PowerSource::Battery;
    p.hasBattery = hasBattery_;
    p.batteryPct = uint8_t(std::lround(battery_));
    return p;
}

// ---- network -------------------------------------------------------------------------
void SimUnit::send(const uint8_t* data, size_t len) {
    if (!sock_ || link_ == ic::LinkType::None) return;
    rng_ = rng_ * 1664525u + 1013904223u;
    if (link_ == ic::LinkType::WiFi && (rng_ >> 24) < 3) return;  // ~1 % loss on Wi-Fi
    if (len > 3 && data[3] == uint8_t(ic::PacketType::Audio)) sentFrames_++;
    sock_->writeDatagram(reinterpret_cast<const char*>(data), qint64(len), kGroup, ic::kAudioPort);
}

size_t SimUnit::recv(uint8_t* buf, size_t cap) {
    while (sock_ && sock_->hasPendingDatagrams()) {
        qint64 n = sock_->readDatagram(reinterpret_cast<char*>(buf), qint64(cap));
        if (n > 0 && link_ != ic::LinkType::None) return size_t(n);
    }
    return 0;
}

// ---- panel and storage ---------------------------------------------------------------
void SimUnit::show(const char text[3], uint8_t dots) {
    std::copy(text, text + 3, text_);
    dots_ = dots;
}

void SimUnit::display(char text[3], uint8_t* dots) const {
    if (booting()) {  // boot: all segments test pattern
        text[0] = text[1] = text[2] = '8';
        *dots = 0x07;
        return;
    }
    std::copy(text_, text_ + 3, text);
    *dots = dots_;
}

ic::UnitId SimUnit::loadUnit() {
    QSettings s(opt_.dataDir + "/unit.ini", QSettings::IniFormat);
    ic::UnitId id;
    if (s.contains("room")) {
        id.room = uint16_t(s.value("room").toInt());
        id.sub = uint8_t(s.value("sub").toInt());
    } else if (opt_.room >= 0) {
        id.room = uint16_t(opt_.room);
        id.sub = uint8_t(opt_.sub);
    } else {
        auto m = QRegularExpression("^(\\d{1,3})([a-d]?)$").match(opt_.name);
        if (m.hasMatch()) {
            id.room = uint16_t(m.captured(1).toInt());
            id.sub = m.captured(2).isEmpty() ? 0 : uint8_t(m.captured(2)[0].toLatin1() - 'a' + 1);
        }
    }
    return id;
}

void SimUnit::saveUnit(const ic::UnitId& id) {
    QSettings s(opt_.dataDir + "/unit.ini", QSettings::IniFormat);
    s.setValue("room", id.room);
    s.setValue("sub", id.sub);
}

std::string SimUnit::loadText(const char* key) {
    QSettings s(opt_.dataDir + "/unit.ini", QSettings::IniFormat);
    return s.value(key).toString().toStdString();
}

void SimUnit::saveText(const char* key, const std::string& value) {
    QSettings s(opt_.dataDir + "/unit.ini", QSettings::IniFormat);
    s.setValue(key, QString::fromStdString(value));
}

void SimUnit::log(const char* msg) {
    QString line = QString("%1  %2").arg(clock_.isValid() ? clock_.elapsed() / 1000.0 : 0.0, 7, 'f', 2).arg(msg);
    fprintf(stdout, "[%s] %s\n", qPrintable(opt_.name), qPrintable(line));
    fflush(stdout);
    emit logLine(line);
}

// ---- audio clock -----------------------------------------------------------------------
void SimUnit::tick() {
    qint64 now = clock_.elapsed();

    // Battery: drains on battery power, charges on PoE (sped up for the demo).
    double dt = (now - lastBatt_) / 1000.0;
    lastBatt_ = now;
    if (hasBattery_ && powered_) {
        if (poe_) battery_ = std::min(100.0, battery_ + dt * 0.5);
        else battery_ = std::max(0.0, battery_ - dt * 0.1);
        if (!poe_ && battery_ <= 0.5) powerUpdate();
    }

    // Boot finished: the firmware starts.
    if (powered_ && !ic_ && now - bootAt_ >= kBootMs) {
        aec_.reset(new ic::NlmsAec(2048, 0));
        ic::IntercomOptions io;
        io.maxTaps = 4096;  // a PC can run more; the S3 limit is set in the firmware
        ic_.reset(new ic::Intercom(ic::Hal{this, this, this, this, this, this}, *aec_, io));
        ic_->begin();
        ic::UnitId id = ic_->unit();
        voice_ = sim::TestVoice(100.0f + float((id.room * 37 + id.sub * 53) % 130), id.key(), -22.0f);
        log(qPrintable(QString("sim: booted in %1 ms").arg(now - bootAt_)));
    }
    // PHY and Wi-Fi association follow power and range, with their real-world delays.
    bool ethWant = powered_ && poe_;
    if (ethWant != ethUp_) {
        if (!ethWant) ethUp_ = false;
        else if (ethChangeAt_ == 0) ethChangeAt_ = now;
        else if (now - ethChangeAt_ >= kEthAutonegMs) ethUp_ = true;
    }
    if (ethWant == ethUp_) ethChangeAt_ = 0;
    bool wifiWant = powered_ && wifiInRange_;
    if (wifiWant != wifiAssoc_) {
        if (!wifiWant) wifiAssoc_ = false;
        else if (wifiChangeAt_ == 0) wifiChangeAt_ = now;
        else if (now - wifiChangeAt_ >= kWifiAssocMs) wifiAssoc_ = true;
    }
    if (wifiWant == wifiAssoc_) wifiChangeAt_ = 0;

    // Scripted talk for headless runs.
    double t = now / 1000.0;
    autoTalk_ = (opt_.autoTalkAt >= 0 && t >= opt_.autoTalkAt && t < opt_.autoTalkAt + opt_.autoTalkFor) ? 1 : 0;

    int16_t mic[ic::kFrameSamples];
    if (mic_ == MicMode::Pc && in_.isOpen()) {
        while (in_.read(mic)) {
            processFrame(mic);
            framesRun_++;
        }
    } else {
        qint64 due = now / ic::kFrameMs;
        if (due - framesRun_ > 10) framesRun_ = due - 10;  // window was stalled: don't spiral
        while (framesRun_ < due) {
            std::fill(mic, mic + ic::kFrameSamples, int16_t(0));
            processFrame(mic);
            framesRun_++;
        }
    }

    if (opt_.quitAfter > 0 && t >= opt_.quitAfter) {
        printSummary();
        QCoreApplication::exit(0);
    }
}

void SimUnit::processFrame(int16_t* mic) {
    if (!ic_) return;
    if (mic_ != MicMode::Pc) {
        // Speaker path latency (DMA buffers, DAC/ADC), then the room.
        size_t L = hwLine_.size();
        int16_t delayed[ic::kFrameSamples];
        for (int i = 0; i < ic::kFrameSamples; i++) {
            if (L) {
                delayed[i] = hwLine_[hwPos_];
                hwLine_[hwPos_] = lastSpk_[i];
                hwPos_ = (hwPos_ + 1) % L;
            } else {
                delayed[i] = lastSpk_[i];
            }
        }
        float echo[ic::kFrameSamples];
        room_.process(delayed, echo, ic::kFrameSamples);  // what the mic hears of our own speaker
        int16_t voice[ic::kFrameSamples] = {};
        if (mic_ == MicMode::Test && (buttons() & (1u << ic::kBtnTalk))) voice_.render(voice, ic::kFrameSamples);
        for (int i = 0; i < ic::kFrameSamples; i++) {
            rng_ = rng_ * 1664525u + 1013904223u;
            float noise = 3.0f * (float(rng_ >> 8) / float(1u << 24) - 0.5f);  // ~ -80 dBFS room noise
            float v = echo[i] + voice[i] + noise;
            mic[i] = int16_t(std::max(-32767.0f, std::min(32767.0f, v)));
        }
    }
    int16_t spk[ic::kFrameSamples];
    ic_->processFrame(mic, spk);
    std::copy(spk, spk + ic::kFrameSamples, lastSpk_);
    if (out_.isOpen()) out_.write(spk);
    const ic::AecStats& st = aec_->stats();
    if (st.farActive && !st.doubleTalk && st.converged) {
        erleSum_ += st.erleDb;
        erleN_++;
    }
}

void SimUnit::printSummary() {
    QString s;
    QTextStream o(&s);
    o << "SUMMARY unit " << opt_.name << ": ";
    if (!ic_) {
        o << "not running";
    } else {
        char u[8];
        ic::formatUnit(ic_->unit(), u);
        o << "id " << u << ", link " << int(ic_->link()) << ", sent " << sentFrames_ << " frames, peers " << ic_->peers().size()
          << ", heard";
        for (auto& h : ic_->heard()) {
            ic::UnitId id{uint16_t(h.first >> 8), uint8_t(h.first & 0xff)};
            ic::formatUnit(id, u);
            o << " " << u << "=" << h.second;
        }
        o << ", ERLE avg " << (erleN_ ? erleSum_ / erleN_ : 0.0) << " dB";
    }
    log(qPrintable(s));
    QFile f(opt_.dataDir + "/summary.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) f.write(s.toUtf8() + "\n");
}
