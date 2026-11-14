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
    "  unit [214a]          show or set the room number (sets the IP address)\n"
    "  tone [seconds]       play a 1 kHz test tone on this unit's speaker (default 2 s, 0 stops)\n"
    "  talk [seconds|off]   transmit as if TALK were held (USB only; default 5 s, bring-up without the panel)\n"
    "  selftest [result]    check the board and the speaker->mic path (plays a short tone)\n"
    "  version              firmware version\n"
    "  netkey <key>         set the network command key (USB only; \"netkey off\" refuses all network commands)\n"
    "  webpass <password>   set the web interface password (USB only; \"webpass off\" turns it off)\n"
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
    lastNonce_ = netauth::loadNonce(*hal_.store);
    params_ = TuneParams();
    params_.taps = std::min(params_.taps, opt_.maxTaps);
    std::string saved = hal_.store->loadText(kTuneKey);
    if (!saved.empty()) params_.fromString(saved);
    applyParams();
    char u[8], ip[16];
    formatUnit(unit_, u);
    formatIp(unit_, ip);
    logf("unit %s, ip %s, aec %s, %s tuning: %s", u, ip, aec_.name(), saved.empty() ? "default" : "saved",
         params_.toString().c_str());
}

void Intercom::applyParams() {
    if (params_.taps > opt_.maxTaps) params_.taps = opt_.maxTaps;
    aec_.configure(params_);
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
        snprintf(buf, sizeof(buf), "erle_db=%.1f far=%d double_talk=%d converged=%d nlp_gain=%.2f link=%s mic=%u sent=%u spk=%u talkers=%d",
                 double(s.erleDb), s.farActive, s.doubleTalk, s.converged, double(s.nlpGain), link[int(link_)], micLevel_,
                 sentLevel_, spkLevel_, playing_);
        return buf;
    }
    if (c == "unit") {
        if (a.size() > 1) {
            uint16_t room;
            uint8_t sub;
            if (!parseTarget(a[1], &room, &sub) || room == kTargetAllRooms) return "usage: unit 214 | unit 214a";
            UnitId id;
            id.room = room;
            id.sub = sub == kTargetAnySub ? 0 : sub;
            setUnit(id);
        }
        char u[8], ip[16];
        formatUnit(unit_, u);
        formatIp(unit_, ip);
        snprintf(buf, sizeof(buf), "unit %s ip %s", u, ip);
        return buf;
    }
    if (c == "version") return std::string("firmware ") + kFirmwareVersion;
    if (c == "talk") {
        if (src != CmdSource::Local) return "talk only works from the USB console";
        if (a.size() > 1 && a[1] == "off") {
            talkFrames_ = 0;
            return "talk off";
        }
        float secs = a.size() > 1 ? std::strtof(a[1].c_str(), nullptr) : 5.0f;
        secs = std::max(0.0f, std::min(secs, 60.0f));
        talkFrames_ = uint32_t(secs * 1000 / kFrameMs);
        snprintf(buf, sizeof(buf), "talking for %.0f s (\"talk off\" stops)", double(secs));
        return buf;
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
    bool eth = hal_.links->ethernetUp();
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
    int16_t pcm[kFrameSamples];
    size_t n;
    while ((n = hal_.net->recv(buf, sizeof(buf))) > 0) {
        if (link_ == LinkType::None) continue;
        bool ok;
        PacketType type = peekType(buf, n, &ok);
        if (!ok) continue;
        if (type == PacketType::Audio) {
            if (cal_.measuring() || testPhase_) continue;  // the room must stay quiet while measuring
            AudioHeader h;
            if (!decodeAudio(buf, n, &h, pcm)) continue;
            if (h.from == unit_) continue;                                               // our own multicast echo
            if (h.from.room == unit_.room && !(h.flags & kAudioFromConsole)) continue;   // same room group: heard acoustically
            if (h.target != 0 && h.target != unit_.room) continue;                       // a call for another room
            if (Talker* t = talkerFor(h.from)) {
                t->jb.push(h.seq, pcm, h.level);
                t->lastFrame = frame_;
            }
        } else if (type == PacketType::Status) {
            StatusPacket s;
            if (!decodeStatus(buf, n, &s) || s.from == unit_) continue;
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
    PowerState pw = hal_.power->read();
    StatusPacket s;
    s.from = unit_;
    s.link = link_;
    s.power = pw.source;
    s.battery = pw.hasBattery ? pw.batteryPct : 0;
    s.flags = transmitting_ ? kStatusTalking : 0;
    s.uptimeS = frame_ * kFrameMs / 1000;
    uint8_t buf[32];
    hal_.net->send(buf, encodeStatus(s, buf));
}

void Intercom::updatePanel() {
    char text[3];
    uint8_t dots = 0;
    if (setup_.active()) {
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
        if (transmitting_) dots |= 0x01;
        if (playing_) dots |= 0x02;
        if (link_ == LinkType::None && (frame_ / 25) % 2) text[0] = text[1] = text[2] = '-';  // no network
    }
    if (std::memcmp(text, shown_, 3) != 0 || dots != shownDots_) {  // the display sits behind I2C: write only on change
        std::memcpy(shown_, text, 3);
        shownDots_ = dots;
        hal_.panel->show(text, dots);
    }
}

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

    // Auto-tune from the panel: MODE + UP held 3 s, or UP alone held 5 s. (The board's TM1637
    // key scan reports one key at a time, so on hardware only the UP-alone hold works.)
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

    // Panel: room-number setup and TALK. (MODE alone held 3 s = setup; with UP = auto-tune.)
    UnitId id = unit_;
    uint8_t setupHeld = comboSince_ ? uint8_t(held & ~(1u << kBtnMode)) : held;
    if (setup_.update(setupHeld, frame_, &id) && id != unit_) setUnit(id);
    if (talkFrames_) talkFrames_--;
    bool talk = ((held & talkBit) || talkFrames_) && !setup_.active();
    if (talk != transmitting_) {
        transmitting_ = talk;
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
        h.seq = seq_++;
        h.level = sentLevel_ = frameLevel(clean, kFrameSamples);
        uint8_t buf[kMaxPacketBytes];
        hal_.net->send(buf, encodeAudio(h, clean, buf));
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
    spkLevel_ = frameLevel(speaker, kFrameSamples);
    aec_.playback(speaker);

    if (frame_ % kStatusEveryFrames == 0) sendStatus();
    updatePanel();
}

}  // namespace ic
