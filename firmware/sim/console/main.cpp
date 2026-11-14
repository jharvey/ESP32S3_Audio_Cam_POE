// Intercom office console.
//   intercom-console                                  room grid, talk, commands
//   intercom-console --key <network key>              key for signed commands
//   intercom-console --rooms 101,102,214,150a,150b    units expected (shown as "never heard" until seen)
//   intercom-console --rooms-file rooms.txt           same, one unit per line
//   intercom-console --test-voice                     talk with a synthetic voice instead of the PC mic
//   intercom-console --auto-command "214 stats" --quit-after 5 --shot console.png   scripted check
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTextStream>

#include "ConsoleWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("intercom-console");
    QCommandLineParser p;
    p.addHelpOption();
    p.addOption({"key", "Network key (same as \"netkey\" on the units).", "key"});
    p.addOption({"rooms", "Units expected on the network, comma separated.", "list"});
    p.addOption({"rooms-file", "Units expected on the network, one per line.", "file"});
    p.addOption({"test-voice", "Talk with a synthetic voice instead of the PC microphone."});
    p.addOption({"auto-command", "Send one command 2 s after start: \"214 stats\".", "target cmd"});
    p.addOption({"auto-talk", "Hold a talk button: START,DUR,TARGET (TARGET = all or a room).", "start,dur,target"});
    p.addOption({"quit-after", "Exit after N seconds.", "s"});
    p.addOption({"shot", "Save a PNG of the window on exit (with --quit-after).", "file"});
    p.process(app);

    ConsoleOptions o;
    o.key = p.value("key");
    if (p.isSet("rooms")) o.rooms = p.value("rooms").split(',', Qt::SkipEmptyParts);
    if (p.isSet("rooms-file")) {
        QFile f(p.value("rooms-file"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                QString l = in.readLine().section('#', 0, 0).trimmed();
                if (!l.isEmpty()) o.rooms << l;
            }
        }
    }
    o.testVoice = p.isSet("test-voice");
    o.autoCommand = p.value("auto-command");
    if (p.isSet("auto-talk")) {
        QStringList v = p.value("auto-talk").split(',');
        o.autoTalkAt = v.value(0).toDouble();
        o.autoTalkFor = v.value(1, "2").toDouble();
        o.autoTalkTarget = v.value(2, "all");
    }
    if (p.isSet("quit-after")) o.quitAfter = p.value("quit-after").toDouble();
    o.shot = p.value("shot");

    ConsoleWindow w(o);
    w.show();
    return app.exec();
}
