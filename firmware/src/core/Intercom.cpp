#include "Intercom.h"

#include "Auth.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace ic {

namespace {
constexpr int kEthStableFrames = 10;     // Ethernet must be up 200 ms before traffic moves to it
constexpr int kTalkerIdleFrames = 50;    // forget a talker 1 s after its last packet
constexpr size_t kMaxTalkers = 16;
constexpr uint32_t kComboFrames = 150;   // MODE + UP held 3 s = auto-tune
constexpr uint32_t kUpHoldFrames = 250;  // or UP alone held 5 s (one-key panels)
constexpr uint32_t kResultFrames = 250;  // show the auto-tune result for 5 s
// Room calls (R2.0).
constexpr uint32_t kCallFrames = 50;        // a call is "on" while its packets keep coming (1 s gap ends it)
constexpr uint32_t kAnswerFrames = 500;     // TALK answers the caller during a call and 10 s after
constexpr uint32_t kAllCallHoldFrames = 25; // an office all-call keeps room calls out until 0.5 s after it
constexpr uint32_t kCallHoldFrames = 100;   // DOWN held 2 s = call the office (again = cancel)
constexpr uint32_t kCallingTimeoutFrames = 15000;  // a call to the office gives up after 5 min
constexpr uint32_t kPendingSoundFrames = 3000;     // a timed chime waits up to 60 s for a call to end
constexpr const char* kSchedKey = "sched";
constexpr uint32_t kDupHoldFrames = 150; // "dUP" stays 3 s after the other unit was last heard
constexpr uint32_t kFaultCycle = 175;    // faults: room number 3 s, then the code for 0.5 s
constexpr uint32_t kFaultShow = 25;
constexpr uint8_t kLowBatteryPct = 15;
const char* kGroupKey = "group";
const char* kWifiSsidKey = "wifi_ssid";
const char* kWifiPassKey = "wifi_pass";
const char* kTuneKey = "tune";
const char* kTunePrevKey = "tune_prev";

const char* kHelp =
    "commands:\n"
    "  help                 this list\n"
    "  get [key]            show tuning parameters\n"
    "  set key value ...    change parameters now (not saved)\n"
    "  save                 save parameters to flash\n"
    "  load                 reload saved parameters\n"
    "  defaults             factory parameters (not saved)\n"
    "  undo                 restore the parameters from before the last auto-tune or save\n"
    "  cal                  auto-tune the echo canceller (~5 s, room quiet, unit plays a noise burst)\n"
    "  cal result           last auto-tune measurements\n"
    "  stats                echo canceller and link state\n"
    "  unit [214]           show or set the room number (sets the IP address 10.42.<floor>.<room>)\n"
    "  group [299|off]      room group: units in one room (the gym: 299, 298 ...) do not play each other\n"
    "  tone [seconds]       play a 1 kHz test tone on this unit's speaker (default 2 s, 0 stops)\n"
    "  mute [minutes|off]   stop this unit's TALK for a while (office: stuck button, misuse); it still hears\n"
    "  time [utc_seconds]   show the clock / set it (the office console does this when there's no NTP)\n"
    "  tz MIN [none|us|eu]  time zone: standard-time offset in minutes (US Eastern -300) and daylight-saving rule\n"
    "  sched                timed events: list | add HH:MM mon-fri SOUND [room N|floor N|group N] | del N | clear\n"
    "  dayoff [add|del DATE] days without timed events (2026-12-24); \"skip today\" / \"skip off\"\n"
    "  play SOUND           play a sound here now (chime bell lunch tone)\n"
    "  boottest [on|off|run] power-up light test: display segments, lamps, RGB LED (about 4 s at start)\n"
    "  lamp [white|ir|other 0-100|auto]  lamp outputs; other = call light (blinks for a call, on for an all-call)\n"
    "  ntp [server|default] the school's time server (from the next restart)\n"
    "  callofc [off]        call the office (as DOWN held 2 s; the console shows it until answered)\n"
    "  talk [seconds|off] [room N|floor N|group N]\n"
    "                       transmit as if TALK were held, to everyone or a target (USB only; default 5 s)\n"
    "  selftest [result]    check the board and the speaker->mic path (plays a short tone)\n"
    "  version              firmware version\n"
    "  netkey <key>         set the network command key (USB only; \"netkey off\" refuses all network commands)\n"
    "  webpass <password>   set the web interface password (USB only; \"webpass off\" turns it off)\n"
    "  wifi <ssid> <pass>   Wi-Fi for failover and portable units (USB only; \"wifi off\"; same IP as Ethernet)\n"
    "parameters:\n";
}  // namespace

Intercom::Intercom(const Hal& hal, EchoCanceller& aec, IntercomOptions opt) : hal_(hal), aec_(aec), opt_(opt) {
    cal_.setRunner(opt_.runBackground);
}

void Intercom::logf(const char* fmt, ...) {
    if (!hal_.log) return;
    char buf[480];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    hal_.log->log(buf);
}

void Intercom::begin() {
    unit_ = hal_.store->loadUnit();
    unit_.sub = 0;  // letters are gone since protocol 2
    group_ = uint16_t(std::strtoul(hal_.store->loadText(kGroupKey).c_str(), nullptr, 10));
    sched_.fromString(hal_.store->loadText(kSchedKey));
    if (hal_.store->loadText("boottest") != "off") bootT0_ = 1;  // runs from the first frame
    refreshPower();
    hal_.links->setWifi(hal_.store->loadText(kWifiSsidKey), hal_.store->loadText(kWifiPassKey));
    lastNonce_ = netauth::loadNonce(*hal_.store);
    params_ = TuneParams();
    params_.taps = std::min(params_.taps, opt_.maxTaps);
    std::string saved = hal_.store->loadText(kTuneKey);
    if (!saved.empty()) params_.fromString(saved);
    applyParams();
    char u[8], ip[16];
    formatUnit(unit_, u);
    formatIp(unit_, ip);
    logf("unit %s, ip %s, group %u, id %08lx, aec %s, %s tuning: %s", u, ip, unsigned(group()), (unsigned long)opt_.unitUid,
         aec_.name(), saved.empty() ? "default" : "saved", params_.toString().c_str());
}

void Intercom::applyParams() {
    if (params_.taps > opt_.maxTaps) params_.taps = opt_.maxTaps;
    aec_.configure(params_);
    applyCodec();
}

void Intercom::applyCodec() {
    Codec want = params_.opus && codecAvailable(Codec::Opus) ? Codec::Opus : Codec::Pcm16;
    if (!encoder_ || encoder_->codec() != want) encoder_ = makeEncoder(want);
    if (!encoder_) encoder_ = makeEncoder(Codec::Pcm16);
    encoder_->setBitrate(params_.bitrateKbps * 1000);
    encoder_->setFec(link_ == LinkType::WiFi, 5);  // Wi-Fi loses packets; wired doesn't
}

// ---- auto-tune -------------------------------------------------------------------------
void Intercom::startCal(const char* why) {
    if (cal_.active()) return;
    for (auto& t : talkers_) t->jb.reset();
    cal_.start(params_, opt_.maxTaps);
    logf("auto-tune started (%s): ~5 s, keep the room quiet", why);
}

void Intercom::finishCal() {
    const CalResult& r = cal_.result();
    haveCal_ = true;
    showResultUntil_ = frame_ + kResultFrames;
    logf("auto-tune: %s", r.summary().c_str());
    if (!r.ok) return;
    hal_.store->saveText(kTunePrevKey, params_.toString());
    params_ = cal_.tuned();
    applyParams();
    hal_.store->saveText(kTuneKey, params_.toString());
    logf("auto-tune applied and saved (\"undo\" restores the previous settings): %s", params_.toString().c_str());
}

// ---- console ---------------------------------------------------------------------------
std::string Intercom::command(const std::string& line, CmdSource src) {
    std::istringstream in(line);
    std::vector<std::string> a;
    std::string tok;
    while (in >> tok) a.push_back(tok);
    if (a.empty()) return "";
    const std::string& c = a[0];
    char buf[256];

    if (c == "help" || c == "?") return std::string(kHelp) + TuneParams::help();
    if (c == "get") {
        if (a.size() == 1) return params_.toString();
        std::string out, v;
        for (size_t i = 1; i < a.size(); i++) {
            if (!params_.get(a[i], &v)) return "unknown parameter " + a[i];
            out += (out.empty() ? "" : " ") + a[i] + "=" + v;
        }
        return out;
    }
    if (c == "set") {
        if (a.size() < 3 || a.size() % 2 == 0) return "usage: set key value [key value ...]";
        TuneParams p = params_;
        std::string out, v;
        for (size_t i = 1; i + 1 < a.size(); i += 2) {
            if (!p.set(a[i], a[i + 1])) return "bad parameter or value: " + a[i] + " " + a[i + 1];
            p.get(a[i], &v);
            out += (out.empty() ? "" : " ") + a[i] + "=" + v;
        }
        params_ = p;
        applyParams();
        logf("tuning set: %s", out.c_str());
        return "ok " + out + " (not saved; \"save\" keeps it)";
    }
    if (c == "save") {
        hal_.store->saveText(kTunePrevKey, hal_.store->loadText(kTuneKey));
        hal_.store->saveText(kTuneKey, params_.toString());
        return "saved: " + params_.toString();
    }
    if (c == "load") {
        std::string s = hal_.store->loadText(kTuneKey);
        params_ = TuneParams();
        if (!s.empty()) params_.fromString(s);
        applyParams();
        return "loaded: " + params_.toString();
    }
    if (c == "defaults") {
        params_ = TuneParams();
        applyParams();
        return "defaults (not saved): " + params_.toString();
    }
    if (c == "undo") {
        std::string prev = hal_.store->loadText(kTunePrevKey);
        if (prev.empty()) return "nothing to undo";
        std::string cur = hal_.store->loadText(kTuneKey);
        params_ = TuneParams();
        params_.fromString(prev);
        applyParams();
        hal_.store->saveText(kTuneKey, prev);
        hal_.store->saveText(kTunePrevKey, cur);
        return "restored and saved: " + params_.toString();
    }
    if (c == "cal") {
        if (a.size() > 1 && a[1] == "result") return haveCal_ ? cal_.result().summary() : "no auto-tune run yet";
        if (cal_.active() || testPhase_) return "auto-tune or self-test already running";
        startCal("console");
        return "auto-tune started: ~5 s, keep the room quiet; result in the log and \"cal result\"";
    }
    if (c == "stats") {
        const AecStats& s = aec_.stats();
        static const char* link[] = {"none", "ethernet", "wifi"};
        snprintf(buf, sizeof(buf), "erle_db=%.1f far=%d double_talk=%d converged=%d nlp_gain=%.2f link=%s mic=%u sent=%u spk=%u talkers=%d codec=%s",
                 double(s.erleDb), s.farActive, s.doubleTalk, s.converged, double(s.nlpGain), link[int(link_)], micLevel_,
                 sentLevel_, spkLevel_, playing_, codecName(sendCodec()));
        return buf;
    }
    if (c == "unit") {
        if (a.size() > 1) {
            uint16_t room;
            uint8_t sub;
            if (!parseTarget(a[1], &room, &sub) || room == kTargetAllRooms || sub != kTargetAnySub || room == 0)
                return "usage: unit 214 (1-999, no letters; several units in one room: own numbers plus group)";
            UnitId id;
            id.room = room;
            setUnit(id);
        }
        char u[8], ip[16];
        formatUnit(unit_, u);
        formatIp(unit_, ip);
        snprintf(buf, sizeof(buf), "unit %s ip %s group %u%s", u, ip, unsigned(group()),
                 duplicate() ? " DUPLICATE: another unit uses this number" : "");
        return buf;
    }
    if (c == "group") {
        if (a.size() > 1) {
            if (a[1] == "off") {
                group_ = 0;
            } else {
                unsigned long g = std::strtoul(a[1].c_str(), nullptr, 10);
                if (g < 1 || g > 999) return "usage: group 299 | group off";
                group_ = uint16_t(g == unit_.room ? 0 : g);
            }
            hal_.store->saveText(kGroupKey, group_ ? std::to_string(group_) : "");
            logf("room group set to %u", unsigned(group()));
            sendStatus();
        }
        snprintf(buf, sizeof(buf), "group %u%s", unsigned(group()), group_ ? "" : " (own room)");
        return buf;
    }
    if (c == "version") return std::string("firmware ") + kFirmwareVersion;
    if (c == "boottest") {
        if (a.size() > 1 && (a[1] == "on" || a[1] == "off")) hal_.store->saveText("boottest", a[1] == "off" ? "off" : "");
        if (a.size() > 1 && a[1] == "run") bootT0_ = frame_ ? frame_ : 1;
        return std::string("power-up light test ") + (hal_.store->loadText("boottest") == "off" ? "off" : "on") +
               (bootTesting() ? " (running)" : "");
    }
    if (c == "lamp") {
        static const char* names[] = {"white", "ir", "other"};
        if (a.size() >= 3) {
            int which = -1;
            for (int i = 0; i < int(Lamp::Count); i++)
                if (a[1] == names[i]) which = i;
            if (which < 0) return "usage: lamp white|ir|other 0-100|auto";
            if (a[2] == "auto") lampManual_[which] = 255;
            else lampManual_[which] = uint8_t(std::min(100L, std::max(0L, std::strtol(a[2].c_str(), nullptr, 10))));
            updateLamps();
        }
        std::string out;
        for (int i = 0; i < int(Lamp::Count); i++)
            out += std::string(i ? ", " : "") + names[i] + " " + std::to_string(lamp_[i]) + "%" + (lampManual_[i] == 255 ? " (auto)" : " (manual)");
        return out;
    }
    if (c == "ntp") {  // the school's time server (default: public pools); used from the next restart
        if (a.size() > 1) hal_.store->saveText("ntp", a[1] == "default" ? "" : a[1]);
        std::string s = hal_.store->loadText("ntp");
        return "ntp server: " + (s.empty() ? std::string("pool.ntp.org (default)") : s) + (a.size() > 1 ? " (from the next restart)" : "");
    }
    if (c == "mute") {  // office: stop a unit's TALK (stuck button, misuse); it still hears everyone
        if (a.size() > 1 && a[1] == "off") {
            muteUntil_ = 0;
        } else if (a.size() > 1) {
            float mins = std::strtof(a[1].c_str(), nullptr);
            if (!(mins > 0) || mins > 1440) return "usage: mute <minutes 1-1440> | mute off | mute";
            muteUntil_ = frame_ + uint32_t(mins * 60000 / kFrameMs);
            logf("TALK muted for %.0f min (%s)", double(mins), src == CmdSource::Local ? "USB" : "office");
        }
        sendStatus();
        if (!muted()) return "not muted";
        snprintf(buf, sizeof(buf), "TALK muted, %u min left", unsigned((muteUntil_ - frame_) * kFrameMs / 60000 + 1));
        return buf;
    }
    if (c == "talk") {
        if (src != CmdSource::Local) return "talk only works from the USB console";
        if (a.size() > 1 && a[1] == "off") {
            talkFrames_ = 0;
            return "talk off";
        }
        float secs = a.size() > 1 ? std::strtof(a[1].c_str(), nullptr) : 5.0f;
        secs = std::max(0.0f, std::min(secs, 60.0f));
        TalkTarget t;
        if (a.size() > 2) {
            unsigned long n = a.size() > 3 ? std::strtoul(a[3].c_str(), nullptr, 10) : 0;
            if (a[2] == "room") t.kind = TalkTarget::Room;
            else if (a[2] == "floor") t.kind = TalkTarget::Floor;
            else if (a[2] == "group") t.kind = TalkTarget::Group;
            else n = 0;
            if (n < 1 || n > 999) return "usage: talk [seconds] [room N|floor N|group N]";
            t.n = uint16_t(n);
        }
        talkTarget_ = t;
        talkFrames_ = uint32_t(secs * 1000 / kFrameMs);
        static const char* kinds[] = {"everyone", "room", "floor", "group"};
        std::string num = t.kind ? " " + std::to_string(t.n) : std::string();
        snprintf(buf, sizeof(buf), "talking for %.0f s to %s%s (\"talk off\" stops)", double(secs), kinds[t.kind], num.c_str());
        return buf;
    }
    if (c == "time") {
        if (a.size() > 1) {
            long long t = std::strtoll(a[1].c_str(), nullptr, 10);
            if (t < 1600000000LL || t > 4100000000LL) return "usage: time <UTC seconds since 1970>";
            setEpoch_ = t;
            setFrame_ = frame_;
        }
        int64_t now = utcNow();
        if (!now) return "no clock yet (no NTP; the office console sets it)";
        LocalTime lt = sched_.local(now);
        static const char* wd[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        bool ntp = opt_.utcNow && opt_.utcNow() > 1600000000LL;
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d %s%s, clock from %s%s", lt.year, lt.month, lt.day, lt.hour, lt.minute,
                 lt.second, wd[lt.wday], lt.dst ? " (daylight saving)" : "", ntp ? "NTP" : "the office",
                 sched_.isDayOff(lt) ? ", day off: no timed events today" : "");
        return buf;
    }
    if (c == "tz") {
        if (a.size() < 2) return sched_.list().substr(0, sched_.list().find('\n'));
        int m = std::atoi(a[1].c_str());
        if (m < -720 || m > 840) return "usage: tz <minutes from UTC, standard time> [none|us|eu]";
        sched_.tzMinutes = m;
        if (a.size() > 2) sched_.dst = a[2] == "us" ? DstRule::Us : a[2] == "eu" ? DstRule::Eu : DstRule::None;
        saveSchedule();
        return "ok " + sched_.list().substr(0, sched_.list().find('\n'));
    }
    if (c == "sched" || c == "dayoff" || c == "skip") return schedCommand(a);
    if (c == "play") {
        Sound s;
        if (a.size() < 2 || !soundFromName(a[1], &s)) return "usage: play " + soundList();
        startSound(s);
        return std::string("playing ") + soundName(s);
    }
    if (c == "callofc") {
        setCallingOffice(!(a.size() > 1 && a[1] == "off"), src == CmdSource::Local ? "USB" : "console");
        return callingOffice_ ? "calling the office" : "not calling the office";
    }
    if (c == "selftest") {
        if (a.size() > 1 && a[1] == "result") return testResult_.empty() ? "no self-test run yet" : testResult_;
        if (cal_.active() || testPhase_) return "busy (auto-tune or self-test running)";
        testBoard_ = hal_.diag ? hal_.diag->boardTest() : std::string("ok board checks not available on this platform\n");
        testPhase_ = 1;
        testCount_ = 0;
        testQuiet_ = testLoud_ = 0;
        return "self-test started (~1.5 s, plays a tone); \"selftest result\" shows the result";
    }
    if (c == "netkey") {
        if (src != CmdSource::Local) return "netkey only works from the USB console";
        if (a.size() != 2)
            return netauth::loadKey(*hal_.store).empty() ? "network commands off (no key)" : "network commands on (key set)";
        if (a[1] == "off") {
            netauth::saveKey(*hal_.store, "");
            return "network commands off";
        }
        if (int(a[1].size()) < netauth::kMinKeyLen) return "key too short (at least 16 characters, no spaces)";
        netauth::saveKey(*hal_.store, a[1]);
        logf("network key changed");
        return "network key set; network commands on";
    }
    if (c == "tone") {
        float secs = a.size() > 1 ? std::strtof(a[1].c_str(), nullptr) : 2.0f;
        if (secs < 0) secs = 0;
        if (secs > 30) secs = 30;
        toneFrames_ = uint32_t(secs * 1000 / kFrameMs);
        return toneFrames_ ? "playing a 1 kHz test tone" : "tone stopped";
    }
    if (c == "wifi") {
        if (src != CmdSource::Local) return "wifi only works from the USB console";
        if (a.size() == 2 && a[1] == "off") {
            hal_.store->saveText(kWifiSsidKey, "");
            hal_.store->saveText(kWifiPassKey, "");
            hal_.links->setWifi("", "");
            return "wifi off";
        }
        if (a.size() == 3) {
            if (a[2].size() < 8) return "password too short (WPA2 needs at least 8 characters)";
            hal_.store->saveText(kWifiSsidKey, a[1]);
            hal_.store->saveText(kWifiPassKey, a[2]);
            hal_.links->setWifi(a[1], a[2]);
            logf("Wi-Fi set to \"%s\"", a[1].c_str());
            return "wifi set to \"" + a[1] + "\"; it connects in the background";
        }
        std::string ssid = hal_.store->loadText(kWifiSsidKey);
        if (ssid.empty()) return "wifi off (usage: wifi <ssid> <password>; names with spaces are not supported)";
        return "wifi \"" + ssid + "\" " + (hal_.links->wifiUp() ? "connected" : "not connected");
    }
    if (c == "webpass") {
        if (src != CmdSource::Local) return "webpass only works from the USB console";
        if (a.size() != 2) return auth::hasPassword(*hal_.store) ? "web interface on (password set)" : "web interface off (no password)";
        if (a[1] == "off") {
            auth::clearPassword(*hal_.store);
            return "web interface off";
        }
        if (int(a[1].size()) < auth::kMinPasswordLen) return "password too short (at least 8 characters, no spaces)";
        auth::setPassword(*hal_.store, a[1], frame_ ^ unit_.key());
        logf("web password changed");
        return "web password set; web interface on";
    }
    return "unknown command \"" + c + "\" (try help)";
}

void Intercom::sendReply(const TextPacket& to, const std::string& text) {
    // Split long replies (help) into packets on line boundaries.
    TextPacket r;
    r.type = PacketType::Reply;
    r.from = unit_;
    r.targetRoom = to.from.room;
    r.targetSub = to.from.sub;
    size_t pos = 0;
    uint8_t buf[kMaxPacketBytes];
    do {
        size_t n = std::min(text.size() - pos, kMaxTextBytes);
        if (pos + n < text.size()) {
            size_t nl = text.rfind('\n', pos + n);
            if (nl != std::string::npos && nl > pos) n = nl + 1 - pos;
        }
        r.text = text.substr(pos, n);
        hal_.net->send(buf, encodeText(r, buf));
        pos += n;
    } while (pos < text.size());
}

// ---- network ---------------------------------------------------------------------------
void Intercom::updateLink() {
    bool eth = ethUp_ = hal_.links->ethernetUp();
    bool wifi = hal_.links->wifiUp();
    ethStable_ = eth ? ethStable_ + 1 : 0;

    LinkType want = LinkType::None;
    if (eth && ethStable_ >= kEthStableFrames) want = LinkType::Ethernet;
    else if (wifi) want = LinkType::WiFi;
    else if (eth) want = LinkType::Ethernet;  // only link available, use it while it settles

    if (want == link_) return;
    static const char* names[] = {"none", "Ethernet", "Wi-Fi"};
    if (link_ == LinkType::None) {
        logf("link %s, %u ms %s", names[int(want)], unsigned((frame_ - linkLostAt_) * kFrameMs),
             everLinked_ ? "without network" : "after start");
    } else {
        logf("link %s -> %s", names[int(link_)], names[int(want)]);
    }
    if (want == LinkType::None) linkLostAt_ = frame_;
    else everLinked_ = true;
    link_ = want;
    hal_.links->useLink(want);
    if (encoder_) encoder_->setFec(link_ == LinkType::WiFi, 5);
    sendStatus();  // tell the console right away
}

Intercom::Talker* Intercom::talkerFor(const UnitId& id) {
    for (auto& t : talkers_)
        if (t->id == id) return t.get();
    if (talkers_.size() >= kMaxTalkers) return nullptr;
    talkers_.push_back(std::unique_ptr<Talker>(new Talker()));
    talkers_.back()->id = id;
    return talkers_.back().get();
}

void Intercom::setUnit(const UnitId& id) {
    if (id == unit_) return;
    unit_ = id;
    hal_.store->saveUnit(unit_);
    hal_.links->setAddress(unit_);
    char u[8], ip[16];
    formatUnit(unit_, u);
    formatIp(unit_, ip);
    logf("room set to %s, ip %s", u, ip);
    sendStatus();
}

// Self-test audio check: 0.5 s quiet, then 1 s of 1 kHz tone; the mic must hear the tone.
void Intercom::selfTestFrame(const int16_t* mic, int16_t* speaker) {
    constexpr int kQuietFrames = 25, kToneFrames = 50, kSkipFrames = 5;
    double p = 0;
    for (int i = 0; i < kFrameSamples; i++) p += double(mic[i]) * mic[i];
    p /= kFrameSamples;
    std::memset(speaker, 0, sizeof(int16_t) * kFrameSamples);
    if (testPhase_ == 1) {
        testQuiet_ += p;
        if (++testCount_ >= kQuietFrames) {
            testPhase_ = 2;
            testCount_ = 0;
        }
        return;
    }
    const float w = 2.0f * 3.14159265f * 1000.0f / kSampleRate;
    for (int i = 0; i < kFrameSamples; i++, tonePhase_++)
        speaker[i] = int16_t(8200.0f * std::sin(w * float(tonePhase_ % kSampleRate)));
    if (testCount_ >= kSkipFrames) testLoud_ += p;  // skip the speaker->mic latency at the start
    if (++testCount_ < kToneFrames) return;

    double quietDb = 10 * std::log10(testQuiet_ / kQuietFrames / (32768.0 * 32768.0) + 1e-12);
    double loudDb = 10 * std::log10(testLoud_ / (kToneFrames - kSkipFrames) / (32768.0 * 32768.0) + 1e-12);
    char buf[200];
    bool ok = loudDb - quietDb >= 15 && loudDb > -60;
    snprintf(buf, sizeof(buf), "%s speaker->mic: tone heard at %.1f dBFS, quiet room %.1f dBFS (%.1f dB above)%s\n",
             ok ? "ok" : "FAIL", loudDb, quietDb, loudDb - quietDb, ok ? "" : " - check the speaker, amp and mics");
    testResult_ = testBoard_ + buf;
    bool allOk = testResult_.find("FAIL") == std::string::npos;
    testResult_ += allOk ? "SELF-TEST PASSED" : "SELF-TEST FAILED";
    testPhase_ = 0;
    logf("self-test:\n%s", testResult_.c_str());
}

void Intercom::receive() {
    uint8_t buf[kMaxPacketBytes + 16];
        size_t n;
    while ((n = hal_.net->recv(buf, sizeof(buf))) > 0) {
        if (link_ == LinkType::None) continue;
        bool ok;
        PacketType type = peekType(buf, n, &ok);
        if (!ok) continue;
        if (type == PacketType::Audio) {
            if (cal_.measuring() || testPhase_) continue;  // the room must stay quiet while measuring
            AudioHeader h;
            const uint8_t* payload;
            size_t plen;
            if (!decodeAudioData(buf, n, &h, &payload, &plen) || !codecAvailable(h.codec)) continue;
            if (h.from == unit_) continue;                                               // our own multicast echo
            if (sameGroup(h.from) && !(h.flags & kAudioFromConsole)) continue;            // same room group: heard acoustically
            if (!isForMe(h)) continue;                                                     // a call for another room
            if (h.target == 0 && !(h.flags & kAudioReply) && (h.flags & kAudioFromConsole))
                allCallUntil_ = frame_ + kAllCallHoldFrames;
            if (h.target != 0 || (h.flags & kAudioReply)) {
                if (frame_ < allCallUntil_) continue;  // an office all-call is on: it wins
                if (!inCall()) {
                    char u[8];
                    formatUnit(h.from, u);
                    logf("call from %s", h.from.room ? u : "the office");
                    if (!canAnswer()) startSound(Sound::Chime);  // a new call, not the next sentence of one
                }
                callFrom_ = h.from;
                callLast_ = frame_;
                if (h.flags & kAudioFromConsole) setCallingOffice(false, "the office answered");
            }
            if (Talker* t = talkerFor(h.from)) {
                t->jb.push(h.seq, h.codec, payload, plen, h.level);
                t->lastFrame = frame_;
            }
        } else if (type == PacketType::Status) {
            StatusPacket s;
            if (!decodeStatus(buf, n, &s)) continue;
            if (s.from == unit_) {  // our own multicast echo, or another unit set to our room number
                if (s.unitUid && opt_.unitUid && s.unitUid != opt_.unitUid) {
                    if (!duplicate() || s.unitUid != dupUid_)
                        logf("DUPLICATE: unit %08lx also uses room %u", (unsigned long)s.unitUid, unsigned(unit_.room));
                    dupUntil_ = frame_ + kDupHoldFrames;
                    dupUid_ = s.unitUid;
                }
                continue;
            }
            Peer* p = nullptr;
            for (auto& q : peers_)
                if (q.status.from == s.from) p = &q;
            if (!p) {
                peers_.push_back(Peer());
                p = &peers_.back();
            }
            p->status = s;
            p->lastFrame = frame_;
        } else if (type == PacketType::Command) {
            // Signed with the network key (netkey), and never replayed.
            TextPacket t;
            if (!decodeText(buf, n, &t) || !t.addressedTo(unit_)) continue;
            std::string cmd;
            uint64_t nonce = 0;
            netauth::Check ck = netauth::verify(netauth::loadKey(*hal_.store), lastNonce_, t, &cmd, &nonce);
            if (ck != netauth::Check::Ok) {
                logf("net command from %u refused: %s", unsigned(t.from.room), netauth::describe(ck));
                sendReply(t, netauth::describe(ck));
                continue;
            }
            lastNonce_ = nonce;
            netauth::saveNonce(*hal_.store, nonce);
            std::string reply = command(cmd, CmdSource::Network);
            bool secret = cmd.compare(0, 7, "webpass") == 0 || cmd.compare(0, 6, "netkey") == 0;
            logf("net command from %u: %s", unsigned(t.from.room), secret ? "(password command, hidden)" : cmd.c_str());
            sendReply(t, reply);
        }
    }
    for (size_t i = 0; i < peers_.size();) {
        if (frame_ - peers_[i].lastFrame > uint32_t(kPeerTimeoutFrames)) peers_.erase(peers_.begin() + long(i));
        else i++;
    }
}

void Intercom::sendStatus() {
    if (link_ == LinkType::None) return;
    refreshPower();
    const PowerState& pw = power_;
    StatusPacket s;
    s.from = unit_;
    s.link = link_;
    s.power = pw.source;
    s.battery = pw.hasBattery ? pw.batteryPct : 0;
    s.flags = uint8_t((transmitting_ ? kStatusTalking : 0) | (muted() ? kStatusMuted : 0) | (talkHeld() ? kStatusTalkHeld : 0) |
                      (callingOffice_ ? kStatusCalling : 0));
    s.fault = faultCode();
    s.uptimeS = frame_ * kFrameMs / 1000;
    s.unitUid = opt_.unitUid;
    s.group = group_;
    uint8_t buf[32];
    hal_.net->send(buf, encodeStatus(s, buf));
}

void Intercom::updatePanel() {
    if (frame_ % kStatusEveryFrames == 1) refreshPower();  // once a second, whatever the link
    char text[3];
    uint8_t dots = 0;
    uint8_t testLamps[int(Lamp::Count)];
    uint32_t testRgb;
    if (bootT0_ && bootTestStep(text, &dots, testLamps, &testRgb) < 0) bootT0_ = 0;  // test over
    if (bootT0_) {
        // power-up light test: text and dots already set
    } else if (setup_.active()) {
        setup_.render(frame_, text, &dots);
    } else if (cal_.active()) {  // "c42" = auto-tune 42 % done
        int pct = std::min(99, cal_.progressPct());
        text[0] = 'c';
        text[1] = char('0' + pct / 10);
        text[2] = char('0' + pct % 10);
    } else if (frame_ < showResultUntil_) {  // "e17" = 17 dB echo reduction, "err" = failed
        const CalResult& r = cal_.result();
        if (r.ok) {
            int e = std::max(0, std::min(99, int(r.erleAfterDb + 0.5f)));
            text[0] = 'e';
            text[1] = char('0' + e / 10);
            text[2] = char('0' + e % 10);
        } else {
            text[0] = 'e';
            text[1] = text[2] = 'r';
        }
    } else {
        unsigned r = unit_.room % 1000;
        text[0] = r >= 100 ? char('0' + r / 100) : ' ';
        text[1] = r >= 10 ? char('0' + (r / 10) % 10) : ' ';
        text[2] = char('0' + r % 10);
        if (r == 0) text[0] = text[1] = text[2] = '-';  // no room number set yet
        if (canAnswer() && !transmitting_) {  // a call for this room: CAL (TALK answers the caller)
            std::memcpy(text, "CAL", 3);
        } else if (callingOffice_ && !transmitting_ && (frame_ / 50) % 2) {  // calling the office: oFC / number
            std::memcpy(text, "oFC", 3);
        }
        if (transmitting_) dots |= 0x01;
        if (playing_) dots |= 0x02;
        // The room number stays the main thing shown. A duplicate number alternates with "dUP" every
        // second; other faults flash their code for 0.5 s every 3.5 s.
        const char* f = fault();
        bool showCode = duplicate() ? ((frame_ / 50) % 2 != 0) : (*f && frame_ % kFaultCycle >= kFaultCycle - kFaultShow);
        if (*f && showCode) {
            std::memcpy(text, f, 3);
            dots = 0;
        }
    }
    if (std::memcmp(text, shown_, 3) != 0 || dots != shownDots_) {  // the display sits behind I2C: write only on change
        std::memcpy(shown_, text, 3);
        shownDots_ = dots;
        hal_.panel->show(text, dots);
    }
}

const char* Intercom::fault() const {
    if (duplicate()) return "dUP";
    if (link_ == LinkType::None) return "E2 ";                        // no network at all
    if (power_.source == PowerSource::PoE && !ethUp_) return "E1 ";  // PoE power but no Ethernet link
    if (power_.source == PowerSource::Battery && power_.hasBattery && power_.batteryPct < kLowBatteryPct) return "Lo ";
    return "";
}

Fault Intercom::faultCode() const {
    const char* f = fault();
    if (f[0] == 'd') return Fault::Duplicate;
    if (f[0] == 'E') return f[1] == '1' ? Fault::NoEthernet : Fault::NoNetwork;
    if (f[0] == 'L') return Fault::LowBattery;
    return Fault::None;
}

bool Intercom::isForMe(const AudioHeader& h) const {
    if (h.flags & kAudioReply) return h.target != 0 && h.target == unit_.room;  // 0 = an answer to the office console
    if (h.target == 0) return true;
    if (h.flags & kAudioFloor) return unit_.room / 100 == h.target;
    if (h.flags & kAudioGroupCall) return group() == h.target || unit_.room == h.target;
    return h.target == unit_.room;
}

int64_t Intercom::utcNow() const {
    int64_t t = opt_.utcNow ? opt_.utcNow() : 0;
    if (t > 1600000000LL) return t;
    if (setEpoch_) return setEpoch_ + int64_t(frame_ - setFrame_) * kFrameMs / 1000;
    return 0;
}

void Intercom::startSound(Sound s) {
    sound_ = s;
    soundPos_ = 0;
    soundActive_ = true;
}

void Intercom::saveSchedule() { hal_.store->saveText(kSchedKey, sched_.toString()); }

std::string Intercom::schedCommand(const std::vector<std::string>& a) {
    const std::string& c = a[0];
    std::string sub = a.size() > 1 ? a[1] : "";
    if (c == "skip") {
        if (sub == "off") sched_.skipDay = 0;
        else if (sub == "today") {
            int64_t now = utcNow();
            if (!now) return "no clock yet";
            sched_.skipDay = sched_.local(now).ymd();
        } else {
            return "usage: skip today | skip off";
        }
        saveSchedule();
        return sched_.skipDay ? "no timed events today" : "timed events back on";
    }
    if (c == "dayoff") {
        if (sub.empty()) return sched_.list();
        uint32_t d;
        if ((sub != "add" && sub != "del") || a.size() < 3 || !Schedule::parseDate(a[2], &d)) return "usage: dayoff add|del 2026-12-24";
        auto it = std::find(sched_.daysOff.begin(), sched_.daysOff.end(), d);
        if (sub == "add" && it == sched_.daysOff.end()) {
            if (sched_.daysOff.size() >= Schedule::kMaxDaysOff) return "too many days off (60)";
            sched_.daysOff.push_back(d);
            std::sort(sched_.daysOff.begin(), sched_.daysOff.end());
        }
        if (sub == "del" && it != sched_.daysOff.end()) sched_.daysOff.erase(it);
        saveSchedule();
        return "ok";
    }
    // sched
    if (sub.empty() || sub == "list") return sched_.list();
    if (sub == "add") {
        ScheduleEntry e;
        std::string err = Schedule::parseEntry(a, 2, &e);
        if (err.empty()) err = sched_.add(e);
        if (!err.empty()) return err;
        saveSchedule();
        return "added: " + Schedule::formatEntry(e);
    }
    if (sub == "del") {
        unsigned long n = a.size() > 2 ? std::strtoul(a[2].c_str(), nullptr, 10) : 0;
        if (n < 1 || n > sched_.entries.size()) return "usage: sched del N (the number from \"sched\")";
        std::string gone = Schedule::formatEntry(sched_.entries[n - 1]);
        sched_.entries.erase(sched_.entries.begin() + long(n - 1));
        saveSchedule();
        return "deleted: " + gone;
    }
    if (sub == "clear") {
        sched_.entries.clear();
        saveSchedule();
        return "no timed events";
    }
    if (sub == "set") {  // the console pushes the whole table at once (as "sched get" prints it)
        std::string s;
        for (size_t i = 2; i < a.size(); i++) s += (i > 2 ? " " : "") + a[i];
        Schedule n;
        if (!n.fromString(s)) return "bad schedule";
        sched_ = n;
        saveSchedule();
        return "ok, " + std::to_string(sched_.entries.size()) + " timed events";
    }
    if (sub == "get") return sched_.toString();
    return "usage: sched [list | add HH:MM DAYS SOUND [room N|floor N|group N] | del N | clear | get | set ...]";
}

void Intercom::checkSchedule() {
    int64_t now = utcNow();
    if (!now) return;
    int64_t minute = now / 60;
    if (minute == lastSchedMinute_) return;
    bool first = lastSchedMinute_ == 0;
    lastSchedMinute_ = minute;
    LocalTime lt = sched_.local(now);
    if (first && lt.second > 20) return;  // just started mid-minute: don't play an event late
    if (sched_.isDayOff(lt)) return;
    for (const ScheduleEntry& e : sched_.entries) {
        if (e.minute != lt.hour * 60 + lt.minute || !(e.days & (1u << lt.wday)) || !sched_.appliesTo(e, unit_.room, group())) continue;
        logf("timed event %s", Schedule::formatEntry(e).c_str());
        pendingSound_ = true;
        pending_ = e.sound;
        pendingSince_ = frame_;
        break;  // one sound a minute
    }
}

// The call light on the "other" lamp output (J11): blinks 0.6 s on / 0.6 s off while a call for this
// room is on (and while TALK would answer it), steady during an office all-call. White and IR stay
// off unless set by hand (R3: night vision, emergency).
void Intercom::updateLamps() {
    uint8_t want[int(Lamp::Count)] = {0, 0, 0};
    uint32_t rgb = 0;
    char t[3];
    uint8_t d;
    if (bootT0_ && bootTestStep(t, &d, want, &rgb) >= 0) {
        // power-up light test drives the lamps and the RGB LED
    } else if (canAnswer() && !transmitting_) want[int(Lamp::Other)] = (frame_ / 30) % 2 ? 0 : 100;
    else if (frame_ < allCallUntil_) want[int(Lamp::Other)] = 100;
    for (int i = 0; i < int(Lamp::Count); i++) {
        uint8_t v = lampManual_[i] == 255 ? want[i] : lampManual_[i];
        if (v == lamp_[i]) continue;
        lamp_[i] = v;
        if (hal_.lamps) hal_.lamps->set(Lamp(i), v);
    }
    if (rgb != rgb_) {
        rgb_ = rgb;
        if (hal_.lamps) hal_.lamps->setRgb(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb));
    }
}

// Power-up light test, so a dead segment, lamp or LED shows at a glance:
//   segments a..g one at a time on all three digits, then the dots   (8 x 0.16 s)
//   everything on, "8.8.8."                                           (0.5 s)
//   L1 white lamp, L2 IR lamp, L3 call light (J11)                     (0.4 s each)
//   L4 / L5 / L6: RGB LED red, green, blue                             (0.3 s each)
// MISC outputs (one may carry the laser) and servo power are left out on purpose.
// Returns the step number, or -1 when the test is over.
int Intercom::bootTestStep(char text[3], uint8_t* dots, uint8_t lamps[], uint32_t* rgb) const {
    if (!bootT0_ || frame_ < bootT0_) return -1;
    uint32_t t = frame_ - bootT0_;
    text[0] = text[1] = text[2] = ' ';
    *dots = 0;
    *rgb = 0;
    for (int i = 0; i < int(Lamp::Count); i++) lamps[i] = 0;
    if (t < 64) {  // one segment at a time
        uint32_t s = t / 8;
        if (s < 7) text[0] = text[1] = text[2] = char(0x80 | (1u << s));
        else *dots = 0x07;
        return int(s);
    }
    if (t < 89) {
        text[0] = text[1] = text[2] = char(0x80 | 0x7f);
        *dots = 0x07;
        return 8;
    }
    if (t < 149) {
        int n = int((t - 89) / 20);  // 0 white, 1 IR, 2 other
        text[0] = 'L';
        text[1] = char('1' + n);
        lamps[n] = 100;
        return 9 + n;
    }
    if (t < 194) {
        int n = int((t - 149) / 15);
        static const uint32_t colours[] = {0xff0000, 0x00ff00, 0x0000ff};
        text[0] = 'L';
        text[1] = char('4' + n);
        *rgb = colours[n];
        return 12 + n;
    }
    return -1;
}

bool Intercom::inCall() const { return callLast_ && frame_ - callLast_ < kCallFrames; }
bool Intercom::canAnswer() const { return callLast_ && frame_ - callLast_ < kAnswerFrames; }

void Intercom::setCallingOffice(bool on, const char* why) {
    if (on == callingOffice_) return;
    callingOffice_ = on;
    callingSince_ = frame_;
    logf(on ? "calling the office (%s)" : "call to the office ended (%s)", why);
    sendStatus();
}

bool Intercom::sameGroup(const UnitId& from) const {
    if (from.room == unit_.room) return true;
    uint16_t theirs = from.room;
    for (const auto& p : peers_)
        if (p.status.from == from && p.status.group) theirs = p.status.group;
    return theirs == group();
}

void Intercom::refreshPower() { power_ = hal_.power->read(); }

AudioStats Intercom::audioStats() const {
    AudioStats s = retired_;
    for (const auto& t : talkers_) {
        s.concealed += t->jb.concealed();
        s.lateDrops += t->jb.lateDrops();
    }
    return s;
}

// ---- the frame -------------------------------------------------------------------------
void Intercom::processFrame(const int16_t* mic, int16_t* speaker) {
    frame_++;
    micLevel_ = frameLevel(mic, kFrameSamples);
    uint8_t held = hal_.panel->buttons();
    const uint8_t talkBit = 1u << kBtnTalk;
    const uint8_t combo = (1u << kBtnMode) | (1u << kBtnUp);

    // Auto-tune from the panel: MODE + UP held 3 s, or UP alone held 5 s. (Both work on boards
    // from 75d75fb, where the buttons are on their own expander; the older TM1637 key scan saw one key.)
    const bool comboHeld = (held & combo) == combo;
    const bool upHeld = held == (1u << kBtnUp);
    if ((comboHeld || upHeld) && !setup_.active()) {
        if (!comboSince_) comboSince_ = frame_;
        else if (frame_ - comboSince_ == (comboHeld ? kComboFrames : kUpHoldFrames)) startCal("panel");
    } else {
        comboSince_ = 0;
    }

    if (cal_.analyzing() && cal_.poll()) finishCal();
    if (testPhase_) {
        selfTestFrame(mic, speaker);
        spkLevel_ = frameLevel(speaker, kFrameSamples);
        transmitting_ = false;
        sentLevel_ = playing_ = 0;
        aec_.playback(speaker);
        updateLink();
        receive();
        if (frame_ % kStatusEveryFrames == 0) sendStatus();
        updatePanel();
        return;
    }
    if (cal_.measuring()) {
        if (held & talkBit) {
            cal_.abort();
            logf("auto-tune cancelled (TALK pressed)");
        } else {
            transmitting_ = false;
            sentLevel_ = playing_ = 0;
            if (cal_.frame(mic, speaker)) finishCal();
            spkLevel_ = frameLevel(speaker, kFrameSamples);
            updateLink();
            receive();
            if (frame_ % kStatusEveryFrames == 0) sendStatus();
            updatePanel();
            return;
        }
    }

    int16_t clean[kFrameSamples];
    aec_.capture(mic, clean);

    // Panel: room-number setup and TALK. (Hidden SETUP held 3 s = setup; MODE + UP or UP held = auto-tune.)
    UnitId id = unit_;
    uint8_t setupHeld = comboSince_ ? uint8_t(held & ~(1u << kBtnMode)) : held;
    if (setup_.update(setupHeld, frame_, &id) && id != unit_) setUnit(id);
    if (talkFrames_ && --talkFrames_ == 0) talkTarget_ = TalkTarget();  // "talk ... room N" is for that once
    if (muteUntil_ && frame_ >= muteUntil_) {
        muteUntil_ = 0;
        logf("mute ended");
        sendStatus();
    }
    bool talk = ((held & talkBit) || talkFrames_) && !setup_.active() && !muted();
    // DOWN held 2 s (outside setup) = call the office; again = cancel. It gives up after 5 minutes.
    if (held == (1u << kBtnDown) && !setup_.active()) {
        if (!downSince_) downSince_ = frame_;
        else if (frame_ - downSince_ == kCallHoldFrames) setCallingOffice(!callingOffice_, "DOWN");
    } else {
        downSince_ = 0;
    }
    if (callingOffice_ && frame_ - callingSince_ >= kCallingTimeoutFrames) setCallingOffice(false, "no answer in 5 min");
    if (talk != transmitting_) {
        transmitting_ = talk;
        talkSince_ = talk ? frame_ : 0;
        if (talk) {  // who this transmission goes to
            if (canAnswer()) {
                txTarget_ = callFrom_.room;  // 0 = the office console
                txFlags_ = kAudioReply;
            } else {
                static const uint8_t flags[] = {kAudioAllCall, 0, kAudioFloor, kAudioGroupCall};
                txTarget_ = talkTarget_.kind == TalkTarget::All ? 0 : talkTarget_.n;
                txFlags_ = flags[talkTarget_.kind];
            }
        }
        sendStatus();
    }
    if (talk && txFlags_ == kAudioReply) callLast_ = frame_;  // keep the conversation open while answering
    if (talkSince_ && frame_ - talkSince_ == kTalkHeldFrames) {
        logf("TALK held for over 2 minutes (stuck button?)");
        sendStatus();
    }

    updateLink();

    if (transmitting_ && link_ != LinkType::None) {
        if (params_.micGainDb != 0) {
            float g = std::pow(10.0f, params_.micGainDb / 20.0f);
            for (int i = 0; i < kFrameSamples; i++) {
                float v = clean[i] * g;
                clean[i] = int16_t(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
            }
        }
        AudioHeader h;
        h.from = unit_;
        h.flags = txFlags_;
        h.target = txTarget_;
        h.seq = seq_++;
        h.level = sentLevel_ = frameLevel(clean, kFrameSamples);
        uint8_t coded[kMaxCodedBytes], buf[kMaxPacketBytes];
        size_t clen = encoder_ ? encoder_->encode(clean, coded, sizeof(coded)) : 0;
        if (clen) {
            h.codec = encoder_->codec();
            hal_.net->send(buf, encodeAudioData(h, coded, clen, buf));
        } else {
            hal_.net->send(buf, encodeAudio(h, clean, buf));  // encoder failed: send this frame raw
        }
    } else {
        sentLevel_ = 0;
    }

    receive();

    // Mix everyone talking.
    int16_t frames[kMaxTalkers][kFrameSamples];
    MixInput in[kMaxTalkers];
    int n = 0;
    for (size_t i = 0; i < talkers_.size();) {
        Talker& t = *talkers_[i];
        uint8_t lvl;
        if (t.jb.pop(frames[n], &lvl)) {
            in[n].pcm = frames[n];
            in[n].level = lvl;
            n++;
            retired_.framesPlayed++;
            bool found = false;
            for (auto& h : heard_)
                if (h.first == t.id.key()) { h.second++; found = true; }
            if (!found) heard_.push_back({t.id.key(), 1});
        }
        if (!t.jb.playing() && frame_ - t.lastFrame > uint32_t(kTalkerIdleFrames)) {
            retired_.concealed += t.jb.concealed();
            retired_.lateDrops += t.jb.lateDrops();
            talkers_.erase(talkers_.begin() + long(i));
        } else {
            i++;
        }
    }
    playing_ = n;
    float gainDb = params_.volumeDb + ((transmitting_ && params_.duck) ? params_.duckDb : 0.0f);
    if (n) mixer_.mix(in, n, std::pow(10.0f, gainDb / 20.0f), speaker);
    else std::memset(speaker, 0, sizeof(int16_t) * kFrameSamples);
    if (toneFrames_) {  // test tone, -12 dBFS, mixed in before the echo canceller's reference
        toneFrames_--;
        const float w = 2.0f * 3.14159265f * 1000.0f / kSampleRate;
        for (int i = 0; i < kFrameSamples; i++, tonePhase_++) {
            int v = speaker[i] + int(8200.0f * std::sin(w * float(tonePhase_ % kSampleRate)));
            speaker[i] = int16_t(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
    }
    // Timed events: a waiting chime plays once nobody is talking (up to 60 s late); a call or all-call cuts one.
    if (frame_ % kStatusEveryFrames == 0) checkSchedule();
    const bool busy = n > 0 || transmitting_ || inCall();
    if (soundActive_ && sound_ != Sound::Chime && busy) {
        soundActive_ = false;
        logf("timed event cut short by a call");
    }
    if (pendingSound_ && !busy && !soundActive_) {
        pendingSound_ = false;
        startSound(pending_);
    } else if (pendingSound_ && frame_ - pendingSince_ > kPendingSoundFrames) {
        pendingSound_ = false;
        logf("timed event skipped: a call lasted over a minute");
    }
    if (soundActive_) {  // the call chime or a timed sound, mixed in before the echo canceller's reference
        soundActive_ = renderSound(sound_, soundPos_, speaker, kFrameSamples);
        soundPos_ += kFrameSamples;
    }
    spkLevel_ = frameLevel(speaker, kFrameSamples);
    aec_.playback(speaker);

    if (frame_ % kStatusEveryFrames == 0) sendStatus();
    updatePanel();
    updateLamps();
}

}  // namespace ic
