// Headless checks of the intercom core: packets, jitter buffer, mixer, echo canceller
// (against simulated classroom and gym echo), and several units talking over an
// in-memory network, including Ethernet -> Wi-Fi failover.
//   ic-selftest            run all, exit code 0 = pass
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Calibrator.h"
#include "Auth.h"
#include "Intercom.h"
#include "WebUi.h"
#include "NlmsAec.h"
#include "Panel.h"
#include "Tm1637.h"
#include "Packet.h"
#include "BoardGeometry.h"
#include "RoomEcho.h"
#include "TestVoice.h"

using namespace ic;

static int g_fail = 0;
#define CHECK(cond, ...)                                     \
    do {                                                     \
        bool ok_ = (cond);                                   \
        printf("  [%s] ", ok_ ? "ok" : "FAIL");              \
        printf(__VA_ARGS__);                                 \
        printf("\n");                                        \
        if (!ok_) g_fail++;                                  \
    } while (0)

static double powOf(const int16_t* p, int n) {
    double e = 0;
    for (int i = 0; i < n; i++) e += double(p[i]) * p[i];
    return e / n;
}
static double powOf(const float* p, int n) {
    double e = 0;
    for (int i = 0; i < n; i++) e += double(p[i]) * p[i];
    return e / n;
}
static double db(double r) { return 10.0 * std::log10(r > 1e-12 ? r : 1e-12); }

// ---------------------------------------------------------------------------------
static void testPackets() {
    printf("packets\n");
    char ip[16], u[8];
    formatIp(UnitId{214, 0}, ip);
    CHECK(!strcmp(ip, "10.42.2.14"), "room 214 -> %s (10.42.<floor>.<room>)", ip);
    formatIp(UnitId{299, 0}, ip);
    formatUnit(UnitId{299, 0}, u);
    CHECK(!strcmp(ip, "10.42.2.99") && !strcmp(u, "299"), "room %s -> %s", u, ip);
    formatIp(UnitId{300, 0}, ip);
    CHECK(!strcmp(ip, "10.42.3.100"), "room 300 -> %s (no .0 host)", ip);

    int16_t pcm[kFrameSamples], back[kFrameSamples];
    for (int i = 0; i < kFrameSamples; i++) pcm[i] = int16_t(i * 97 - 15000);
    AudioHeader h, h2;
    h.from = {123, 2};
    h.seq = 65535;
    h.target = 301;
    h.level = 55;
    uint8_t buf[kMaxPacketBytes];
    size_t n = encodeAudio(h, pcm, buf);
    bool ok = decodeAudio(buf, n, &h2, back);
    CHECK(ok && n == kMaxPacketBytes && h2.from == h.from && h2.seq == 65535 && h2.target == 301 &&
              !memcmp(pcm, back, sizeof(pcm)),
          "audio packet round trip (%zu bytes)", n);
    CHECK(!decodeAudio(buf, n - 1, &h2, back), "truncated packet rejected");

    // Opus (R1.6): a 440 Hz tone survives the round trip at about the same level, in about 1/10 the bytes.
    CHECK(codecAvailable(Codec::Opus), "Opus is built in");
    auto enc = makeEncoder(Codec::Opus);
    auto dec = makeDecoder(Codec::Opus);
    double inE = 0, outE = 0;
    size_t bytes = 0;
    int frames = 0;
    for (int f = 0; f < 50; f++) {
        int16_t tone[kFrameSamples], dout[kFrameSamples];
        for (int i = 0; i < kFrameSamples; i++) tone[i] = int16_t(8000 * std::sin(2 * 3.14159265 * 440 * (f * kFrameSamples + i) / kSampleRate));
        uint8_t coded[kMaxCodedBytes];
        size_t m = enc->encode(tone, coded, sizeof(coded));
        AudioHeader oh;
        oh.codec = Codec::Opus;
        oh.seq = uint16_t(f);
        uint8_t pkt[kMaxPacketBytes];
        size_t pn = encodeAudioData(oh, coded, m, pkt);
        AudioHeader rh;
        const uint8_t* pl;
        size_t plen;
        bool okp = decodeAudioData(pkt, pn, &rh, &pl, &plen) && rh.codec == Codec::Opus && plen == m;
        if (!okp || !dec->decode(pl, plen, dout)) continue;
        bytes += m;
        frames++;
        if (f >= 10)
            for (int i = 0; i < kFrameSamples; i++) { inE += double(tone[i]) * tone[i]; outE += double(dout[i]) * dout[i]; }
    }
    double lvlDb = 10 * std::log10((outE + 1) / (inE + 1));
    double kbps = frames ? bytes * 8.0 / (frames * kFrameMs) : 0;
    CHECK(frames == 50 && std::fabs(lvlDb) < 2.0 && kbps < 40, "Opus round trip: %d frames, level %+.1f dB, %.1f kbit/s (PCM 256)", frames, lvlDb, kbps);
}

// ---------------------------------------------------------------------------------
static void testJitter() {
    printf("jitter buffer\n");
    int16_t f[kFrameSamples], out[kFrameSamples];
    uint8_t lvl;
    auto fill = [&](int v) { for (auto& s : f) s = int16_t(v); };

    JitterBuffer jb;
    // Arrive 0,2,1,3,4 (reordered); expect playout 0,1,2,3,4 after the prebuffer.
    int order[] = {0, 2, 1, 3, 4};
    std::vector<int> played;
    for (int k = 0; k < 5; k++) {
        fill(1000 + order[k]);
        jb.push(uint16_t(65534 + order[k]), f, 50);  // wraps through 65535 -> 0
        if (jb.pop(out, &lvl)) played.push_back(out[0] - 1000);
    }
    for (int k = 0; k < 8; k++)
        if (jb.pop(out, &lvl)) played.push_back(out[0] - 1000);
    bool inOrder = played.size() >= 5;
    for (size_t i = 0; i < 5 && i < played.size(); i++) inOrder = inOrder && played[i] == int(i);
    CHECK(inOrder, "reordered frames played in order across seq wrap (%zu frames out)", played.size());
    CHECK(!jb.playing(), "stops after the talker goes quiet (%zu frames incl. fade)", played.size());

    // One lost frame is concealed, not a gap.
    JitterBuffer jb2;
    int got = 0;
    for (int k = 0; k < 20; k++) {
        fill(8000);
        if (k != 10) jb2.push(uint16_t(k), f, 60);
        if (jb2.pop(out, &lvl)) got++;
    }
    CHECK(jb2.concealed() == 1 && got == 20 - JitterBuffer::kTargetDepth + 1, "single loss concealed (%u), %d frames out",
          jb2.concealed(), got);
}

// ---------------------------------------------------------------------------------
static void testMixer() {
    printf("mixer\n");
    static int16_t a[6][kFrameSamples];
    MixInput in[6];
    for (int t = 0; t < 6; t++) {
        for (int i = 0; i < kFrameSamples; i++) a[t][i] = int16_t(20000 * std::sin(0.05 * i * (t + 1)));
        in[t] = {a[t], uint8_t(40 + t)};
    }
    Mixer m;
    int16_t out[kFrameSamples];
    int peak = 0, used = 0;
    for (int k = 0; k < 10; k++) {
        used = m.mix(in, 6, 1.0f, out);
        for (int i = 0; i < kFrameSamples; i++) peak = std::max(peak, std::abs(int(out[i])));
    }
    CHECK(used == kMaxMixTalkers, "6 talkers -> loudest %d mixed", used);
    CHECK(peak <= 32767 && m.limiterGain() < 0.5f, "limiter holds peak %d (gain %.2f)", peak, m.limiterGain());
}

// ---------------------------------------------------------------------------------
struct AecResult {
    double erleDb, erleNlpDb, nearKeptDb;
};

// Far end talks for 12 s into a room; near end also talks from 8 s to 10 s.
static AecResult runAec(sim::RoomPreset preset, int taps, const sim::board::MicPath* micPath = nullptr, double* residDbfs = nullptr) {
    sim::RoomEcho room;
    if (micPath) room.setMicPath(micPath->delayMs, micPath->relDb);
    room.setPreset(preset);
    sim::TestVoice far(120.0f, 7, -16.0f), nearV(210.0f, 99, -26.0f);
    NlmsAec aec(taps, 0);
    aec.setNlp(false);
    NlmsAec aecNlp(taps, 0);

    int16_t spk[kFrameSamples] = {}, mic[kFrameSamples], out[kFrameSamples], outNlp[kFrameSamples], nearF[kFrameSamples];
    float echo[kFrameSamples];
    double micP = 0, outP = 0, outNlpP = 0, nearP = 0, dtOutP = 0, dtEchoResP = 0;
    const int frames = 12 * 50;
    for (int k = 0; k < frames; k++) {
        room.process(spk, echo, kFrameSamples);  // echo of the previous speaker frame
        bool dt = k >= 400 && k < 500;
        if (dt) nearV.render(nearF, kFrameSamples);
        else memset(nearF, 0, sizeof(nearF));
        for (int i = 0; i < kFrameSamples; i++) {
            float v = echo[i] + nearF[i] + 3.0f * (float((i * 7919 + k * 104729) % 2001) / 1000.0f - 1.0f);
            mic[i] = int16_t(std::max(-32767.0f, std::min(32767.0f, v)));
        }
        aec.capture(mic, out);
        aecNlp.capture(mic, outNlp);
        far.render(spk, kFrameSamples);
        aec.playback(spk);
        aecNlp.playback(spk);
        if (k >= 250 && k < 400) {  // converged, far end only
            micP += powOf(mic, kFrameSamples);
            outP += powOf(out, kFrameSamples);
            outNlpP += powOf(outNlp, kFrameSamples);
        }
        if (dt && k >= 410) {
            nearP += powOf(nearF, kFrameSamples);
            dtOutP += powOf(out, kFrameSamples);
            float res[kFrameSamples];
            for (int i = 0; i < kFrameSamples; i++) res[i] = float(out[i]) - float(nearF[i]);
            dtEchoResP += powOf(res, kFrameSamples);
        }
    }
    (void)dtEchoResP;
    if (residDbfs) *residDbfs = db(outNlpP / 150.0 / kFrameSamples / (32768.0 * 32768.0));
    return {db(micP / outP), db(micP / outNlpP), db(dtOutP / nearP)};
}

static void testAec() {
    printf("echo canceller (NLMS, simulated rooms)\n");
    struct Case { sim::RoomPreset p; int taps; double minErle; };
    Case cases[] = {
        {sim::RoomPreset::Classroom, 2048, 15.0},
        {sim::RoomPreset::Gym, 2048, 6.0},
    };
    for (const Case& c : cases) {
        AecResult r = runAec(c.p, c.taps);
        CHECK(r.erleDb >= c.minErle, "%-24s %d taps: ERLE %.1f dB (+NLP %.1f dB), want >= %.0f", sim::presetName(c.p), c.taps,
              r.erleDb, r.erleNlpDb, c.minErle);
        CHECK(r.nearKeptDb > -3.0 && r.nearKeptDb < 3.0, "%-24s double talk: near-end level kept %+.1f dB",
              sim::presetName(c.p), r.nearKeptDb);
    }
    // Each mic's own direct path from the board layout (firmware/tools/echo_predict.py -> BoardGeometry.h).
    printf("echo from the board layout (speaker at %.1f, %.1f mm)\n", double(sim::board::kSpeakerX), double(sim::board::kSpeakerY));
    double resid[2] = {0, 0};
    for (int m = 0; m < 2; m++) {
        const sim::board::MicPath& mp = sim::board::kMics[m];
        AecResult r = runAec(sim::RoomPreset::Classroom, 2048, &mp, &resid[m]);
        CHECK(r.erleDb >= 10.0, "%s: %.0f mm from the speaker (%+.1f dB): ERLE %.1f dB, residual echo %.1f dBFS", mp.name,
              double(mp.distMm), double(mp.relDb), r.erleDb, resid[m]);
    }
    printf("      in a classroom the reverb dominates: %s leaves %.1f dBFS, %s %.1f dBFS of echo\n", sim::board::kMics[0].name, resid[0],
           sim::board::kMics[1].name, resid[1]);
}

// ---------------------------------------------------------------------------------
// In-memory network: every send() is delivered to every unit (like multicast with loopback).
struct Bus {
    std::vector<std::deque<std::vector<uint8_t>>*> queues;
};

static int64_t g_clock0 = 0;  // test wall clock: UTC at frame 0 (0 = no clock)
static bool g_bootTest = false;  // power-up light test in new FakeUnits (off: it would light lamps in other tests)

struct FakeUnit : INet, ILinks, IPower, IPanel, IStore, ILog, ILamps, IDiag {
    uint8_t outFaults = 0;
    std::string boardTest() override { return "ok fake board\n"; }
    uint8_t outputFaults() override { return outFaults; }
    Bus* bus;
    std::deque<std::vector<uint8_t>> rx;
    UnitId id;
    bool eth = true, wifi = false;
    LinkType used = LinkType::None;
    uint8_t held = 0;
    std::vector<std::string> logs;
    NlmsAec aec{512, 0};
    std::unique_ptr<Intercom> ic;
    sim::TestVoice voice;
    PowerState pw;

    FakeUnit(Bus* b, UnitId u, float f0, uint32_t uid = 0) : bus(b), id(u), voice(f0, u.room) {
        bus->queues.push_back(&rx);
        IntercomOptions opt;
        opt.unitUid = uid ? uid : 0xA0000000u + u.room;  // stands in for the board's MAC
        opt.utcNow = [this] { return g_clock0 ? g_clock0 + int64_t(ic->frame()) / 50 : int64_t(0); };
        if (!g_bootTest) kv["boottest"] = "off";
        ic.reset(new Intercom(Hal{this, this, this, this, this, this, this, this}, aec, opt));
        ic->begin();
    }
    void send(const uint8_t* d, size_t n) override {
        if (used == LinkType::None) return;
        for (auto* q : bus->queues) q->push_back(std::vector<uint8_t>(d, d + n));
    }
    size_t recv(uint8_t* buf, size_t cap) override {
        if (rx.empty()) return 0;
        auto p = rx.front();
        rx.pop_front();
        size_t n = std::min(cap, p.size());
        memcpy(buf, p.data(), n);
        return n;
    }
    bool ethernetUp() override { return eth; }
    bool wifiUp() override { return wifi; }
    void useLink(LinkType l) override { used = l; }
    PowerState read() override { return pw; }
    uint8_t buttons() override { return held; }
    std::vector<std::string> shown;  // every display change, 3 chars + dots byte
    uint32_t rgbSeen = 0;
    uint8_t lampSeen = 0;            // bit n: lamp n was switched on
    void show(const char* t, uint8_t d) override { shown.push_back(std::string(t, 3) + char(d)); }
    void set(Lamp l, uint8_t pct) override { if (pct) lampSeen |= uint8_t(1u << int(l)); }
    void setRgb(uint8_t r, uint8_t g, uint8_t b) override { rgbSeen |= uint32_t(r) << 16 | uint32_t(g) << 8 | b; }
    UnitId loadUnit() override { return id; }
    void saveUnit(const UnitId& u) override { id = u; }
    void log(const char* m) override { logs.push_back(m); }
    std::map<std::string, std::string> kv;
    std::string loadText(const char* k) override { return kv[k]; }
    void saveText(const char* k, const std::string& v) override { kv[k] = v; }

    void step() {
        int16_t mic[kFrameSamples], spk[kFrameSamples];
        if (held & 1) voice.render(mic, kFrameSamples);
        else memset(mic, 0, sizeof(mic));
        ic->processFrame(mic, spk);
    }
    uint32_t heardFrom(const UnitId& u) const {
        for (auto& h : ic->heard())
            if (h.first == u.key()) return h.second;
        return 0;
    }
};

static void testIntercom() {
    printf("intercom (4 units, in-memory network)\n");
    Bus bus;
    FakeUnit r101(&bus, {101, 0}, 120), r214(&bus, {214, 0}, 160), gymA(&bus, {299, 0}, 190), gymB(&bus, {298, 0}, 230);
    std::vector<FakeUnit*> all = {&r101, &r214, &gymA, &gymB};
    auto run = [&](int frames) { for (int k = 0; k < frames; k++) for (auto* u : all) u->step(); };

    run(20);  // Ethernet settles
    CHECK(r101.used == LinkType::Ethernet, "units come up on Ethernet");
    CHECK(!strcmp(r101.ic->fault(), ""), "no fault code on PoE with Ethernet (got \"%s\")", r101.ic->fault());
    gymA.ic->command("group 299");
    gymB.ic->command("group 299");
    r101.ic->command("set opus 0");  // one unit on raw PCM, the rest on Opus: everyone must still hear everyone
    CHECK(r101.ic->sendCodec() == Codec::Pcm16 && r214.ic->sendCodec() == Codec::Opus, "101 sends PCM, the others Opus");
    run(5);
    CHECK(gymB.ic->group() == 299 && gymB.kv["group"] == "299", "gym unit 298 joined room group 299 (saved)");
    // 101 and gym A talk at the same time for 2 s.
    r101.held = gymA.held = 1;
    run(100);
    CHECK(gymB.ic->groupTalking() && gymB.ic->speakerLevel() == 0 && r214.ic->speakerLevel() > 0,
          "298 goes quiet while 299 (same room) talks, so 299's mic doesn't pick up 101 from it (298 %u, 214 %u)",
          unsigned(gymB.ic->speakerLevel()), unsigned(r214.ic->speakerLevel()));
    r101.held = gymA.held = 0;
    run(20);
    CHECK(r214.heardFrom(r101.id) > 90 && r214.heardFrom(gymA.id) > 90, "214 heard 101 (%u frames) and 299 (%u) at once",
          r214.heardFrom(r101.id), r214.heardFrom(gymA.id));
    CHECK(gymB.heardFrom(r101.id) > 90 && gymB.heardFrom(gymA.id) == 0,
          "298 heard 101, not 299 in its room group (%u)", gymB.heardFrom(gymA.id));
    CHECK(r101.heardFrom(r101.id) == 0, "nobody plays their own voice");
    CHECK(r214.ic->peers().size() == 3, "214 sees %zu peers via status", r214.ic->peers().size());

    // Portable unit 214 is unplugged: Wi-Fi was already associated.
    r214.wifi = true;
    run(5);
    r214.eth = false;
    run(1);
    CHECK(r214.used == LinkType::WiFi, "214 unplugged -> Wi-Fi within 1 frame (20 ms)");
    CHECK(!strcmp(r214.ic->fault(), "E1 "), "PoE power but no Ethernet link -> E1 (got \"%s\")", r214.ic->fault());
    r101.held = 1;
    run(50);
    r101.held = 0;
    run(10);
    CHECK(r214.heardFrom(r101.id) > 130, "214 still hears 101 over Wi-Fi (%u frames total)", r214.heardFrom(r101.id));
    r214.eth = true;
    run(15);
    CHECK(r214.used == LinkType::Ethernet, "214 plugged back in -> Ethernet after 200 ms debounce");
    for (auto& l : r214.logs) printf("      214 log: %s\n", l.c_str());

    // Faults: no network at all -> E2; on battery below 15 % -> Lo.
    r214.eth = r214.wifi = false;
    run(2);
    CHECK(!strcmp(r214.ic->fault(), "E2 "), "no network at all -> E2 (got \"%s\")", r214.ic->fault());
    r214.eth = true;
    r214.pw.source = PowerSource::Battery;
    r214.pw.hasBattery = true;
    r214.pw.batteryPct = 10;
    run(60);
    CHECK(!strcmp(r214.ic->fault(), "Lo "), "on battery at 10 %% -> Lo (got \"%s\")", r214.ic->fault());
    r214.pw = PowerState();
    run(60);

    // A shorted output (lamp cable): oL on the display, the office sees it, gone once cleared.
    r214.outFaults = 0x04;
    run(60);
    {
        const StatusPacket* p = nullptr;
        for (const Peer& q : r101.ic->peers())
            if (q.status.from.room == 214) p = &q.status;
        bool logged = false;
        for (auto& l : r214.logs) logged |= l.find("OUTPUT OVERLOAD: call light") != std::string::npos;
        CHECK(!strcmp(r214.ic->fault(), "oL ") && p && p->fault == Fault::Overload && logged,
              "shorted output -> oL, logged, and the office sees it (got \"%s\")", r214.ic->fault());
    }
    r214.outFaults = 0;
    run(60);
    CHECK(!strcmp(r214.ic->fault(), ""), "fault cleared -> no code");

    // A second unit set to room 214 (a different board): both show dUP until one is renumbered.
    FakeUnit dup(&bus, {214, 0}, 200, 0xB0000214u);
    all.push_back(&dup);
    run(80);
    CHECK(r214.ic->duplicate() && dup.ic->duplicate() && !strcmp(r214.ic->fault(), "dUP"), "two units on room 214 both show dUP");
    CHECK(!r101.ic->duplicate(), "other units are not flagged");
    auto peerOf = [](FakeUnit& at, uint16_t room) -> const StatusPacket* {
        for (const Peer& p : at.ic->peers())
            if (p.status.from.room == room) return &p.status;
        return nullptr;
    };
    const StatusPacket* p214 = peerOf(r101, 214);
    CHECK(p214 && p214->fault == Fault::Duplicate, "101 (and the office) see 214's dUP in its status");
    dup.ic->command("unit 215");
    run(200);
    CHECK(!r214.ic->duplicate() && !dup.ic->duplicate(), "dUP clears 3 s after 215 is renumbered");
    CHECK(dup.ic->command("unit 214a").find("usage") == 0, "letters are refused (\"unit 214a\")");
    all.pop_back();

    // Office mute: 101's TALK is ignored while muted; everyone sees the muted flag.
    uint32_t before = r214.heardFrom(r101.id);
    CHECK(r101.ic->command("mute 1", CmdSource::Network).find("TALK muted") == 0, "\"mute 1\" accepted from the network");
    r101.held = 1;
    run(60);
    r101.held = 0;
    run(10);
    const StatusPacket* p101 = peerOf(r214, 101);
    CHECK(r214.heardFrom(r101.id) == before && p101 && (p101->flags & kStatusMuted), "muted 101 isn't heard; 214 sees it muted");
    r101.ic->command("mute off", CmdSource::Network);
    run(60);
    p101 = peerOf(r214, 101);
    CHECK(p101 && !(p101->flags & kStatusMuted), "\"mute off\" clears it");

    // TALK held over 2 minutes: flagged (stuck button), cleared on release.
    r101.held = 1;
    run(6010);
    p101 = peerOf(r214, 101);
    CHECK(r101.ic->talkHeld() && p101 && (p101->flags & kStatusTalkHeld), "TALK held 2 min -> flagged to everyone");
    r101.held = 0;
    run(60);
    p101 = peerOf(r214, 101);
    CHECK(!r101.ic->talkHeld() && p101 && !(p101->flags & kStatusTalkHeld), "released -> flag cleared");

    // Room-number setup on the panel: hold MODE 3 s, set 3-0-5, MODE on the last digit saves.
    FakeUnit& u = r101;
    auto press = [&](uint8_t bit, int frames) { u.held = bit; run(frames); u.held = 0; run(3); };
    press(1 << kBtnMode, 155);
    CHECK(!u.ic->inSetup(), "MODE held 3 s on the front does not start setup");
    press(1 << kBtnSetup, 155);
    CHECK(u.ic->inSetup(), "hidden SETUP held 3 s enters setup");
    press(1 << kBtnUp, 2); press(1 << kBtnUp, 2);  // 1 -> 3
    press(1 << kBtnMode, 2);
    press(1 << kBtnMode, 2);  // 0 stays... next digit
    for (int i = 0; i < 4; i++) press(1 << kBtnUp, 2);  // 1 -> 5
    press(1 << kBtnMode, 2);  // MODE on the last digit saves
    char s[8];
    formatUnit(u.ic->unit(), s);
    CHECK(!u.ic->inSetup() && !strcmp(s, "305"), "panel setup saved unit %s", s);
}

// ---------------------------------------------------------------------------------
// Cross-room echo (R1.4): 101 holds TALK and speaks in 400 ms bursts; 102 next door plays it, and 102's
// speaker leaks back into 101's mic (`leak`, through the wall or an open door) 15 ms of air later. The same
// run with no leak gives the clean sent stream, so (with leak - without) is exactly the echo 101 sends out.
static std::vector<int16_t> crossRoomSent(float leak, int frames) {
    Bus bus;
    std::deque<std::vector<uint8_t>> tap;  // a listener on the network: 101's packets
    FakeUnit a(&bus, {101, 0}, 140), b(&bus, {102, 0}, 180);
    bus.queues.push_back(&tap);
    a.ic->command("set opus 0");  // exact samples, so the two runs can be subtracted
    const int air = 15 * kSampleRate / 1000;
    std::vector<int16_t> bHist(size_t(kFrameSamples * 20), 0);
    size_t bPos = 0;
    std::vector<int16_t> sent(size_t(frames) * kFrameSamples, 0);
    for (int k = 0; k < frames; k++) {
        bool burst = (k / 20) % 2 == 0;
        int16_t mic[kFrameSamples], spkA[kFrameSamples], micB[kFrameSamples], spkB[kFrameSamples];
        if (burst) a.voice.render(mic, kFrameSamples);
        else memset(mic, 0, sizeof(mic));
        for (int i = 0; i < kFrameSamples; i++) {
            size_t idx = (bPos + bHist.size() - size_t(air) + size_t(i)) % bHist.size();
            int v = mic[i] + int(leak * bHist[idx]);
            mic[i] = int16_t(std::max(-32767, std::min(32767, v)));
        }
        a.held = 1;
        a.ic->processFrame(mic, spkA);
        memset(micB, 0, sizeof(micB));
        b.ic->processFrame(micB, spkB);
        for (int i = 0; i < kFrameSamples; i++) bHist[(bPos + size_t(i)) % bHist.size()] = spkB[i];
        bPos = (bPos + kFrameSamples) % bHist.size();
        while (!tap.empty()) {
            AudioHeader h;
            int16_t pcm[kFrameSamples];
            if (decodeAudio(tap.front().data(), tap.front().size(), &h, pcm) && h.from.room == 101 && h.seq < frames)
                memcpy(&sent[size_t(h.seq) * kFrameSamples], pcm, sizeof(pcm));
            tap.pop_front();
        }
    }
    return sent;
}

struct CrossEcho { double echoToVoiceDb, lagMs; };
static CrossEcho crossRoomEcho(float leak) {
    const int frames = 1200, skip = 400 * kFrameSamples;  // skip the first 8 s (settling)
    std::vector<int16_t> dry = crossRoomSent(0.0f, frames), wet = crossRoomSent(leak, frames);
    double ev = 0, ee = 0;
    for (size_t i = size_t(skip); i < dry.size(); i++) {
        double e = double(wet[i]) - double(dry[i]);
        ev += double(dry[i]) * dry[i];
        ee += e * e;
    }
    double best = 0;
    int bestLag = 0;
    for (int lag = 0; lag < 3200; lag += 16) {  // where the echo sits: correlate the echo with the dry voice
        double c = 0;
        for (size_t i = size_t(skip); i + size_t(lag) < dry.size(); i += 4) c += (double(wet[i + size_t(lag)]) - dry[i + size_t(lag)]) * dry[i];
        if (std::fabs(c) > best) { best = std::fabs(c); bestLag = lag; }
    }
    return {10 * std::log10((ee + 1) / (ev + 1)), bestLag * 1000.0 / kSampleRate};
}

static void testCrossRoom() {
    printf("cross-room echo (own voice coming back from the next room's speaker)\n");
    CrossEcho wall = crossRoomEcho(0.1f), door = crossRoomEcho(0.316f);
    CHECK(wall.echoToVoiceDb < -15.0, "through a wall (-20 dB): own voice sent back %.1f dB below the voice, %.0f ms late",
          wall.echoToVoiceDb, wall.lagMs);
    CHECK(door.echoToVoiceDb < -5.0, "open door (-10 dB): %.1f dB below the voice, %.0f ms late", door.echoToVoiceDb, door.lagMs);
}

// ---------------------------------------------------------------------------------
static void testTuning() {
    printf("tuning parameters\n");
    TuneParams p;
    CHECK(p.set("mu", "0.05") && p.set("taps", "99999") && p.set("duck", "1") && !p.set("nope", "1") && !p.set("mu", "x"),
          "set with clamping, unknown keys refused");
    std::string v;
    p.get("taps", &v);
    CHECK(v == "4096", "taps clamped to %s", v.c_str());
    TuneParams q;
    q.fromString(p.toString());
    CHECK(q.toString() == p.toString(), "save/load text round trip: %s", q.toString().c_str());
}

// Runs auto-tune against a simulated room. hwDelayMs = converter/DMA latency on the echo path.
static CalResult runCal(sim::RoomPreset preset, int hwDelayMs, float noiseDbfs, bool deadSpeaker, TuneParams* tuned) {
    sim::RoomEcho room;
    room.setPreset(preset);
    std::vector<int16_t> line(size_t(hwDelayMs) * kSampleRate / 1000 + 1, 0);
    size_t lp = 0;
    Calibrator cal;
    cal.start(TuneParams(), 4096);
    int16_t prev[kFrameSamples] = {}, mic[kFrameSamples], spk[kFrameSamples];
    float echo[kFrameSamples];
    uint32_t rng = 7;
    float na = 32768.0f * std::pow(10.0f, noiseDbfs / 20.0f) * 1.732f;
    for (int k = 0; k < 400 && cal.active(); k++) {
        int16_t d[kFrameSamples];
        for (int i = 0; i < kFrameSamples; i++) {
            d[i] = line[lp];
            line[lp] = deadSpeaker ? 0 : prev[i];
            lp = (lp + 1) % line.size();
        }
        room.process(d, echo, kFrameSamples);
        for (int i = 0; i < kFrameSamples; i++) {
            rng = rng * 1664525u + 1013904223u;
            float n = na * (float(rng >> 8) / float(1u << 24) * 2 - 1);
            mic[i] = int16_t(std::max(-32767.0f, std::min(32767.0f, echo[i] + n)));
        }
        cal.frame(mic, spk);
        memcpy(prev, spk, sizeof(prev));
    }
    if (tuned) *tuned = cal.tuned();
    return cal.result();
}

static void testAutoTune() {
    printf("auto-tune (simulated rooms + hardware latency)\n");
    TuneParams t;
    CalResult r = runCal(sim::RoomPreset::Classroom, 12, -75, false, &t);
    printf("      %s\n", r.summary().c_str());
    CHECK(r.ok && std::fabs(r.delayMs - 12.0f) <= 1.0f, "classroom, 12 ms latency: found delay %.1f ms", r.delayMs);
    CHECK(r.ok && !t.duck && r.erleAfterDb >= 12, "classroom: no ducking needed, ERLE %.1f dB", r.erleAfterDb);

    r = runCal(sim::RoomPreset::Gym, 12, -75, false, &t);
    printf("      %s\n", r.summary().c_str());
    CHECK(r.ok && t.duck && r.tailMs > 250, "gym: long tail (%.0f ms, RT60 %.2f s) -> ducking on, nlp %.0f dB", r.tailMs,
          r.rt60S, t.nlpDb);

    r = runCal(sim::RoomPreset::Classroom, 150, -75, false, &t);
    printf("      %s\n", r.summary().c_str());
    CHECK(r.ok && r.erleBeforeDb < 3 && r.erleAfterDb > 12,
          "150 ms latency (beyond the default filter): ERLE %.1f dB before -> %.1f dB after", r.erleBeforeDb,
          r.erleAfterDb);

    r = runCal(sim::RoomPreset::Classroom, 12, -25, false, nullptr);
    CHECK(!r.ok, "noisy room refused: %s", r.message.c_str());
    r = runCal(sim::RoomPreset::Classroom, 12, -75, true, nullptr);
    CHECK(!r.ok, "dead speaker detected: %s", r.message.c_str());
}

static void testConsole() {
    printf("console (local and over the network)\n");
    Bus bus;
    FakeUnit a(&bus, {101, 0}, 120), b(&bus, {214, 0}, 160);
    std::deque<std::vector<uint8_t>> tool;  // an office tool listening on the network
    bus.queues.push_back(&tool);
    for (int k = 0; k < 20; k++) { a.step(); b.step(); }

    std::string rep = a.ic->command("set mu 0.08 nlp_db -20");
    CHECK(rep.rfind("ok", 0) == 0 && std::fabs(a.ic->params().mu - 0.08f) < 1e-6f, "set: %s", rep.c_str());
    CHECK(a.ic->command("set mu banana").find("bad") != std::string::npos, "bad value refused");
    a.ic->command("save");
    CHECK(a.kv["tune"].find("mu=0.08") != std::string::npos, "save writes flash");
    a.ic->command("defaults");
    a.ic->command("load");
    CHECK(std::fabs(a.ic->params().mu - 0.08f) < 1e-6f, "load restores saved values");
    a.ic->command("set mu 0.3");
    a.ic->command("save");
    rep = a.ic->command("undo");
    CHECK(std::fabs(a.ic->params().mu - 0.08f) < 1e-6f, "undo: %s", rep.c_str());

    // Network: a tool (room 0) tells 214 to turn its speaker down, then reads it back.
    const std::string key = "test-network-key-123";
    a.ic->command("netkey " + key);
    b.ic->command("netkey " + key);
    TextPacket t;
    t.from = {0, 0};
    parseTarget("214", &t.targetRoom, &t.targetSub);
    t.text = "set volume_db -6";
    t.text = netauth::sign(key, 1000, t);
    uint8_t buf[kMaxPacketBytes];
    size_t n = encodeText(t, buf);
    for (auto* q : bus.queues) q->push_back(std::vector<uint8_t>(buf, buf + n));
    for (int k = 0; k < 3; k++) { a.step(); b.step(); }
    std::string got;
    for (auto& pkt : tool) {
        TextPacket r;
        if (decodeText(pkt.data(), pkt.size(), &r) && r.type == PacketType::Reply) {
            char u[8];
            formatUnit(r.from, u);
            got += std::string(u) + ": " + r.text + "; ";
        }
    }
    CHECK(std::fabs(b.ic->params().volumeDb + 6) < 1e-6f && std::fabs(a.ic->params().volumeDb) < 1e-6f,
          "network command reached only 214");
    CHECK(got.find("214: ok volume_db=-6") != std::string::npos, "tool got reply: %s", got.c_str());

    // Panel combo MODE+UP 3 s starts auto-tune; TALK cancels it.
    a.held = (1 << kBtnMode) | (1 << kBtnUp);
    for (int k = 0; k < 155; k++) { a.step(); b.step(); }
    a.held = 0;
    bool started = a.ic->calibrating();
    for (int k = 0; k < 5; k++) a.step();
    CHECK(started && !a.ic->inSetup(), "MODE+UP held 3 s starts auto-tune, not room setup");
    a.held = 1;
    a.step();
    a.held = 0;
    CHECK(!a.ic->calibrating(), "TALK cancels auto-tune");
}

static std::string basic(const std::string& userPass) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out = "Basic ";
    size_t i = 0;
    for (; i + 2 < userPass.size(); i += 3) {
        uint32_t v = uint8_t(userPass[i]) << 16 | uint8_t(userPass[i + 1]) << 8 | uint8_t(userPass[i + 2]);
        out += k[v >> 18]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += k[v & 63];
    }
    if (userPass.size() - i == 1) {
        uint32_t v = uint8_t(userPass[i]) << 16;
        out += k[v >> 18]; out += k[(v >> 12) & 63]; out += "==";
    } else if (userPass.size() - i == 2) {
        uint32_t v = uint8_t(userPass[i]) << 16 | uint8_t(userPass[i + 1]) << 8;
        out += k[v >> 18]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += '=';
    }
    return out;
}

// Sends one command packet onto the bus and returns the replies the tool saw.
static std::string netCommand(Bus& bus, std::deque<std::vector<uint8_t>>& tool, std::vector<FakeUnit*> units, const std::string& target,
                              const std::string& text) {
    TextPacket t;
    t.from = {0, 0};
    parseTarget(target, &t.targetRoom, &t.targetSub);
    t.text = text;
    uint8_t buf[kMaxPacketBytes];
    size_t n = encodeText(t, buf);
    tool.clear();
    for (auto* q : bus.queues) q->push_back(std::vector<uint8_t>(buf, buf + n));
    for (int k = 0; k < 3; k++)
        for (auto* u : units) u->step();
    std::string got;
    for (auto& pkt : tool) {
        TextPacket r;
        if (decodeText(pkt.data(), pkt.size(), &r) && r.type == PacketType::Reply) got += r.text + "; ";
    }
    return got;
}

static std::string signedText(const std::string& key, uint64_t nonce, const std::string& target, const std::string& cmd) {
    TextPacket t;
    t.from = {0, 0};
    parseTarget(target, &t.targetRoom, &t.targetSub);
    t.text = cmd;
    return netauth::sign(key, nonce, t);
}

static void testNetAuth() {
    printf("network command key\n");
    CHECK(hmacSha256Hex("key", "The quick brown fox jumps over the lazy dog") ==
              "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8",
          "HMAC-SHA256 test vector");
    Bus bus;
    FakeUnit a(&bus, {214, 0}, 120);
    std::deque<std::vector<uint8_t>> tool;
    bus.queues.push_back(&tool);
    for (int k = 0; k < 20; k++) a.step();
    const std::string key = "school-key-0123456789";

    std::string r = netCommand(bus, tool, {&a}, "214", "set volume_db -9");
    CHECK(r.find("off") != std::string::npos && std::fabs(a.ic->params().volumeDb) < 1e-6f, "no key set: network commands refused (%s)", r.c_str());
    CHECK(a.ic->command("netkey short").find("short") != std::string::npos, "short key refused");
    CHECK(a.ic->command("netkey " + key, CmdSource::Network).find("USB") != std::string::npos, "netkey can't be set over the network");
    a.ic->command("netkey " + key);
    r = netCommand(bus, tool, {&a}, "214", "set volume_db -9");
    CHECK(r.find("not signed") != std::string::npos && std::fabs(a.ic->params().volumeDb) < 1e-6f, "unsigned command refused");
    r = netCommand(bus, tool, {&a}, "214", signedText("wrong-key-0123456789", 5000, "214", "set volume_db -9"));
    CHECK(r.find("wrong network key") != std::string::npos && std::fabs(a.ic->params().volumeDb) < 1e-6f, "wrong key refused");
    std::string good = signedText(key, 5000, "214", "set volume_db -9");
    r = netCommand(bus, tool, {&a}, "214", good);
    CHECK(r.find("ok volume_db=-9") != std::string::npos, "signed command accepted: %s", r.c_str());
    a.ic->command("set volume_db 0");
    r = netCommand(bus, tool, {&a}, "214", good);
    CHECK(r.find("old or repeated") != std::string::npos && std::fabs(a.ic->params().volumeDb) < 1e-6f, "replayed command refused");
    r = netCommand(bus, tool, {&a}, "all", signedText(key, 5001, "214", "set volume_db -9"));
    CHECK(r.find("wrong network key") != std::string::npos, "signature covers the target (re-addressed packet refused)");
    std::string tampered = signedText(key, 5002, "214", "set volume_db -9");
    tampered.replace(tampered.size() - 2, 2, "12");
    r = netCommand(bus, tool, {&a}, "214", tampered);
    CHECK(r.find("wrong network key") != std::string::npos, "changed command text refused");
    CHECK(a.kv["net_nonce"] == "5000", "last accepted nonce saved in flash (%s)", a.kv["net_nonce"].c_str());
    r = netCommand(bus, tool, {&a}, "214", signedText(key, 6000, "214", "talk 5"));
    CHECK(r.find("USB") != std::string::npos && !a.ic->transmitting(), "talk refused over the network");
    r = netCommand(bus, tool, {&a}, "214", signedText(key, 6001, "214", "netkey another-key-0123456"));
    CHECK(r.find("USB") != std::string::npos, "netkey refused over the network, even signed");
    bool leaked = false;
    for (auto& l : a.logs) leaked |= l.find("another-key") != std::string::npos;
    CHECK(!leaked, "keys never written to the log");
}

static void testBringUp() {
    printf("bring-up commands (talk, selftest, version)\n");
    Bus bus;
    FakeUnit a(&bus, {214, 0}, 120), b(&bus, {101, 0}, 160);
    for (int k = 0; k < 20; k++) { a.step(); b.step(); }
    CHECK(a.ic->command("version").find(kFirmwareVersion) != std::string::npos, "version: %s", a.ic->command("version").c_str());
    a.ic->command("talk 1");
    for (int k = 0; k < 40; k++) { a.step(); b.step(); }
    bool during = a.ic->transmitting();
    uint32_t heard = b.heardFrom(a.id);
    for (int k = 0; k < 20; k++) { a.step(); b.step(); }
    CHECK(during && heard > 20 && !a.ic->transmitting(), "talk 1: transmits for 1 s without the button (101 heard %u frames)", heard);

    // Self-test with the speaker coupled to the mic (a working unit) and without (dead speaker).
    for (int pass = 0; pass < 2; pass++) {
        bool coupled = pass == 0;
        CHECK(a.ic->command("selftest").find("started") != std::string::npos, "selftest starts");
        int16_t mic[kFrameSamples] = {}, spk[kFrameSamples] = {};
        uint32_t rng = 9;
        for (int k = 0; k < 100 && a.ic->selfTesting(); k++) {
            for (int i = 0; i < kFrameSamples; i++) {
                rng = rng * 1664525u + 1013904223u;
                int noise = int(rng >> 27) - 16;  // quiet room
                mic[i] = int16_t(noise + (coupled ? spk[i] / 4 : 0));
            }
            a.ic->processFrame(mic, spk);
        }
        std::string res = a.ic->command("selftest result");
        if (coupled)
            CHECK(res.find("SELF-TEST PASSED") != std::string::npos, "self-test passes with a working speaker:\n%s", res.c_str());
        else
            CHECK(res.find("FAIL speaker->mic") != std::string::npos, "self-test catches a dead speaker");
    }
}

static void testBackgroundCal() {
    printf("auto-tune analysis in a background task\n");
    std::vector<std::function<void()>> jobs;
    Bus bus;
    struct BgUnit : FakeUnit {
        BgUnit(Bus* b, std::vector<std::function<void()>>* jobs) : FakeUnit(b, {214, 0}, 120) {
            IntercomOptions io;
            io.maxTaps = 512;
            io.runBackground = [jobs](std::function<void()> j) { jobs->push_back(std::move(j)); };
            ic.reset(new Intercom(Hal{this, this, this, this, this, this}, aec, io));
            ic->begin();
        }
    } a(&bus, &jobs);
    sim::RoomEcho room;
    room.setPreset(sim::RoomPreset::Classroom);
    int16_t mic[kFrameSamples] = {}, spk[kFrameSamples] = {};
    float echo[kFrameSamples];
    uint32_t rng = 3;
    auto step = [&] {
        room.process(spk, echo, kFrameSamples);  // echo of the previous speaker frame
        for (int i = 0; i < kFrameSamples; i++) {
            rng = rng * 1664525u + 1013904223u;
            float n = 60.0f * (float(rng >> 8) / float(1u << 24) * 2 - 1);  // about -60 dBFS room noise
            mic[i] = int16_t(std::max(-32767.0f, std::min(32767.0f, echo[i] + n)));
        }
        a.ic->processFrame(mic, spk);
    };
    a.ic->command("cal");
    int k = 0;
    for (; k < 400 && jobs.empty(); k++) step();
    CHECK(jobs.size() == 1 && a.ic->calibrating() && a.ic->calProgress() == 99, "measurement done, analysis handed to the background (%d frames)", k);
    for (int i = 0; i < 10; i++) step();
    CHECK(a.ic->calibrating() && !a.ic->lastCal(), "audio keeps running while the analysis runs");
    jobs[0]();  // the background task finishes
    step();
    CHECK(!a.ic->calibrating() && a.ic->lastCal() && a.ic->lastCal()->ok, "result picked up on the next frame: %s",
          a.ic->lastCal() ? a.ic->lastCal()->summary().c_str() : "none");
}

// A TM1637 on two open-drain lines: decodes start / stop / bytes / ACK like the chip, and
// answers key reads. Checks the driver's bit stream without hardware.
struct FakeTm1637 : board::Tm1637Lines {
    bool present = true;
    bool mc = true, md = true, cd = true;  // master CLK, master DIO, chip DIO (true = released)
    bool inFrame = false, reading = false, firstByte = true;
    int bits = 0;
    uint8_t cur = 0, keyCode = 0xFF;
    std::vector<std::vector<uint8_t>> frames;
    bool line() const { return md && cd; }
    void clk(bool h) override {
        if (h == mc) return;
        mc = h;
        if (!inFrame || !present) return;
        if (h) {
            bits++;
            if (!reading && bits <= 8 && line()) cur |= uint8_t(1u << (bits - 1));
            return;
        }
        if (reading) {
            if (bits < 8) cd = (keyCode >> bits) & 1;
            else if (bits == 8) cd = true;
            else endByte();
        } else {
            if (bits == 8) cd = false;  // ACK
            else if (bits == 9) {
                cd = true;
                endByte();
            }
        }
    }
    void endByte() {
        if (!reading) {
            frames.back().push_back(cur);
            if (firstByte && cur == 0x42) {
                reading = true;
                cd = keyCode & 1;  // the chip presents bit 0 as the ACK clock ends
            }
            firstByte = false;
        }
        bits = 0;
        cur = 0;
    }
    void dio(bool h) override {
        if (h == md) return;
        bool was = line();
        md = h;
        if (!mc || was == line()) return;
        if (!h) {  // start: DIO falls while CLK high
            inFrame = true;
            reading = false;
            firstByte = true;
            bits = 0;
            cur = 0;
            frames.push_back({});
        } else {  // stop
            inFrame = false;
            reading = false;
        }
    }
    bool readDio() override { return line(); }
};

static void testPanelDriver() {
    printf("front panel (TM1637 through the I/O expander)\n");
    FakeTm1637 chip;
    board::Tm1637 tm(chip);
    const char text[3] = {'2', '1', '4'};
    bool ok = tm.show(text, 0x02, 4);
    using board::Tm1637;
    bool frames = chip.frames.size() == 3 && chip.frames[0] == std::vector<uint8_t>{0x40} &&
                  chip.frames[1] == std::vector<uint8_t>{0xC0, Tm1637::segments('2'), uint8_t(Tm1637::segments('1') | 0x80), Tm1637::segments('4')} &&
                  chip.frames[2] == std::vector<uint8_t>{0x8C};
    CHECK(ok && frames, "display \"21.4\": data command, 3 digits with the dot, display on (chip ACKed every byte)");

    // Buttons are on U22 (TCA9555 0x21), one input each, pressed = 0.
    struct Case { int bit; Button b; const char* name; };
    const Case cases[] = {{0, kBtnTalk, "PB1 TALK"}, {1, kBtnMode, "PB2 MODE"}, {2, kBtnUp, "PB4 UP"}, {3, kBtnDown, "PB5 DOWN"},
                          {4, kBtnSetup, "PB6 SETUP"}, {5, kBtnTalk, "PB7 TALK"}, {6, kBtnTalk, "PB8 TALK"}, {7, kBtnTalk, "PB9 TALK"},
                          {8, kBtnTalk, "PB10 TALK"}, {9, kBtnTalk, "PB11 TALK"}, {10, kBtnTalk, "PB12 TALK"}};
    for (const Case& c : cases)
        CHECK(board::buttonsFromInputs(uint16_t(~(1u << c.bit))) == (1u << c.b), "U22 P%d%d low -> %s", c.bit / 8, c.bit % 8, c.name);
    CHECK(board::buttonsFromInputs(0xFFFF) == 0, "nothing pressed -> no buttons");
    CHECK(board::buttonsFromInputs(uint16_t(~((1u << 1) | (1u << 2)))) == ((1u << kBtnMode) | (1u << kBtnUp)),
          "MODE + UP together are both seen (the old key scan saw one)");
    CHECK(board::buttonsFromInputs(uint16_t(~(1u << 13))) == 0, "free inputs P15-P17 do nothing");
    uint8_t got = 0;
    chip.keyCode = 0xFF;
    tm.readKeys(&got);

    FakeTm1637 missing;
    missing.present = false;
    board::Tm1637 tm2(missing);
    CHECK(!tm2.show(text, 0, 4), "missing display detected (no ACK)");

    // One-key panels: UP alone held 5 s starts auto-tune.
    Bus bus;
    FakeUnit u(&bus, {214, 0}, 120);
    u.held = 1u << kBtnUp;
    for (int k = 0; k < 240; k++) u.step();
    bool early = u.ic->calibrating();
    for (int k = 0; k < 20; k++) u.step();
    u.held = 0;
    CHECK(!early && u.ic->calibrating(), "UP held 5 s starts auto-tune (not before)");
}

static void testWeb() {
    printf("web interface\n");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 test vector");
    CHECK(sha256Hex(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", "SHA-256 two-block message");
    Bus bus;
    FakeUnit a(&bus, {214, 0}, 120), b(&bus, {101, 0}, 160);
    for (int k = 0; k < 60; k++) { a.step(); b.step(); }
    WebUi web(*a.ic);
    HttpRequest req;
    req.method = "GET";
    req.path = "/";
    CHECK(web.handle(req).status == 403, "no password set: web interface off");

    CHECK(a.ic->command("webpass short").find("short") != std::string::npos, "short password refused");
    CHECK(a.ic->command("webpass s3cret-pass", CmdSource::Network).find("USB") != std::string::npos &&
              a.ic->command("webpass s3cret-pass", CmdSource::Web).find("USB") != std::string::npos,
          "webpass refused from the network and the web");
    a.ic->command("webpass s3cret-pass");
    CHECK(a.kv["web_pass"].find("s3cret") == std::string::npos && a.kv["web_pass"].size() > 64, "password stored as a hash");

    // Installer tests password: none set = tests open to the web login; set = needed.
    CHECK(!a.ic->testsLocked() && a.ic->command("tests - couple run", CmdSource::Web).find("usage") != std::string::npos,
          "no tests password: \"tests -\" runs the command (here its usage)");
    CHECK(a.ic->command("testpass gym-key", CmdSource::Web).find("USB") != std::string::npos, "testpass refused from the web");
    a.ic->command("testpass gym-key");
    CHECK(a.ic->testsLocked() && a.ic->command("cal", CmdSource::Web).find("tests password") != std::string::npos &&
              a.ic->command("send all couple run 1 auto 101,214", CmdSource::Web).find("tests password") != std::string::npos,
          "tests password set: auto-tune and coupling test refused straight from the web");
    CHECK(a.ic->command("tests nope cal", CmdSource::Web).find("wrong") != std::string::npos, "wrong tests password refused");
    CHECK(a.ic->command("tests gym-key couple run", CmdSource::Web).find("usage") != std::string::npos &&
              a.ic->command("stats", CmdSource::Web).find("password") == std::string::npos,
          "right tests password runs it; other commands don't need it");
    a.ic->command("testpass off");
    CHECK(!a.ic->testsLocked(), "testpass off: no tests password again");

    HttpResponse r = web.handle(req);
    CHECK(r.status == 401 && r.askLogin && r.realm == "Intercom 214", "no login: 401 asks for the password");
    req.authorization = basic("admin:wrong-pass");
    CHECK(web.handle(req).status == 401, "wrong password refused");
    req.authorization = basic("admin:s3cret-pass");
    r = web.handle(req);
    CHECK(r.status == 200 && r.body.find("<html") != std::string::npos, "right password: page served (%zu bytes)", r.body.size());

    req.path = "/api/status";
    r = web.handle(req);
    CHECK(r.status == 200 && r.body.find("\"unit\":\"214\"") != std::string::npos && r.body.find("\"ip\":\"10.42.2.14\"") != std::string::npos &&
              r.body.find("\"key\":\"mu\"") != std::string::npos && r.body.find("\"unit\":\"101\"") != std::string::npos,
          "status JSON has unit, IP, settings and peers");

    req.path = "/school";
    r = web.handle(req);
    CHECK(r.status == 200 && r.body.find("School intercom") != std::string::npos, "school page served");
    CHECK(r.body.find("/floorplan.js") != std::string::npos && r.body.find("id=\"plan\"") != std::string::npos, "school page has the floor plan map");
    req.path = "/floorplan.js";
    r = web.handle(req);
    CHECK(r.status == 200 && r.contentType.find("javascript") != std::string::npos && r.body.find("planUpdate") != std::string::npos,
          "floor plan script served");
    req.path = "/api/school";
    r = web.handle(req);
    CHECK(r.status == 200 && r.body.find("\"self\":\"214\"") != std::string::npos && r.body.find("\"unit\":\"101\"") != std::string::npos &&
              r.body.find("\"online\":true") != std::string::npos,
          "school JSON lists this unit and the units it hears");
    for (int k = 0; k < 400; k++) a.step();  // 101 goes quiet for 8 s
    r = web.handle(req);
    CHECK(r.body.find("\"unit\":\"101\"") != std::string::npos && r.body.find("\"online\":false") != std::string::npos,
          "a unit that went quiet stays listed, offline");

    req.path = "/api/cmd";
    req.method = "POST";
    req.body = "set volume_db -3";
    CHECK(web.handle(req).status == 403, "command without X-Intercom header refused (cross-site)");
    req.xIntercom = "1";
    r = web.handle(req);
    CHECK(r.status == 200 && std::fabs(a.ic->params().volumeDb + 3) < 1e-6f, "web command applied: %s", r.body.c_str());
    req.body = "webpass newpassword1";
    CHECK(web.handle(req).body.find("USB") != std::string::npos && a.ic->command("webpass").find("on") != std::string::npos,
          "web can't change the password");

    // Web console talk: the office PC posts its microphone in 100 ms chunks; the unit sends it as an
    // office all-call and the other unit hears it.
    {
        sim::TestVoice office(110.0f, 9);
        uint32_t before = b.heardFrom(a.id);
        HttpRequest t;
        t.method = "POST";
        t.path = "/api/talk";
        t.authorization = req.authorization;
        t.xIntercom = "1";
        bool ok = true;
        for (int chunk = 0; chunk < 10; chunk++) {
            int16_t pcm[1600];
            office.render(pcm, 1600);
            std::string bytes(reinterpret_cast<const char*>(pcm), sizeof(pcm));
            t.body = basic(bytes).substr(6);  // base64 (basic() = "Basic " + base64)
            ok &= web.handle(t).status == 200;
            for (int k = 0; k < 5; k++) { a.step(); b.step(); }
        }
        CHECK(ok && a.ic->webTalking(), "web talk accepted and being sent");
        for (int k = 0; k < 40; k++) { a.step(); b.step(); }
        CHECK(b.heardFrom(a.id) > before + 35 && !a.ic->webTalking(), "the other unit heard the office's web talk (%u frames); it stopped after",
              b.heardFrom(a.id) - before);
        t.xIntercom = "";
        CHECK(web.handle(t).status == 403, "web talk without X-Intercom refused (cross-site)");
        t.xIntercom = "1";
        t.body = "not base64!";
        CHECK(web.handle(t).status == 400, "bad audio refused");

        // R2: a call to one room from the web page reaches that room only.
        auto webCall = [&](const char* to) {
            t.path = std::string("/api/talk?to=") + to;
            for (int chunk = 0; chunk < 8; chunk++) {
                int16_t pcm[1600];
                office.render(pcm, 1600);
                t.body = basic(std::string(reinterpret_cast<const char*>(pcm), sizeof(pcm))).substr(6);
                web.handle(t);
                for (int k = 0; k < 5; k++) { a.step(); b.step(); }
            }
            for (int k = 0; k < 40; k++) { a.step(); b.step(); }
        };
        uint32_t h0 = b.heardFrom(a.id);
        webCall("room:101");
        uint32_t h1 = b.heardFrom(a.id);
        CHECK(h1 > h0 + 25 && b.ic->canAnswer(), "web call to room 101: 101 hears it as a call (%u frames)", h1 - h0);
        for (int k = 0; k < 600; k++) { a.step(); b.step(); }  // past 101's answer window
        uint32_t h2 = b.heardFrom(a.id);
        webCall("room:305");
        CHECK(b.heardFrom(a.id) == h2, "web call to room 305: 101 doesn't hear it");
        t.path = "/api/talk?to=planet:3";
        CHECK(web.handle(t).status == 400, "bad target refused");
    }

    // Web console commands: 214's page sends a signed command to every unit and collects the replies.
    {
        HttpRequest c;
        c.method = "POST";
        c.path = "/api/cmd";
        c.authorization = req.authorization;
        c.xIntercom = "1";
        c.body = "send all version";
        CHECK(web.handle(c).body.find("no network key") != std::string::npos, "send needs this unit's network key");
        a.ic->command("netkey school-network-key-1");
        b.ic->command("netkey school-network-key-1");
        CHECK(web.handle(c).body.find("no clock") != std::string::npos, "send needs the clock (nonces are milliseconds since 1970)");
        a.ic->command("time 1795451390");
        CHECK(web.handle(c).body == "sent to all", "send all version: %s", web.handle(c).body.c_str());
        for (int k = 0; k < 5; k++) { a.step(); b.step(); }
        HttpRequest g;
        g.method = "GET";
        g.path = "/api/replies?since=0";
        g.authorization = req.authorization;
        std::string rj = web.handle(g).body;
        CHECK(rj.find("\"from\":\"101\",\"text\":\"firmware") != std::string::npos && rj.find("\"from\":\"214\",\"text\":\"firmware") != std::string::npos,
              "replies from 101 and from 214 itself come back to the page");
        CHECK(b.ic->command("send all version", CmdSource::Network).find("only works") != std::string::npos,
              "a unit won't relay commands that came over the network");
    }

    req.body = "tone 1";
    web.handle(req);
    int16_t mic[kFrameSamples] = {}, spk[kFrameSamples];
    a.ic->processFrame(mic, spk);
    CHECK(a.ic->toneActive() && a.ic->speakerLevel() > 60, "test tone plays (speaker level %u)", a.ic->speakerLevel());

    // Password change logs out the old one (even though the login is cached).
    a.ic->command("webpass another-pass");
    req.method = "GET";
    req.path = "/api/status";
    CHECK(web.handle(req).status == 401, "old password stops working after webpass");

    // 5 wrong passwords lock the page for 30 s, even for the right password.
    req.authorization = basic("admin:nope-nope");
    for (int k = 0; k < 5; k++) web.handle(req);
    req.authorization = basic("admin:another-pass");
    CHECK(web.handle(req).status == 429, "5 wrong passwords: locked");
    for (int k = 0; k < 30 * 50 + 5; k++) a.step();
    CHECK(web.handle(req).status == 200, "unlocked after 30 s");

    a.ic->command("webpass off");
    CHECK(web.handle(req).status == 403, "webpass off turns the web interface off");
}

// The office console on the test bus: sends targeted audio as room 0, counts answers, sees "calling".
struct FakeOffice {
    Bus* bus;
    std::deque<std::vector<uint8_t>> rx;
    sim::TestVoice voice{110.0f, 7};
    uint16_t seq = 0;
    int replies = 0;
    UnitId lastReplyFrom;
    std::vector<uint16_t> calling;  // rooms whose last status says "calling the office"
    explicit FakeOffice(Bus* b) : bus(b) { bus->queues.push_back(&rx); }
    void talk(uint16_t target, uint8_t flags) {
        int16_t pcm[kFrameSamples];
        voice.render(pcm, kFrameSamples);
        AudioHeader h;
        h.from = UnitId();
        h.flags = uint8_t(flags | kAudioFromConsole);
        h.target = target;
        h.seq = seq++;
        h.level = frameLevel(pcm, kFrameSamples);
        uint8_t buf[kMaxPacketBytes];
        size_t n = encodeAudio(h, pcm, buf);
        for (auto* q : bus->queues)
            if (q != &rx) q->push_back(std::vector<uint8_t>(buf, buf + n));
    }
    void drain() {
        for (; !rx.empty(); rx.pop_front()) {
            const std::vector<uint8_t>& p = rx.front();
            bool ok;
            PacketType t = peekType(p.data(), p.size(), &ok);
            if (!ok) continue;
            if (t == PacketType::Audio) {
                AudioHeader h;
                const uint8_t* d;
                size_t n;
                if (decodeAudioData(p.data(), p.size(), &h, &d, &n) && (h.flags & kAudioReply) && h.target == 0) {
                    replies++;
                    lastReplyFrom = h.from;
                }
            } else if (t == PacketType::Status) {
                StatusPacket s;
                if (!decodeStatus(p.data(), p.size(), &s)) continue;
                auto it = std::find(calling.begin(), calling.end(), s.from.room);
                if ((s.flags & kStatusCalling) && it == calling.end()) calling.push_back(s.from.room);
                if (!(s.flags & kStatusCalling) && it != calling.end()) calling.erase(it);
            }
        }
    }
    bool isCalling(uint16_t room) const { return std::find(calling.begin(), calling.end(), room) != calling.end(); }
};

static void testRoomCalls() {
    printf("room calls (R2.0): office, floors, groups, answers, call the office\n");
    Bus bus;
    FakeUnit r101(&bus, {101, 0}, 120), r102(&bus, {102, 0}, 135), r214(&bus, {214, 0}, 160), gymA(&bus, {299, 0}, 190),
        gymB(&bus, {298, 0}, 230);
    FakeOffice office(&bus);
    std::vector<FakeUnit*> all = {&r101, &r102, &r214, &gymA, &gymB};
    int officeTalk = 0;
    uint16_t officeTarget = 0;
    uint8_t officeFlags = 0;
    auto run = [&](int frames) {
        for (int k = 0; k < frames; k++) {
            if (officeTalk > 0) {
                office.talk(officeTarget, officeFlags);
                officeTalk--;
            }
            for (auto* u : all) u->step();
            office.drain();
        }
    };
    auto officeCall = [&](uint16_t target, uint8_t flags, int frames) {
        officeTarget = target;
        officeFlags = flags;
        officeTalk = frames;
        run(frames + 10);
    };
    const UnitId kOffice;
    run(20);
    gymA.ic->command("group 299");
    gymB.ic->command("group 299");
    run(60);  // status beacons carry the groups

    // The office calls room 214: only 214 hears it, with a chime and CAL.
    officeTarget = 214, officeFlags = 0, officeTalk = 50;
    run(3);
    CHECK(r214.ic->chiming() && r214.ic->inCall(), "214 chimes and shows CAL when the office calls it");
    int lampOn = 0, lampOff = 0, lampElsewhere = 0, rgbOn = 0, rgbOff = 0, rgbSync = 0;
    for (int k = 0; k < 57; k++) {
        run(1);
        (r214.ic->lampLevel(Lamp::Other) ? lampOn : lampOff)++;
        (r214.ic->rgb() ? rgbOn : rgbOff)++;
        if ((r214.ic->rgb() != 0) == (r214.ic->lampLevel(Lamp::Other) != 0)) rgbSync++;
        if (r101.ic->lampLevel(Lamp::Other) || r101.ic->rgb()) lampElsewhere++;
    }
    CHECK(lampOn > 15 && lampOff > 15 && !lampElsewhere, "call light blinks at 214 only (%d on, %d off frames)", lampOn, lampOff);
    CHECK(rgbOn > 15 && rgbOff > 15 && rgbSync == 57 && !r214.ic->callAnswered(),
          "the RGB LED blinks blue with the call light until answered (%d on, %d off)", rgbOn, rgbOff);
    CHECK(r214.heardFrom(kOffice) > 40 && r101.heardFrom(kOffice) == 0 && gymA.heardFrom(kOffice) == 0,
          "room call: 214 heard the office (%u frames), 101 and 299 did not", r214.heardFrom(kOffice));
    // 214 answers with TALK within 10 s: only the office hears it.
    r214.held = 1;
    run(50);
    r214.held = 0;
    run(10);
    CHECK(office.replies > 40 && office.lastReplyFrom == r214.id, "214's answer went to the office (%d frames)", office.replies);
    CHECK(r101.heardFrom(r214.id) == 0 && gymA.heardFrom(r214.id) == 0, "nobody else heard the answer");
    CHECK(r214.ic->callAnswered() && r214.ic->rgb() != 0, "answered: the RGB LED stops blinking and stays on during the call");
    run(520);  // more than 10 s after the call
    CHECK(!r214.ic->canAnswer(), "the answer window closes 10 s after the call");
    r214.held = 1;
    run(30);
    r214.held = 0;
    run(10);
    CHECK(r101.heardFrom(r214.id) > 15, "after that, TALK is an all-call again");
    CHECK(!r214.ic->missedCall(), "an answered call is not a missed call");
    // The office calls 214 again and nobody answers: a slow missed-call blink until a button is pressed.
    officeTarget = 214, officeFlags = 0, officeTalk = 50;
    run(60);
    CHECK(!r214.ic->callAnswered() && r214.ic->canAnswer(), "second call to 214, not answered yet");
    run(510);
    CHECK(r214.ic->missedCall() && !r214.ic->canAnswer(), "10 s with no answer: 214 shows a missed call");
    {
        int on = 0;
        for (int k = 0; k < 200; k++) {
            run(1);
            if (r214.ic->rgb()) on++;
        }
        CHECK(on > 10 && on < 60, "missed call: a short blue blink every 2 s (%d of 200 frames on)", on);
    }
    r214.held = 1u << kBtnDown;  // any button (DOWN, briefly)
    run(3);
    r214.held = 0;
    run(5);
    CHECK(!r214.ic->missedCall() && r214.ic->rgb() == 0 && r214.ic->lampLevel(Lamp::Other) == 0,
          "a button press clears the missed call and its lights");

    // Floor 1: 101 and 102, not 214.
    uint32_t h214 = r214.heardFrom(kOffice);
    officeCall(1, kAudioFloor, 50);
    CHECK(r101.heardFrom(kOffice) > 40 && r102.heardFrom(kOffice) > 40 && r214.heardFrom(kOffice) == h214,
          "floor call 1: 101 and 102 heard it, 214 did not");
    // Group 299: both gym units, nobody else.
    run(520);
    uint32_t h101 = r101.heardFrom(kOffice);
    officeCall(299, kAudioGroupCall, 50);
    CHECK(gymA.heardFrom(kOffice) > 40 && gymB.heardFrom(kOffice) > 40 && r101.heardFrom(kOffice) == h101,
          "group call 299: both gym units heard it, 101 did not");
    run(520);

    // Unit to unit (office unit / bring-up): 101 calls 214, 214 answers 101 only.
    CHECK(r101.ic->command("talk 1 room 214").find("to room 214") != std::string::npos, "\"talk 1 room 214\" accepted");
    uint32_t h102 = r102.heardFrom(r101.id), h214b = r214.heardFrom(r101.id);
    run(60);
    CHECK(r214.heardFrom(r101.id) > h214b + 40 && r102.heardFrom(r101.id) == h102, "101's room call reached 214 only");
    uint32_t r101from214 = r101.heardFrom(r214.id), r102from214 = r102.heardFrom(r214.id);
    r214.held = 1;
    run(50);
    r214.held = 0;
    run(10);
    CHECK(r101.heardFrom(r214.id) > r101from214 + 40 && r102.heardFrom(r214.id) == r102from214, "214's answer reached 101 only");
    run(520);

    // An office all-call wins: a room call during it isn't played.
    h214b = r214.heardFrom(r101.id);
    officeTarget = 0, officeFlags = kAudioAllCall, officeTalk = 80;
    run(10);
    CHECK(r101.ic->lampLevel(Lamp::Other) == 100 && r214.ic->lampLevel(Lamp::Other) == 100, "office all-call: call light steady on");
    r101.ic->command("talk 1 room 214");
    run(60);
    CHECK(r214.heardFrom(r101.id) == h214b, "a room call during an office all-call is not played");
    run(540);

    // Call the office: DOWN held 2 s; the office sees it; answering clears it.
    r214.held = 1u << kBtnDown;
    run(105);
    r214.held = 0;
    run(55);
    CHECK(r214.ic->callingOffice() && office.isCalling(214), "DOWN held 2 s: 214 calls the office (status flag)");
    officeCall(214, 0, 20);
    run(55);
    CHECK(!r214.ic->callingOffice() && !office.isCalling(214), "the office calling back clears it");
    run(520);
    r101.ic->command("callofc");
    run(55);
    CHECK(office.isCalling(101), "\"callofc\" calls the office");
    r101.ic->command("callofc off");
    run(55);
    CHECK(!office.isCalling(101), "\"callofc off\" cancels");
}

static void testTimedEvents() {
    printf("timed events (R2.1): clock, schedule, days off, priority\n");
    Schedule s;
    s.tzMinutes = -300;
    s.dst = DstRule::Us;
    LocalTime t = s.local(1795451390), t2;  // 2026-11-23 16:29:50 UTC
    CHECK(t.hour == 11 && t.minute == 29 && t.second == 50 && t.wday == 0 && !t.dst && t.ymd() == 20261123,
          "UTC -> US Eastern in November: Mon 11:29:50 EST");
    t = s.local(1783351790);
    CHECK(t.hour == 11 && t.minute == 29 && t.dst && t.ymd() == 20260706, "July: 11:29 EDT (daylight saving)");
    t = s.local(1772953199), t2 = s.local(1772953201);
    CHECK(t.hour == 1 && t.minute == 59 && !t.dst && t2.hour == 3 && t2.dst, "spring forward on 8 Mar 2026: 1:59 -> 3:00");
    t = s.local(1793512799), t2 = s.local(1793512801);
    CHECK(t.hour == 1 && t.dst && t2.hour == 1 && t2.minute == 0 && !t2.dst, "fall back on 1 Nov 2026: 1:59 EDT -> 1:00 EST");
    std::vector<std::string> a = {"sched", "add", "11:30", "mon-fri", "lunch", "floor", "1"};
    ScheduleEntry e;
    CHECK(Schedule::parseEntry(a, 2, &e).empty() && Schedule::formatEntry(e) == "11:30 mon-fri lunch floor 1",
          "parses \"11:30 mon-fri lunch floor 1\"");
    uint8_t m = 0;
    CHECK(Schedule::parseDays("fri-mon", &m) && m == 0x71, "days wrap round: fri-mon");
    s.add(e);
    s.daysOff.push_back(20261224);
    Schedule r;
    CHECK(r.fromString(s.toString()) && r.toString() == s.toString(), "schedule saves and loads (%s)", s.toString().c_str());

    Bus bus;
    FakeUnit r101(&bus, {101, 0}, 120), r214(&bus, {214, 0}, 160);
    FakeOffice office(&bus);
    std::vector<FakeUnit*> all = {&r101, &r214};
    int officeTalk = 0;
    bool any101 = false, any214 = false;
    Sound last101 = Sound::Tone;
    auto run = [&](int frames) {
        for (int k = 0; k < frames; k++) {
            if (officeTalk > 0) {
                office.talk(214, 0);
                officeTalk--;
            }
            for (auto* u : all) u->step();
            office.drain();
            if (r101.ic->soundPlaying()) any101 = true, last101 = r101.ic->currentSound();
            if (r214.ic->soundPlaying() && !r214.ic->chiming()) any214 = true;
        }
    };
    g_clock0 = 0;
    CHECK(r101.ic->command("time").find("no clock") == 0, "no NTP and no office time: no clock, no timed events");
    CHECK(r101.ic->command("time 1795451390").find("2026-11-23 16:29:50 Mon, clock from the office") == 0,
          "\"time\" from the office sets it (UTC until a time zone is set)");
    g_clock0 = 1795451390;  // NTP: both units' clocks read Mon 11:29:50 EST at frame 0
    for (auto* u : all) {
        u->ic->command("tz -300 us");
        u->ic->command("sched add 11:30 mon-fri lunch floor 1");
        u->ic->command("sched add 11:31 daily bell");
        u->ic->command("sched add 11:33 weekend tone");
    }
    CHECK(r101.ic->command("time").find("2026-11-23 11:29:50 Mon, clock from NTP") == 0, "clock: %s", r101.ic->command("time").c_str());
    CHECK(!r101.kv["sched"].empty(), "the schedule is saved in flash");
    run(495);  // 11:29:59.9
    CHECK(!any101, "nothing before 11:30");
    run(305);  // 11:30:06
    CHECK(any101 && last101 == Sound::Lunch && !any214, "11:30 lunch for floor 1: 101 played it, 214 didn't");
    // 11:31 bell for everyone, while the office is calling 214: 214 plays it after the call.
    any101 = any214 = false;
    run(2600);  // 11:30:58
    officeTalk = 200;  // 4 s call to 214
    run(150);          // 11:31:01
    CHECK(any101 && last101 == Sound::Bell && !any214, "11:31 bell: 101 rings, 214 waits (a call is on)");
    run(220);  // the call ended about 1 s ago
    CHECK(any214, "214 rings once the call is over");
    // A day off: nothing plays.
    any101 = false;
    r101.ic->command("sched add 11:32 daily tone");
    r101.ic->command("dayoff add 2026-11-23");
    run(3000);  // past 11:32
    CHECK(!any101 && r101.ic->command("time").find("day off") != std::string::npos, "day off: no timed events");
    r101.ic->command("dayoff del 2026-11-23");
    CHECK(r101.ic->command("skip today") == "no timed events today" && r101.ic->command("skip off") == "timed events back on",
          "skip today / skip off");
    CHECK(r101.ic->command("sched del 1").find("deleted: 11:30") == 0 && r101.ic->schedule().entries.size() == 3, "sched del 1");
    CHECK(r101.ic->command("sched add 25:00 daily bell").find("bad time") == 0, "bad times are refused");
    g_clock0 = 0;
}

static void testBootLightTest() {
    printf("power-up light test\n");
    Bus bus;
    g_bootTest = true;
    FakeUnit u(&bus, {214, 0}, 160);
    g_bootTest = false;
    CHECK(u.ic->bootTesting(), "starts at power-up");
    for (int k = 0; k < 230; k++) u.step();
    uint8_t segsSeen = 0, dotsSeen = 0;
    bool allOn = false;
    for (auto& s : u.shown) {
        for (int i = 0; i < 3; i++)
            if (uint8_t(s[i]) & 0x80) segsSeen |= uint8_t(s[i]) & 0x7f;
        dotsSeen |= uint8_t(s[3]);
        if (uint8_t(s[0]) == 0xff && uint8_t(s[3]) == 7) allOn = true;
    }
    CHECK(segsSeen == 0x7f && dotsSeen == 7 && allOn, "every segment and dot lit, then 8.8.8. (segments 0x%02x)", segsSeen);
    CHECK(u.lampSeen == 0x07 && u.rgbSeen == 0xffffff, "white, IR and call lamps and the RGB LED's red, green, blue all lit");
    CHECK(!u.ic->bootTesting() && u.shown.back().substr(0, 3) == "214" && !u.ic->lampLevel(Lamp::White) && !u.ic->rgb(),
          "ends after about 4 s: room number back, everything off");
    CHECK(u.ic->command("boottest off").find("off") != std::string::npos && u.kv["boottest"] == "off", "\"boottest off\" saves");
    u.ic->command("boottest run");
    u.step();
    CHECK(u.ic->bootTesting(), "\"boottest run\" runs it again");
}

// Room coupling test with a simple acoustic model between the units: each mic hears its own speaker
// at -10 dB, its wall neighbour at -30 dB, and a unit in the same gym at -12 dB.
static void testCoupling() {
    printf("room coupling test (acoustic model between units)\n");
    Bus bus;
    FakeUnit r101(&bus, {101, 0}, 120), r102(&bus, {102, 0}, 135), r214(&bus, {214, 0}, 160), g299(&bus, {299, 0}, 190),
        g298(&bus, {298, 0}, 230);
    std::vector<FakeUnit*> all = {&r101, &r102, &r214, &g299, &g298};
    const int N = 5;
    float G[N][N] = {};
    for (int i = 0; i < N; i++) G[i][i] = 0.316f;  // -10 dB: own speaker
    G[0][1] = G[1][0] = 0.0316f;                   // 101 <-> 102 through the wall: -30 dB
    G[3][4] = G[4][3] = 0.25f;                     // 299 <-> 298 in the gym: -12 dB
    std::vector<std::vector<int16_t>> spk(N, std::vector<int16_t>(kFrameSamples, 0));
    std::vector<double> spkSum(N, 0);
    auto run = [&](int frames) {
        for (int k = 0; k < frames; k++) {
            std::vector<std::vector<int16_t>> next(N, std::vector<int16_t>(kFrameSamples, 0));
            for (int i = 0; i < N; i++) {
                int16_t mic[kFrameSamples];
                if (all[i]->held & 1) all[i]->voice.render(mic, kFrameSamples);
                else memset(mic, 0, sizeof(mic));
                for (int s = 0; s < kFrameSamples; s++) {
                    float v = mic[s] + 3.0f * float((s * 7919 + k * 104729 + i * 31) % 200 - 100) / 100.0f;  // a little room noise
                    for (int j = 0; j < N; j++) v += G[i][j] * spk[j][s];
                    mic[s] = int16_t(std::max(-32767.0f, std::min(32767.0f, v)));
                }
                all[i]->ic->processFrame(mic, next[i].data());
                double p = 0;
                for (int16_t x : next[i]) p += double(x) * x;
                spkSum[i] += p;
            }
            spk = next;
        }
    };
    run(60);
    for (auto* u : all) u->ic->command("couple run 1 auto 101,102,214,298,299");
    CHECK(r101.ic->couplingTest(), "coupling test started on every unit");
    run(300);
    auto db = [](FakeUnit& u, uint16_t room) {
        for (const auto& r : u.ic->couplings())
            if (r.room == room) return r.heard ? r.db : -99.0f;
        return -100.0f;
    };
    CHECK(std::fabs(db(r101, 102) + 20) < 4 && std::fabs(db(r102, 101) + 20) < 4,
          "101 and 102 hear each other through the wall at about -20 dB (%.1f / %.1f)", double(db(r101, 102)), double(db(r102, 101)));
    CHECK(db(r101, 214) < -98 && db(r214, 101) < -98, "101 and 214 don't hear each other");
    CHECK(db(g299, 298) > -6 && db(g298, 299) > -6, "the gym units hear each other like their own speaker (%.1f dB)", double(db(g299, 298)));
    CHECK(g298.ic->group() == 299 && g299.ic->group() == 299 && r101.ic->group() == 101 && r102.ic->group() == 102,
          "auto: the gym units are now room group 299; 101 and 102 stay separate");
    CHECK(r101.kv["couple_map"].find("102:") != std::string::npos, "the coupling map is saved");
    CHECK(r101.ic->command("couple").find("102 -2") != std::string::npos || r101.ic->command("couple").find("through wall") != std::string::npos,
          "\"couple\" shows the result: %s", r101.ic->command("couple").c_str());

    // Ducking: 101 plays 102 (heard through the wall) about 6 dB quieter than 214 (not heard).
    auto playLevel = [&](FakeUnit& talker) {
        run(40);
        std::fill(spkSum.begin(), spkSum.end(), 0.0);
        talker.held = 1;
        run(150);
        talker.held = 0;
        double p = spkSum[0];
        run(40);
        return 10 * std::log10(p + 1);
    };
    double from214 = playLevel(r214), from102 = playLevel(r102);
    CHECK(from214 - from102 > 4 && from214 - from102 < 8, "101 plays 102 quieter than 214 (%.1f dB)", from214 - from102);

    // Doors open: 101 and 214 now hear each other through the open doors. Run again labelled "open":
    // a second map, kept apart from the doors-closed one; ducking uses the louder of the two.
    G[0][2] = G[2][0] = 0.0316f;
    run(60);
    for (auto* u : all) u->ic->command("couple run 1 auto 101,102,214,298,299 open");
    run(300);
    CHECK(r101.kv["couple_open_map"].find("214:") != std::string::npos && db(r101, 214) < -98,
          "doors-open result saved on its own; the doors-closed map still says 101 and 214 don't hear each other");
    CHECK(std::fabs(r101.ic->couplingDb(214) + 20) < 4 && std::fabs(r101.ic->couplingDb(102) + 20) < 4,
          "ducking uses the louder of doors open / closed (214 %.1f dB, 102 %.1f dB)",
          double(r101.ic->couplingDb(214)), double(r101.ic->couplingDb(102)));
    CHECK(g298.ic->group() == 299 && r101.ic->group() == 101 && r214.ic->group() == 214,
          "a doors-open run doesn't change room groups");
    std::string c = r101.ic->command("couple");
    CHECK(c.find("doors closed") != std::string::npos && c.find("doors open") != std::string::npos, "\"couple\" shows both: %s", c.c_str());
}

int main() {
    testBootLightTest();
    testTuning();
    testAutoTune();
    testConsole();
    testWeb();
    testPanelDriver();
    testNetAuth();
    testBringUp();
    testBackgroundCal();
    testPackets();
    testJitter();
    testMixer();
    testIntercom();
    testRoomCalls();
    testTimedEvents();
    testCoupling();
    testAec();
    testCrossRoom();
    printf(g_fail ? "\n%d FAILED\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
