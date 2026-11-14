// intercom-sim: one simulated classroom intercom unit per process.
//   intercom-sim --name 214                 room 214, Ethernet, test talker, classroom echo
//   intercom-sim --name 150a --echo gym     first of several units in the gym
//   intercom-sim --name 999 --battery --no-poe    portable unit on Wi-Fi
//   intercom-sim --name 101 --headless --auto-talk 3,2 --quit-after 8   scripted, prints SUMMARY
//   intercom-sim --send "214 cal"           network console: auto-tune unit 214
//   intercom-sim --send "all get"           show every unit's tuning
//   intercom-sim --name 214 --web-port 8214 web interface on http://localhost:8214
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QUdpSocket>
#include <cstdio>
#include <memory>

#include "Packet.h"

#include "Auth.h"
#include "SimUnit.h"
#include "SimWebServer.h"
#include "SimWindow.h"

// --send: a tiny network console client (what an office tool would do).
static int sendCommand(const QString& arg, const QString& key) {
    QString target = arg.section(' ', 0, 0), cmd = arg.section(' ', 1);
    ic::TextPacket t;
    t.type = ic::PacketType::Command;
    t.from = {0, 0};  // room 0 = console / tool
    if (!ic::parseTarget(target.toStdString(), &t.targetRoom, &t.targetSub) || cmd.isEmpty()) {
        fprintf(stderr, "usage: --send \"<all|214|214a> <command>\" --key <network key>\n");
        return 2;
    }
    t.text = cmd.toStdString();
    if (!key.isEmpty()) t.text = ic::netauth::sign(key.toStdString(), uint64_t(QDateTime::currentMSecsSinceEpoch()), t);
    else fprintf(stderr, "no --key: units will refuse the command\n");
    QUdpSocket sock;
    QHostAddress group(QString::fromLatin1(ic::kMulticastGroup));
    sock.bind(QHostAddress::AnyIPv4, ic::kAudioPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    sock.setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    sock.joinMulticastGroup(group);
    uint8_t buf[ic::kMaxPacketBytes + 16];
    sock.writeDatagram(reinterpret_cast<const char*>(buf), qint64(ic::encodeText(t, buf)), group, ic::kAudioPort);
    QElapsedTimer clock;
    clock.start();
    int replies = 0;
    while (clock.elapsed() < 1500) {
        if (!sock.waitForReadyRead(100)) continue;
        while (sock.hasPendingDatagrams()) {
            qint64 n = sock.readDatagram(reinterpret_cast<char*>(buf), sizeof(buf));
            ic::TextPacket r;
            if (n > 0 && ic::decodeText(buf, size_t(n), &r) && r.type == ic::PacketType::Reply && r.targetRoom == 0) {
                char u[8];
                ic::formatUnit(r.from, u);
                printf("%s: %s\n", u, r.text.c_str());
                replies++;
            }
        }
    }
    fflush(stdout);
    if (!replies) fprintf(stderr, "no reply\n");
    return replies ? 0 : 1;
}

int main(int argc, char** argv) {
    bool headless = false;
    for (int i = 1; i < argc; i++)
        if (QString(argv[i]) == "--headless" || QString(argv[i]).startsWith("--send")) headless = true;
    std::unique_ptr<QCoreApplication> app(headless ? new QCoreApplication(argc, argv) : new QApplication(argc, argv));
    QCoreApplication::setApplicationName("intercom-sim");

    QCommandLineParser p;
    p.addHelpOption();
    p.addOption({"name", "Unit name, also the starting room number (214, 150a).", "name", "214"});
    p.addOption({"mic", "test | pc | silent", "mode", "test"});
    p.addOption({"echo", "none | classroom | gym", "room", "classroom"});
    p.addOption({"listen", "Play this unit's speaker on the PC speakers."});
    p.addOption({"battery", "Battery fitted (portable unit)."});
    p.addOption({"no-poe", "Start unplugged."});
    p.addOption({"no-wifi", "Start out of Wi-Fi range."});
    p.addOption({"web-port", "Serve the unit's web interface on http://localhost:PORT.", "port", "0"});
    p.addOption({"net-key", "Set the network command key at boot (as if typed on the USB console).", "key"});
    p.addOption({"key", "Network key for --send.", "key"});
    p.addOption({"web-pass", "Set the web password at boot (as if typed on the USB console).", "password"});
    p.addOption({"hw-delay", "Simulated speaker->mic hardware latency, ms (auto-tune should find it).", "ms", "12"});
    p.addOption({"send", "Send a console command to units on the network and print replies: \"214a get\", \"all stats\".", "target cmd"});
    p.addOption({"auto-talk", "Hold TALK from START for DUR seconds.", "start,dur"});
    p.addOption({"quit-after", "Exit after N seconds and print a SUMMARY line.", "s"});
    p.addOption({"fresh", "Forget the saved room number for this name."});
    p.addOption({"headless", "No window."});
    p.addOption({"shot", "Save a PNG of the window on exit (with --quit-after).", "file"});
    p.process(*app);

    SimOptions o;
    o.name = p.value("name");
    o.dataDir = QCoreApplication::applicationDirPath() + "/simdata/" + o.name;
    if (p.isSet("fresh")) QDir(o.dataDir).removeRecursively();
    QString mic = p.value("mic");
    o.mic = mic == "pc" ? MicMode::Pc : mic == "silent" ? MicMode::Silent : MicMode::Test;
    QString echo = p.value("echo");
    o.echo = echo == "none" ? sim::RoomPreset::None : echo == "gym" ? sim::RoomPreset::Gym : sim::RoomPreset::Classroom;
    o.listen = p.isSet("listen");
    o.battery = p.isSet("battery");
    o.poe = !p.isSet("no-poe");
    o.wifi = !p.isSet("no-wifi");
    o.hwDelayMs = p.value("hw-delay").toInt();
    o.webPort = p.value("web-port").toInt();
    o.webPass = p.value("web-pass");
    o.netKey = p.value("net-key");
    if (p.isSet("send")) return sendCommand(p.value("send"), p.value("key"));
    if (p.isSet("auto-talk")) {
        QStringList v = p.value("auto-talk").split(',');
        o.autoTalkAt = v.value(0).toDouble();
        o.autoTalkFor = v.value(1, "2").toDouble();
    }
    if (p.isSet("quit-after")) o.quitAfter = p.value("quit-after").toDouble();

    SimUnit unit(o);
    std::unique_ptr<SimWindow> win;
    if (!headless) {
        win.reset(new SimWindow(&unit));
        win->show();
        if (p.isSet("shot")) {  // save a picture of the window when the run ends (docs, checks)
            QString path = p.value("shot");
            SimWindow* w = win.get();
            QObject::connect(app.get(), &QCoreApplication::aboutToQuit, [w, path] { w->grab().save(path); });
        }
    }
    unit.start();
    SimWebServer web(&unit);
    if (o.webPort > 0) {
        if (web.listen(o.webPort))
            unit.log(qPrintable(QString("sim: web interface on http://localhost:%1 (set a password first: webpass <password>)").arg(o.webPort)));
        else
            unit.log(qPrintable("sim: web port " + QString::number(o.webPort) + " failed: " + web.error()));
    }
    return app->exec();
}
