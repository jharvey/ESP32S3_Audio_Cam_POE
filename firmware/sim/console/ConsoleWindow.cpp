#include "ConsoleWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QUdpSocket>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>

#include "Auth.h"

namespace {
const QHostAddress kGroup(QString::fromLatin1(ic::kMulticastGroup));
constexpr qint64 kOfflineMs = ic::kPeerTimeoutFrames * ic::kFrameMs;  // 3.5 s without status
constexpr qint64 kTalkerIdleMs = 1000;

QString unitName(const ic::UnitId& id) {
    char u[8];
    ic::formatUnit(id, u);
    return QString::fromLatin1(u);
}
QString unitIp(const ic::UnitId& id) {
    char ip[16];
    ic::formatIp(id, ip);
    return QString::fromLatin1(ip);
}
QString duration(qint64 s) {
    if (s >= 3600) return QString("%1 h %2 min").arg(s / 3600).arg(s % 3600 / 60);
    if (s >= 60) return QString("%1 min").arg(s / 60);
    return QString("%1 s").arg(s);
}
}  // namespace

ConsoleWindow::ConsoleWindow(const ConsoleOptions& opt) : opt_(opt), voice_(120.0f, 7, -20.0f) {
    setWindowTitle("Intercom office console");

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);

    summary_ = new QLabel;
    summary_->setStyleSheet("font-size:15px;font-weight:600");
    root->addWidget(summary_);

    table_ = new QTableWidget(0, 7);
    table_->setHorizontalHeaderLabels({"Unit", "IP", "State", "Link", "Power", "Up", "Last heard"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSortingEnabled(false);
    root->addWidget(table_, 3);

    // Talk.
    auto* talkBox = new QGroupBox("Talk (hold the button)");
    auto* tl = new QHBoxLayout(talkBox);
    allBtn_ = new QPushButton("All-call");
    roomBtn_ = new QPushButton("Talk to selected room");
    for (QPushButton* b : {allBtn_, roomBtn_}) b->setMinimumHeight(44);
    allBtn_->setStyleSheet("font-weight:600");
    micSrc_ = new QComboBox;
    micSrc_->addItems({"PC microphone", "Test voice"});
    micSrc_->setCurrentIndex(opt_.testVoice ? 1 : 0);
    listen_ = new QCheckBox("Play units on the PC speakers");
    listen_->setChecked(true);
    talkState_ = new QLabel;
    tl->addWidget(allBtn_);
    tl->addWidget(roomBtn_);
    tl->addWidget(new QLabel("Mic:"));
    tl->addWidget(micSrc_);
    tl->addWidget(listen_);
    tl->addWidget(talkState_, 1);
    root->addWidget(talkBox);
    connect(allBtn_, &QPushButton::pressed, this, [this] { startTalk(true); });
    connect(roomBtn_, &QPushButton::pressed, this, [this] { startTalk(false); });
    connect(allBtn_, &QPushButton::released, this, &ConsoleWindow::stopTalk);
    connect(roomBtn_, &QPushButton::released, this, &ConsoleWindow::stopTalk);
    connect(listen_, &QCheckBox::toggled, this, [this](bool on) {
        if (on && !out_.isOpen() && !out_.open()) appendLog("no PC speaker available");
        if (!on) out_.close();
    });

    // Commands.
    auto* cmdBox = new QGroupBox("Commands (same as the USB console; \"help\" lists them)");
    auto* cl = new QVBoxLayout(cmdBox);
    auto* row = new QHBoxLayout;
    target_ = new QComboBox;
    target_->addItems({"selected unit", "selected room", "all units"});
    cmd_ = new QLineEdit;
    cmd_->setPlaceholderText("stats");
    auto* send = new QPushButton("Send");
    auto* web = new QPushButton("Open web page");
    key_ = new QLineEdit(opt_.key);
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText("network key");
    key_->setMaximumWidth(180);
    row->addWidget(target_);
    row->addWidget(cmd_, 1);
    row->addWidget(send);
    row->addWidget(new QLabel("Key:"));
    row->addWidget(key_);
    row->addWidget(web);
    cl->addLayout(row);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    log_->setFont(QFont("Consolas", 9));
    cl->addWidget(log_);
    root->addWidget(cmdBox, 2);
    connect(send, &QPushButton::clicked, this, &ConsoleWindow::sendCommand);
    connect(cmd_, &QLineEdit::returnPressed, this, &ConsoleWindow::sendCommand);
    connect(web, &QPushButton::clicked, this, [this] {
        QString u = selectedUnit();
        if (u.isEmpty()) return appendLog("select a unit first");
        ic::UnitId id;
        ic::parseTarget(u.toStdString(), &id.room, &id.sub);
        if (id.sub == ic::kTargetAnySub) id.sub = 0;
        QDesktopServices::openUrl(QUrl("http://" + unitIp(id) + "/"));
    });

    setCentralWidget(central);
    resize(980, 720);

    for (const QString& r : opt_.rooms) {
        ic::UnitId id;
        if (!ic::parseTarget(r.trimmed().toStdString(), &id.room, &id.sub) || id.room == ic::kTargetAllRooms) continue;
        if (id.sub == ic::kTargetAnySub) id.sub = 0;
        UnitRow u;
        u.status.from = id;
        u.expected = true;
        units_[id.key()] = u;
    }

    sock_ = new QUdpSocket(this);
    if (!sock_->bind(QHostAddress::AnyIPv4, ic::kAudioPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
        appendLog("UDP bind failed: " + sock_->errorString());
    sock_->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
    sock_->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    if (!sock_->joinMulticastGroup(kGroup)) appendLog("multicast join failed: " + sock_->errorString());
    if (listen_->isChecked() && !out_.open()) appendLog("no PC speaker available");
    if (opt_.key.isEmpty()) appendLog("No network key yet: enter the key the units were set up with (\"netkey\" on USB) to send commands.");

    clock_.start();
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &ConsoleWindow::tick);
    timer_.start(5);
    refreshTable();
}

ConsoleWindow::~ConsoleWindow() = default;

void ConsoleWindow::appendLog(const QString& line) {
    log_->appendPlainText(QTime::currentTime().toString("HH:mm:ss  ") + line);
}

QString ConsoleWindow::selectedUnit() const {
    auto sel = table_->selectedItems();
    if (sel.isEmpty()) return QString();
    return table_->item(sel.first()->row(), 0)->text();
}

void ConsoleWindow::startTalk(bool allCall) {
    if (!allCall) {
        QString u = selectedUnit();
        if (u.isEmpty()) return appendLog("select a room in the table first");
        ic::UnitId id;
        ic::parseTarget(u.toStdString(), &id.room, &id.sub);
        talkTarget_ = id.room;
    } else {
        talkTarget_ = 0;
    }
    if (micSrc_->currentIndex() == 0 && !in_.isOpen() && !in_.open()) {
        appendLog("no PC microphone: using the test voice");
        micSrc_->setCurrentIndex(1);
    }
    talking_ = true;
    talkState_->setText(talkTarget_ ? QString("TALKING to room %1").arg(talkTarget_) : QString("TALKING to everyone"));
    talkState_->setStyleSheet("color:#c62828;font-weight:600");
}

void ConsoleWindow::stopTalk() {
    talking_ = false;
    talkState_->clear();
}

void ConsoleWindow::tick() {
    receive();
    qint64 now = clock_.elapsed();
    qint64 due = now / ic::kFrameMs;
    if (due - framesRun_ > 10) framesRun_ = due - 1;  // window was blocked: don't burst
    while (framesRun_ < due) {
        framesRun_++;
        frame();
    }
    if (framesRun_ % 25 == 0) refreshTable();

    double t = now / 1000.0;
    if (opt_.autoTalkAt >= 0) {
        if (!talking_ && t >= opt_.autoTalkAt && t < opt_.autoTalkAt + opt_.autoTalkFor) {
            if (opt_.autoTalkTarget != "all") {
                for (int r = 0; r < table_->rowCount(); r++)
                    if (table_->item(r, 0)->text() == opt_.autoTalkTarget) table_->selectRow(r);
            }
            startTalk(opt_.autoTalkTarget == "all");
        } else if (talking_ && t >= opt_.autoTalkAt + opt_.autoTalkFor) {
            stopTalk();
            opt_.autoTalkAt = -1;
        }
    }
    if (!opt_.autoCommand.isEmpty() && t >= 2.0) {
        sendText(opt_.autoCommand.section(' ', 0, 0), opt_.autoCommand.section(' ', 1));
        opt_.autoCommand.clear();
    }
    if (opt_.quitAfter >= 0 && t >= opt_.quitAfter) {
        if (!opt_.shot.isEmpty()) grab().save(opt_.shot);
        opt_.quitAfter = -1;
        QApplication::quit();
    }
}

void ConsoleWindow::frame() {
    // Send: one frame of the office mic while a talk button is held.
    if (talking_) {
        int16_t pcm[ic::kFrameSamples];
        bool got = micSrc_->currentIndex() == 0 && in_.isOpen() && in_.read(pcm);
        if (!got) {
            if (micSrc_->currentIndex() == 1) voice_.render(pcm, ic::kFrameSamples);
            else std::memset(pcm, 0, sizeof(pcm));
        }
        ic::AudioHeader h;
        h.from = {0, 0};  // room 0 = office console
        h.flags = ic::kAudioAllCall | ic::kAudioFromConsole;
        h.target = talkTarget_;
        h.seq = seq_++;
        h.level = ic::frameLevel(pcm, ic::kFrameSamples);
        uint8_t buf[ic::kMaxPacketBytes];
        size_t n = ic::encodeAudio(h, pcm, buf);
        sock_->writeDatagram(reinterpret_cast<const char*>(buf), qint64(n), kGroup, ic::kAudioPort);
    } else if (in_.isOpen()) {
        int16_t drop[ic::kFrameSamples];
        while (in_.read(drop)) {
        }
    }

    // Play: mix every unit that's talking.
    int16_t frames[16][ic::kFrameSamples];
    ic::MixInput mix[16];
    int n = 0;
    qint64 now = clock_.elapsed();
    for (auto it = talkers_.begin(); it != talkers_.end();) {
        Talker& t = **it;
        uint8_t lvl;
        if (n < 16 && t.jb.pop(frames[n], &lvl)) {
            mix[n].pcm = frames[n];
            mix[n].level = lvl;
            n++;
        }
        if (!t.jb.playing() && now - t.lastPacket > kTalkerIdleMs) it = talkers_.erase(it);
        else ++it;
    }
    if (n && out_.isOpen()) {
        int16_t spk[ic::kFrameSamples];
        mixer_.mix(mix, n, 1.0f, spk);
        out_.write(spk);
    }
}

void ConsoleWindow::receive() {
    uint8_t buf[ic::kMaxPacketBytes + 16];
    int16_t pcm[ic::kFrameSamples];
    while (sock_->hasPendingDatagrams()) {
        qint64 len = sock_->readDatagram(reinterpret_cast<char*>(buf), sizeof(buf));
        if (len <= 0) continue;
        bool ok;
        ic::PacketType type = ic::peekType(buf, size_t(len), &ok);
        if (!ok) continue;
        if (type == ic::PacketType::Status) {
            ic::StatusPacket s;
            if (!ic::decodeStatus(buf, size_t(len), &s)) continue;
            UnitRow& u = units_[s.from.key()];
            bool wasOffline = u.lastSeen >= 0 && clock_.elapsed() - u.lastSeen > kOfflineMs;
            if (u.lastSeen < 0 || wasOffline) appendLog(unitName(s.from) + (u.lastSeen < 0 ? " online" : " back online"));
            u.status = s;
            u.lastSeen = clock_.elapsed();
        } else if (type == ic::PacketType::Audio) {
            ic::AudioHeader h;
            if (!ic::decodeAudio(buf, size_t(len), &h, pcm)) continue;
            if (h.from.room == 0) continue;  // our own (or another console's) audio
            auto& t = talkers_[h.from.key()];
            if (!t) t = std::make_shared<Talker>();
            t->jb.push(h.seq, pcm, h.level);
            t->lastPacket = clock_.elapsed();
        } else if (type == ic::PacketType::Reply) {
            ic::TextPacket r;
            if (!ic::decodeText(buf, size_t(len), &r) || r.targetRoom != 0) continue;
            QString text = QString::fromStdString(r.text);
            QStringList lines = text.split('\n', Qt::SkipEmptyParts);
            for (const QString& l : lines) appendLog(unitName(r.from) + ": " + l);
        }
    }
}

void ConsoleWindow::refreshTable() {
    qint64 now = clock_.elapsed();
    QString keep = selectedUnit();
    QList<UnitRow> rows = units_.values();
    std::sort(rows.begin(), rows.end(), [](const UnitRow& a, const UnitRow& b) { return a.status.from.key() < b.status.from.key(); });
    table_->setRowCount(int(rows.size()));
    int online = 0, offline = 0, battery = 0, talking = 0;
    for (int r = 0; r < rows.size(); r++) {
        const UnitRow& u = rows[r];
        bool on = u.lastSeen >= 0 && now - u.lastSeen <= kOfflineMs;
        const ic::StatusPacket& s = u.status;
        QString state, link, power, up, seen;
        QColor bg;
        if (!on) {
            state = u.lastSeen < 0 ? "never heard" : "OFFLINE";
            bg = QColor(255, 205, 205);
            offline++;
        } else {
            online++;
            bool talk = s.flags & ic::kStatusTalking;
            state = talk ? "talking" : "ok";
            if (talk) {
                talking++;
                bg = QColor(255, 236, 179);
            }
            link = s.link == ic::LinkType::Ethernet ? "Ethernet" : s.link == ic::LinkType::WiFi ? "Wi-Fi" : "none";
            power = s.power == ic::PowerSource::Battery ? QString("battery %1%").arg(s.battery) : QString("PoE");
            if (s.power == ic::PowerSource::Battery) {
                battery++;
                if (!talk) bg = s.battery < 25 ? QColor(255, 205, 205) : QColor(255, 244, 214);
            }
            up = duration(s.uptimeS);
        }
        seen = u.lastSeen < 0 ? "-" : duration((now - u.lastSeen) / 1000) + " ago";
        QStringList cells = {unitName(s.from), unitIp(s.from), state, link, power, up, seen};
        for (int c = 0; c < cells.size(); c++) {
            QTableWidgetItem* it = table_->item(r, c);
            if (!it) table_->setItem(r, c, it = new QTableWidgetItem);
            it->setText(cells[c]);
            it->setBackground(bg.isValid() ? QBrush(bg) : QBrush());
            it->setForeground(bg.isValid() ? QBrush(Qt::black) : QBrush());
        }
        if (cells[0] == keep) table_->selectRow(r);
    }
    summary_->setText(QString("%1 online   %2 offline   %3 on battery   %4 talking").arg(online).arg(offline).arg(battery).arg(talking));
}

void ConsoleWindow::sendCommand() {
    QString c = cmd_->text().trimmed();
    if (c.isEmpty()) return;
    QString target;
    if (target_->currentIndex() == 2) {
        target = "all";
    } else {
        QString u = selectedUnit();
        if (u.isEmpty()) return appendLog("select a unit in the table first (or choose \"all units\")");
        target = target_->currentIndex() == 1 ? u.left(u.size() - (u.back().isLetter() ? 1 : 0)) : u;
    }
    sendText(target, c);
    cmd_->clear();
}

void ConsoleWindow::sendText(const QString& target, const QString& cmd) {
    ic::TextPacket t;
    t.type = ic::PacketType::Command;
    t.from = {0, 0};
    if (!ic::parseTarget(target.toStdString(), &t.targetRoom, &t.targetSub)) return appendLog("bad target " + target);
    t.text = cmd.toStdString();
    std::string key = key_->text().toStdString();
    if (key.empty()) return appendLog("enter the network key first");
    lastNonce_ = std::max(lastNonce_ + 1, QDateTime::currentMSecsSinceEpoch());
    t.text = ic::netauth::sign(key, uint64_t(lastNonce_), t);
    uint8_t buf[ic::kMaxPacketBytes + 16];
    size_t n = ic::encodeText(t, buf);
    sock_->writeDatagram(reinterpret_cast<const char*>(buf), qint64(n), kGroup, ic::kAudioPort);
    appendLog("> " + target + " " + (cmd.startsWith("webpass") || cmd.startsWith("netkey") ? cmd.section(' ', 0, 0) + " ****" : cmd));
}
