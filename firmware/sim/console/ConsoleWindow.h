// Office console: a PC app on the intercom network.
//   - Room grid: every unit's link, power, battery and talk state; offline units stand out.
//   - All-call and talk to one room (PC microphone or a test voice), hold to talk.
//   - Hears every unit that talks (mixed, on the PC speakers).
//   - Console commands to one unit, a room or all, signed with the network key.
// Works the same with simulated units and real ones (same multicast packets).
#pragma once
#include <QDateTime>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMap>
#include <QStringList>
#include <QVector>
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
        quint32 otherUid = 0;    // a second board heard with the same number
        qint64 otherSeen = -1;   // when that was
        bool heldLogged = false;
        bool callingLogged = false;
    };
    struct Talker {
        ic::JitterBuffer jb;
        qint64 lastPacket = 0;
        bool answerLogged = false;
    };

    void tick();
    void frame();
    void receive();
    void refreshTable();
    void sendCommand();
    void sendText(const QString& target, const QString& cmd);
    enum TalkKind { kTalkAll = 0, kTalkRoom = 1, kTalkFloor = 2 };
    void startTalk(int kind);
    void stopTalk();
    void appendLog(const QString& line);
    QString selectedUnit() const;

    ConsoleOptions opt_;
    QUdpSocket* sock_ = nullptr;
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 framesRun_ = 0;
    qint64 lastNonce_ = 0;
    double nextTimeSync_ = 3.0;  // s since start: next "time" broadcast

    QMap<quint32, UnitRow> units_;
    QMap<quint32, std::shared_ptr<Talker>> talkers_;
    ic::Mixer mixer_;
    WinAudioIn in_;
    WinAudioOut out_;
    sim::TestVoice voice_;

    bool talking_ = false;
    uint16_t talkTarget_ = 0;  // 0 = all-call
    uint8_t talkFlags_ = 0;
    uint16_t seq_ = 0;

    QTableWidget* table_;
    QPushButton* allBtn_;
    QPushButton* roomBtn_;
    QPushButton* floorBtn_;
    QComboBox* micSrc_;
    QCheckBox* listen_;
    QLabel* talkState_;
    QLabel* summary_;
    // Call history (R2.7): rooms calling the office, and the office's calls to rooms.
    struct CallRec {
        QDateTime when;
        QString from, to, result;
        qint64 startMs = 0, endMs = -1;  // clock_ ms; endMs -1 = still going
        bool roomCall = false;           // a room calling the office (can be missed)
        bool done = false;
        uint16_t room = 0, floor = 0, group = 0;  // office call target (one of them)
    };
    QVector<CallRec> calls_;
    QTableWidget* callsTable_ = nullptr;
    QMap<quint32, int> roomCallOpen_;  // unit key -> index of its open call to the office
    int officeCall_ = -1;              // index of the office's call in progress / last one
    int missed_ = 0;
    int addCall(const QString& from, const QString& to, bool roomCall);
    void finishCall(int i, const QString& result);
    void showCall(int i);
    bool officeCallReaches(const CallRec& c, const ic::UnitId& u) const;
    QComboBox* target_;
    QLineEdit* cmd_;
    QLineEdit* key_;
    QPlainTextEdit* log_;
};
