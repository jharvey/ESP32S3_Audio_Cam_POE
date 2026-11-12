#include "Intercom.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ic {

namespace {
constexpr int kEthStableFrames = 10;   // Ethernet must be up 200 ms before traffic moves to it
constexpr int kTalkerIdleFrames = 50;  // forget a talker 1 s after its last packet
constexpr size_t kMaxTalkers = 16;
}  // namespace

Intercom::Intercom(const Hal& hal, EchoCanceller& aec) : hal_(hal), aec_(aec) {}

void Intercom::logf(const char* fmt, ...) {
    if (!hal_.log) return;
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    hal_.log->log(buf);
}

void Intercom::begin() {
    unit_ = hal_.store->loadUnit();
    char u[8], ip[16];
    formatUnit(unit_, u);
    formatIp(unit_, ip);
    logf("unit %s, ip %s, aec %s", u, ip, aec_.name());
}

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
            AudioHeader h;
            if (!decodeAudio(buf, n, &h, pcm)) continue;
            if (h.from == unit_) continue;                         // our own multicast echo
            if (h.from.room == unit_.room && !(h.flags & kAudioFromConsole)) continue;  // same room group: heard acoustically
            if (h.target != 0 && h.target != unit_.room) continue; // a call for another room
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

void Intercom::processFrame(const int16_t* mic, int16_t* speaker) {
    frame_++;
    int16_t clean[kFrameSamples];
    aec_.capture(mic, clean);
    micLevel_ = frameLevel(mic, kFrameSamples);

    // Panel: room-number setup and TALK.
    uint8_t held = hal_.panel->buttons();
    UnitId id = unit_;
    if (setup_.update(held, frame_, &id) && id != unit_) {
        unit_ = id;
        hal_.store->saveUnit(unit_);
        char u[8], ip[16];
        formatUnit(unit_, u);
        formatIp(unit_, ip);
        logf("room set to %s, ip %s", u, ip);
        sendStatus();
    }
    bool talk = (held & (1u << kBtnTalk)) && !setup_.active();
    if (talk != transmitting_) {
        transmitting_ = talk;
        sendStatus();
    }

    updateLink();

    if (transmitting_ && link_ != LinkType::None) {
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
            bool found = false;
            for (auto& h : heard_)
                if (h.first == t.id.key()) { h.second++; found = true; }
            if (!found) heard_.push_back({t.id.key(), 1});
        }
        if (!t.jb.playing() && frame_ - t.lastFrame > uint32_t(kTalkerIdleFrames)) talkers_.erase(talkers_.begin() + long(i));
        else i++;
    }
    playing_ = n;
    float gain = opt_.volume;
    if (transmitting_ && opt_.duckWhileTalking) gain *= std::pow(10.0f, opt_.duckDb / 20.0f);
    if (n) mixer_.mix(in, n, gain, speaker);
    else std::memset(speaker, 0, sizeof(int16_t) * kFrameSamples);
    spkLevel_ = frameLevel(speaker, kFrameSamples);
    aec_.playback(speaker);

    if (frame_ % kStatusEveryFrames == 0) sendStatus();
    updatePanel();
}

}  // namespace ic
