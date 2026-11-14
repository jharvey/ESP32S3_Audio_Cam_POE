// Office console: a PC app on the intercom network.
//   - Room grid: every unit's link, power, battery and talk state; offline units stand out.
//   - All-call and talk to one room (PC microphone or a test voice), hold to talk.
//   - Hears every unit that talks (mixed, on the PC speakers).
//   - Console commands to one unit, a room or all, signed with the network key.
// Works the same with simulated units and real ones (same multicast packets).
#pragma once
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMap>
#include <QStringList>
#include <QTimer>
#include <memory>

#include "JitterBuffer.h"
#include "Mixer.h"
#include "Packet.h"
#include "TestVoice.h"
#include "WinAudio.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QUdpSocket;

struct ConsoleOptions {
    QString key;           // network key (same as "netkey" on the units)
    QStringList rooms;     // units expected on the network, shown even before they're heard
    bool testVoice = false;
    double quitAfter = -1;
    QString shot;          // save a PNG of the window on exit
    QString autoCommand;   // "214 stats": send once, 2 s after start (scripted checks)
    double autoTalkAt = -1, autoTalkFor = 0;
    QString autoTalkTarget;  // "all" or a room
};

class ConsoleWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ConsoleWindow(const ConsoleOptions& opt);
    ~ConsoleWindow() override;

private:
    struct UnitRow {
        ic::StatusPacket status;
        qint64 lastSeen = -1;  // ms since start, -1 = never
        bool expected = false;
    };
    struct Talker {
        ic::JitterBuffer jb;
        qint64 lastPacket = 0;
    };

    void tick();
    void frame();
    void receive();
    void refreshTable();
    void sendCommand();
    void sendText(const QString& target, const QString& cmd);
    void startTalk(bool allCall);
    void stopTalk();
    void appendLog(const QString& line);
    QString selectedUnit() const;

    ConsoleOptions opt_;
    QUdpSocket* sock_ = nullptr;
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 framesRun_ = 0;
    qint64 lastNonce_ = 0;

    QMap<quint32, UnitRow> units_;
    QMap<quint32, std::shared_ptr<Talker>> talkers_;
    ic::Mixer mixer_;
    WinAudioIn in_;
    WinAudioOut out_;
    sim::TestVoice voice_;

    bool talking_ = false;
    uint16_t talkTarget_ = 0;  // 0 = all-call
    uint16_t seq_ = 0;

    QTableWidget* table_;
    QPushButton* allBtn_;
    QPushButton* roomBtn_;
    QComboBox* micSrc_;
    QCheckBox* listen_;
    QLabel* talkState_;
    QLabel* summary_;
    QComboBox* target_;
    QLineEdit* cmd_;
    QLineEdit* key_;
    QPlainTextEdit* log_;
};
