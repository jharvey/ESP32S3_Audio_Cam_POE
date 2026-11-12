// intercom-sim: one simulated classroom intercom unit per process.
//   intercom-sim --name 214                 room 214, Ethernet, test talker, classroom echo
//   intercom-sim --name 150a --echo gym     first of several units in the gym
//   intercom-sim --name 999 --battery --no-poe    portable unit on Wi-Fi
//   intercom-sim --name 101 --headless --auto-talk 3,2 --quit-after 8   scripted, prints SUMMARY
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <memory>

#include "SimUnit.h"
#include "SimWindow.h"

int main(int argc, char** argv) {
    bool headless = false;
    for (int i = 1; i < argc; i++)
        if (QString(argv[i]) == "--headless") headless = true;
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
    p.addOption({"aec-delay", "Extra speaker-to-mic delay for the echo canceller, ms (PC mic mode).", "ms", "0"});
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
    o.aecDelayMs = p.value("aec-delay").toInt();
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
    return app->exec();
}
