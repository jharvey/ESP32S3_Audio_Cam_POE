#include "SimWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include "SimUnit.h"

// ---------------------------------------------------------------------------------
// The unit's face: case, 3-digit 7-segment display (TM1637), status lights.
class DeviceView : public QWidget {
public:
    explicit DeviceView(SimUnit* u, QWidget* parent = nullptr) : QWidget(parent), unit_(u) { setMinimumSize(260, 250); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF body = rect().adjusted(8, 8, -8, -8);
        p.setBrush(QColor(0xe8, 0xe6, 0xe0));
        p.setPen(QPen(QColor(0x9a, 0x98, 0x90), 2));
        p.drawRoundedRect(body, 18, 18);

        // Speaker grille and camera cover.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x55, 0x55, 0x55));
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 9; c++) p.drawEllipse(QPointF(body.left() + 50 + c * 18, body.bottom() - 70 + r * 14), 3, 3);
        p.setBrush(QColor(0x30, 0x30, 0x30));
        p.drawEllipse(QPointF(body.right() - 34, body.top() + 30), 14, 14);
        p.setPen(QColor(0xdd, 0xdd, 0xdd));
        QFont f = font();
        f.setPointSize(6);
        p.setFont(f);
        char t[3];
        uint8_t dots;
        unit_->display(t, &dots);
        const ic::Intercom* ic = unit_->intercom();
        if (ic) {
            char u[8];
            ic::formatUnit(ic->unit(), u);
            p.drawText(QRectF(body.right() - 48, body.top() + 16, 28, 28), Qt::AlignCenter, u);  // room no. on the cover
        }

        // Display window.
        QRectF win(body.left() + 30, body.top() + 50, 150, 74);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x10, 0x10, 0x18));
        p.drawRoundedRect(win, 6, 6);
        for (int i = 0; i < 3; i++) drawDigit(p, QRectF(win.left() + 12 + i * 46, win.top() + 10, 34, 54), t[i], dots & (1 << i));

        // Status lights.
        QColor off(0x80, 0x80, 0x80);
        QColor net = off;
        QString netTxt = "NET";
        if (unit_->powered() && ic) {
            if (ic->link() == ic::LinkType::Ethernet) { net = QColor(0x2e, 0xc4, 0x5a); netTxt = "ETH"; }
            else if (ic->link() == ic::LinkType::WiFi) { net = QColor(0x3a, 0x8d, 0xf0); netTxt = "WIFI"; }
            else net = QColor(0xe0, 0x40, 0x40);
        }
        struct L { QString name; QColor c; } lights[] = {
            {"PWR", unit_->powered() ? (unit_->poe() ? QColor(0x2e, 0xc4, 0x5a) : QColor(0xf0, 0xa0, 0x20)) : off},
            {netTxt, net},
            {"TX", ic && ic->transmitting() ? QColor(0xf0, 0x30, 0x30) : off},
            {"RX", ic && ic->talkersPlaying() ? QColor(0x2e, 0xc4, 0x5a) : off},
        };
        f.setPointSize(7);
        p.setFont(f);
        for (int i = 0; i < 4; i++) {
            QPointF c(win.right() + 26, win.top() + 8 + i * 19);
            p.setPen(Qt::NoPen);
            p.setBrush(lights[i].c);
            p.drawEllipse(c, 5, 5);
            p.setPen(QColor(0x40, 0x40, 0x40));
            p.drawText(QPointF(c.x() + 9, c.y() + 4), lights[i].name);
        }
    }

private:
    void drawDigit(QPainter& p, QRectF r, char ch, bool dot) {
        // Segment bits: a b c d e f g
        static const struct { char c; uint8_t s; } map[] = {
            {'0', 0x3f}, {'1', 0x06}, {'2', 0x5b}, {'3', 0x4f}, {'4', 0x66}, {'5', 0x6d}, {'6', 0x7d}, {'7', 0x07},
            {'8', 0x7f}, {'9', 0x6f}, {'a', 0x77}, {'b', 0x7c}, {'c', 0x58}, {'d', 0x5e}, {'e', 0x79}, {'f', 0x71},
            {'-', 0x40}, {' ', 0x00}};
        uint8_t segs = 0;
        for (auto& m : map)
            if (m.c == ch) segs = m.s;
        QColor on(0x40, 0x9c, 0xff), off(0x1c, 0x22, 0x30);  // blue display (SLS0563DBA1GD)
        double w = r.width(), h = r.height(), th = 6, hh = h / 2;
        QRectF seg[7] = {
            {r.left() + th, r.top(), w - 2 * th, th},                    // a
            {r.right() - th, r.top() + th, th, hh - 1.5 * th},           // b
            {r.right() - th, r.top() + hh + th / 2, th, hh - 1.5 * th},  // c
            {r.left() + th, r.bottom() - th, w - 2 * th, th},            // d
            {r.left(), r.top() + hh + th / 2, th, hh - 1.5 * th},        // e
            {r.left(), r.top() + th, th, hh - 1.5 * th},                 // f
            {r.left() + th, r.top() + hh - th / 2, w - 2 * th, th},      // g
        };
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 7; i++) {
            p.setBrush(segs & (1 << i) ? on : off);
            p.drawRoundedRect(seg[i], 2, 2);
        }
        p.setBrush(dot ? on : off);
        p.drawEllipse(QPointF(r.right() + 5, r.bottom() - 3), 3, 3);
    }
    SimUnit* unit_;
};

// ---------------------------------------------------------------------------------
SimWindow::SimWindow(SimUnit* unit, QWidget* parent) : QWidget(parent), unit_(unit) {
    setWindowTitle("Intercom sim - " + unit->options().name);
    setFocusPolicy(Qt::StrongFocus);
    auto* root = new QHBoxLayout(this);

    // Left: device + buttons.
    auto* left = new QVBoxLayout;
    device_ = new DeviceView(unit);
    left->addWidget(device_);
    auto* btns = new QHBoxLayout;
    const char* names[] = {"TALK\n(Space)", "MODE\n(M)", "UP\n(↑)", "DOWN\n(↓)"};
    for (int b = 0; b < ic::kButtonCount; b++) {
        auto* pb = new QPushButton(names[b]);
        pb->setFocusPolicy(Qt::NoFocus);
        pb->setMinimumHeight(b == 0 ? 56 : 44);
        if (b == 0) pb->setStyleSheet("QPushButton{background:#c0392b;color:white;font-weight:bold}QPushButton:pressed{background:#e74c3c}");
        connect(pb, &QPushButton::pressed, this, [this, b] { unit_->setButton(b, true); });
        connect(pb, &QPushButton::released, this, [this, b] { unit_->setButton(b, false); });
        btns->addWidget(pb, b == 0 ? 2 : 1);
    }
    left->addLayout(btns);
    status_ = new QLabel;
    status_->setWordWrap(true);
    left->addWidget(status_);
    left->addWidget(new QLabel("<small>Room setup: hold MODE 3 s, UP/DOWN change a digit, MODE next, MODE on the letter saves.</small>"));
    left->addStretch();
    root->addLayout(left, 0);

    // Right: simulated hardware.
    auto* right = new QVBoxLayout;
    auto* pw = new QGroupBox("Power and network");
    auto* pwl = new QFormLayout(pw);
    poe_ = new QCheckBox("PoE cable plugged in");
    wifi_ = new QCheckBox("Wi-Fi in range");
    batt_ = new QCheckBox("Battery fitted (portable unit)");
    battLevel_ = new QSlider(Qt::Horizontal);
    battLevel_->setRange(0, 100);
    poe_->setChecked(unit->poe());
    wifi_->setChecked(unit->wifiInRange());
    batt_->setChecked(unit->hasBattery());
    pwl->addRow(poe_);
    pwl->addRow(wifi_);
    pwl->addRow(batt_);
    pwl->addRow("Battery", battLevel_);
    connect(poe_, &QCheckBox::toggled, this, [this](bool on) { unit_->setPoe(on); });
    connect(wifi_, &QCheckBox::toggled, this, [this](bool on) { unit_->setWifiInRange(on); });
    connect(batt_, &QCheckBox::toggled, this, [this](bool on) { unit_->setHasBattery(on); });
    connect(battLevel_, &QSlider::sliderMoved, this, [this](int v) { unit_->setBattery(v); });
    right->addWidget(pw);

    auto* au = new QGroupBox("Audio");
    auto* aul = new QFormLayout(au);
    mic_ = new QComboBox;
    mic_->addItems({"Test talker (speaks while TALK held)", "PC microphone", "Silent"});
    mic_->setCurrentIndex(int(unit->options().mic));
    echo_ = new QComboBox;
    for (int i = 0; i < 3; i++) echo_->addItem(sim::presetName(sim::RoomPreset(i)));
    echo_->setCurrentIndex(int(unit->options().echo));
    aec_ = new QCheckBox("Echo canceller");
    aec_->setChecked(true);
    duck_ = new QCheckBox("Duck speaker while talking (gym mode)");
    listen_ = new QCheckBox("Play this unit's speaker on the PC");
    listen_->setChecked(unit->options().listen);
    aul->addRow("Mic", mic_);
    aul->addRow("Room echo", echo_);
    aul->addRow(aec_);
    aul->addRow(duck_);
    aul->addRow(listen_);
    connect(mic_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) { unit_->setMicMode(MicMode(i)); });
    connect(echo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) { unit_->setEcho(sim::RoomPreset(i)); });
    connect(aec_, &QCheckBox::toggled, this, [this](bool on) { unit_->setAecEnabled(on); });
    connect(duck_, &QCheckBox::toggled, this, [this](bool on) { unit_->setDuck(on); });
    connect(listen_, &QCheckBox::toggled, this, [this](bool on) { unit_->setListen(on); });

    auto meter = [](const QString& fmt) {
        auto* m = new QProgressBar;
        m->setRange(0, 100);
        m->setFormat(fmt);
        m->setMaximumHeight(16);
        return m;
    };
    mMic_ = meter("mic %v");
    mSent_ = meter("sent %v");
    mSpk_ = meter("speaker %v");
    aecInfo_ = new QLabel;
    aul->addRow("Mic in", mMic_);
    aul->addRow("Sent (after AEC)", mSent_);
    aul->addRow("Speaker", mSpk_);
    aul->addRow("Echo canceller", aecInfo_);
    right->addWidget(au);

    auto* nw = new QGroupBox("Units on the network");
    auto* nwl = new QVBoxLayout(nw);
    peers_ = new QListWidget;
    peers_->setMaximumHeight(110);
    nwl->addWidget(peers_);
    right->addWidget(nw);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(500);
    QFont mono("Consolas");
    mono.setPointSize(8);
    log_->setFont(mono);
    right->addWidget(log_, 1);
    root->addLayout(right, 1);

    connect(unit, &SimUnit::logLine, log_, &QPlainTextEdit::appendPlainText);
    auto* t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &SimWindow::refresh);
    t->start(100);
    resize(760, 640);
}

void SimWindow::refresh() {
    device_->update();
    battLevel_->setEnabled(unit_->hasBattery());
    if (!battLevel_->isSliderDown()) battLevel_->setValue(int(unit_->battery()));
    const ic::Intercom* ic = unit_->intercom();
    QString s;
    if (!unit_->powered()) {
        s = "<b>Off</b> (no PoE, no battery)";
    } else if (!ic) {
        s = "<b>Booting...</b>";
    } else {
        char u[8], ip[16];
        ic::formatUnit(ic->unit(), u);
        ic::formatIp(ic->unit(), ip);
        static const char* link[] = {"<span style='color:#c0392b'>no network</span>", "Ethernet", "Wi-Fi"};
        s = QString("<b>Room %1</b> &nbsp; %2<br>Link: %3 &nbsp; Power: %4%5")
                .arg(u, ip, link[int(ic->link())], unit_->poe() ? "PoE" : "battery",
                     unit_->hasBattery() ? QString(" (%1 %)").arg(int(unit_->battery())) : QString());
        if (ic->inSetup()) s += "<br><b>Setting room number</b>";
    }
    status_->setText(s);
    mMic_->setValue(ic ? ic->micLevel() : 0);
    mSent_->setValue(ic ? ic->sentLevel() : 0);
    mSpk_->setValue(ic ? ic->speakerLevel() : 0);
    if (const ic::AecStats* a = unit_->aecStats()) {
        aecInfo_->setText(QString("ERLE %1 dB  %2%3%4")
                              .arg(a->erleDb, 0, 'f', 1)
                              .arg(a->farActive ? "far-end " : "")
                              .arg(a->doubleTalk ? "double-talk " : "")
                              .arg(a->converged ? "converged" : "adapting"));
    } else {
        aecInfo_->setText("-");
    }
    peers_->clear();
    if (ic) {
        for (const ic::Peer& p : ic->peers()) {
            char u[8];
            ic::formatUnit(p.status.from, u);
            static const char* link[] = {"-", "Ethernet", "Wi-Fi"};
            QString line = QString("%1   %2   %3").arg(u, -6).arg(link[int(p.status.link)], -8)
                               .arg(p.status.power == ic::PowerSource::PoE ? "PoE" : QString("battery %1 %").arg(p.status.battery));
            if (p.status.flags & ic::kStatusTalking) line += "   TALKING";
            peers_->addItem(line);
        }
    }
}

bool SimWindow::keyToButton(int key, int* b) const {
    switch (key) {
        case Qt::Key_Space: *b = ic::kBtnTalk; return true;
        case Qt::Key_M: *b = ic::kBtnMode; return true;
        case Qt::Key_Up: *b = ic::kBtnUp; return true;
        case Qt::Key_Down: *b = ic::kBtnDown; return true;
        default: return false;
    }
}

void SimWindow::keyPressEvent(QKeyEvent* e) {
    int b;
    if (!e->isAutoRepeat() && keyToButton(e->key(), &b)) unit_->setButton(b, true);
    else QWidget::keyPressEvent(e);
}

void SimWindow::keyReleaseEvent(QKeyEvent* e) {
    int b;
    if (!e->isAutoRepeat() && keyToButton(e->key(), &b)) unit_->setButton(b, false);
    else QWidget::keyReleaseEvent(e);
}
