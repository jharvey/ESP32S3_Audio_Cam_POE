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
static bool g_camera = false;       // new FakeUnits have a camera
static std::string g_bootNote;      // ... and this start reason
static bool g_bootTest = false;  // power-up light test in new FakeUnits (off: it would light lamps in other tests)

struct FakeUnit : INet, ILinks, IPower, IPanel, IStore, ILog, ILamps, IDiag, IFirmware, IClips {
    // R3.1: fall-back state of the running firmware, a recorded message, a speaker->microphone path
    bool fwPending = false;
    int fwConfirmed = 0, fwRejected = 0;
    bool pending() override { return fwPending; }
    void confirm() override { fwConfirmed++; fwPending = false; }
    void reject() override { fwRejected++; }
    std::vector<int16_t> clipLockdown;
    const int16_t* clip(const char* name, size_t* n) override {
        if (clipLockdown.empty() || strcmp(name, "lockdown")) return nullptr;
        *n = clipLockdown.size();
        return clipLockdown.data();
    }
    bool loop = false;  // the microphone hears the speaker (one frame later, at half the level)
    int16_t lastSpk[kFrameSamples] = {};
    // the spare firmware slot
    std::vector<uint8_t> fwSlot, fwBooting;  // being written / committed (starts at the next restart)
    size_t fwSize = 0;
    int fwDropped = 0;
    bool fwWriteFails = false;
    bool begin(size_t size) override { fwSlot.clear(); fwSize = size; return true; }
    bool write(const uint8_t* d, size_t n) override {
        if (fwWriteFails) return false;
        fwSlot.insert(fwSlot.end(), d, d + n);
        return true;
    }
    bool finish(bool commit) override {
        if (!commit) { fwDropped++; fwSlot.clear(); return true; }
        if (fwSlot.size() != fwSize) return false;
        fwBooting = fwSlot;
        return true;
    }
    std::string error() override { return "fake flash error"; }
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
    int restarts = 0;
    bool restartDownload = false;

    FakeUnit(Bus* b, UnitId u, float f0, uint32_t uid = 0) : bus(b), id(u), voice(f0, u.room) {
        bus->queues.push_back(&rx);
        IntercomOptions opt;
        opt.unitUid = uid ? uid : 0xA0000000u + u.room;  // stands in for the board's MAC
        opt.utcNow = [this] { return g_clock0 ? g_clock0 + int64_t(ic->frame()) / 50 : int64_t(0); };
        if (!g_bootTest) kv["boottest"] = "off";
        opt.restart = [this](bool d) { restarts++; restartDownload = d; };
        opt.hasCamera = g_camera;
        opt.bootNote = g_bootNote;
        ic.reset(new Intercom(Hal{this, this, this, this, this, this, this, this, this, this}, aec, opt));
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
    unsigned shutUs = 0;
    bool shutSlow = false;
    void setShutter(uint16_t us, bool slow) override { shutUs = us; shutSlow = slow; }
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
        else if (loop) for (int i = 0; i < kFrameSamples; i++) mic[i] = int16_t(lastSpk[i] / 2);
        else memset(mic, 0, sizeof(mic));
        ic->processFrame(mic, spk);
        memcpy(lastSpk, spk, sizeof(lastSpk));
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
    CHECK(a.ic->command("reboot now").find("usage") != std::string::npos, "reboot: bad argument refused");
    CHECK(a.ic->command("reboot download", CmdSource::Network).find("flashing") != std::string::npos && a.restarts == 0,
          "reboot download accepted, not at once (the reply goes out first)");
    for (int k = 0; k < 30; k++) a.step();
    CHECK(a.restarts == 1 && a.restartDownload, "0.5 s later the unit restarts into USB flashing mode");
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
                          {4, kBtnSetup, "PB6 SETUP"}, {5, kBtnOffice, "PB7 OFFICE"}, {6, kBtnTalk, "PB8 TALK"}, {7, kBtnTalk, "PB9 TALK"},
                          {8, kBtnTalk, "PB10 TALK"}, {9, kBtnTalk, "PB11 TALK"}, {10, kBtnRed, "PB12 RED"}};
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

// Firmware update over the network (R3.0).
static void testFirmwareUpdate() {
    printf("firmware update over the network\n");
    {   // the piecewise hash against the one-shot one, with awkward piece sizes
        std::string msg;
        for (int i = 0; i < 1000; i++) msg += char(i * 7 + 3);
        bool same = true;
        for (size_t piece : {size_t(1), size_t(63), size_t(64), size_t(65), size_t(127), size_t(1000)}) {
            Sha256 h;
            for (size_t o = 0; o < msg.size(); o += piece)
                h.add(reinterpret_cast<const uint8_t*>(msg.data()) + o, std::min(piece, msg.size() - o));
            same = same && h.hex() == sha256Hex(msg);
        }
        CHECK(same, "SHA-256 fed in pieces = SHA-256 in one go");
    }
    Bus bus;
    FakeUnit a(&bus, {214, 0}, 120), b(&bus, {101, 0}, 160);
    auto run = [&](int n) { for (int k = 0; k < n; k++) { a.step(); b.step(); } };
    run(60);
    a.ic->command("webpass s3cret-pass");
    WebUi web(*a.ic);
    // POST (or GET) to a unit's web interface, logged in, with the X-Intercom header.
    auto post = [](WebUi& w, const std::string& path, const std::string& body = "", const char* method = "POST") {
        HttpRequest q;
        q.method = method;
        q.path = path;
        q.authorization = basic("admin:s3cret-pass");
        q.xIntercom = "1";
        q.body = body;
        return w.handle(q);
    };
    auto b64 = [](const uint8_t* p, size_t n) { return basic(std::string(reinterpret_cast<const char*>(p), n)).substr(6); };
    // An image: the ESP32 start byte, noise, and this firmware's tag lying across the 4096-byte piece boundary.
    auto image = [](size_t n, uint32_t seed) {
        std::vector<uint8_t> im(n);
        for (auto& v : im) v = uint8_t((seed = seed * 1664525u + 1013904223u) >> 24);
        im[0] = kFwMagic;
        memcpy(&im[4090], kFwTag, strlen(kFwTag));
        return im;
    };
    auto shaOf = [](const std::vector<uint8_t>& im) { return sha256Hex(std::string(im.begin(), im.end())); };
    auto piece = [&](WebUi& w, const std::vector<uint8_t>& im, size_t off, size_t n) {
        return post(w, "/api/fw/data?off=" + std::to_string(off), b64(&im[off], std::min(n, im.size() - off)));
    };
    std::vector<uint8_t> im = image(100000, 1);

    HttpResponse r = post(web, "/api/fw/begin?size=100000");
    CHECK(r.status == 403 && r.body.find("netupdate on") != std::string::npos, "off until switched on: %s", r.body.c_str());
    CHECK(a.ic->command("netupdate on", CmdSource::Web).find("USB") != std::string::npos &&
              a.ic->command("netupdate on", CmdSource::Network).find("USB") != std::string::npos && !a.ic->netUpdateAllowed(),
          "netupdate can't be switched on from the web or the network");
    CHECK(a.ic->command("netupdate on").find(": on") != std::string::npos && a.kv["netupdate"] == "on", "netupdate on from USB, saved");
    r = post(web, "/api/fw", "", "GET");
    CHECK(r.status == 200 && r.body.find("\"allowed\":true") != std::string::npos && r.body.find("\"state\":\"idle\"") != std::string::npos,
          "state JSON: %s", r.body.c_str());
    HttpRequest bare;
    bare.method = "POST";
    bare.path = "/api/fw/begin?size=100000";
    bare.authorization = basic("admin:s3cret-pass");
    CHECK(web.handle(bare).status == 403, "needs the X-Intercom header");
    bare.authorization.clear();
    bare.xIntercom = "1";
    CHECK(web.handle(bare).status == 401, "needs the web password");
    CHECK(post(web, "/api/fw/begin?size=100").status == 400 && post(web, "/api/fw/begin?size=99999999").status == 400 &&
              post(web, "/api/fw/begin?size=-5").status == 400 && post(web, "/api/fw/begin").status == 400 &&
              post(web, "/api/fw/begin?size=100000&sha=xyz").status == 400,
          "bad sizes and a bad hash are refused");
    CHECK(piece(web, im, 0, 4096).status == 400 && post(web, "/api/fw/end").status == 400, "data and end without begin are refused");

    // The good path, with the hiccups a real transfer has.
    CHECK(post(web, "/api/fw/begin?size=100000&sha=" + shaOf(im)).status == 200, "begin");
    CHECK(piece(web, im, 0, 4096).body == "ok 4096", "first piece");
    r = piece(web, im, 0, 4096);
    CHECK(r.status == 409 && r.body == "expected offset 4096", "a repeated piece: told where to go on (%s)", r.body.c_str());
    r = piece(web, im, 9000, 100);
    CHECK(r.status == 409 && a.fwSlot.size() == 4096, "a piece from further on is not written");
    CHECK(post(web, "/api/fw/data?off=4096", "not base64!").status == 400 &&
              post(web, "/api/fw/data?off=4096", std::string(12000, 'A')).status == 400 && a.fwSlot.size() == 4096,
          "a damaged or oversize piece is refused, the transfer stays open");
    b.held = 1;  // a call comes in: it goes first
    run(30);
    r = piece(web, im, 4096, 4096);
    CHECK(r.status == 503 && a.fwSlot.size() == 4096 && a.ic->talkersPlaying() == 1, "while a call plays the piece waits (503)");
    b.held = 0;
    run(60);
    CHECK(post(web, "/api/fw/end").status == 400 && a.fwBooting.empty(), "end before everything arrived is refused");
    bool sent = true;
    for (size_t o = 4096; o < im.size() && sent; o += 4096) sent = piece(web, im, o, 4096).status == 200;
    CHECK(sent && a.fwSlot.size() == im.size(), "the rest arrives after the call");
    int restarts0 = a.restarts;
    r = post(web, "/api/fw/end");
    CHECK(r.status == 200 && r.body.find(shaOf(im)) != std::string::npos && a.fwBooting == im, "end: image committed (%s)", r.body.c_str());
    run(30);
    CHECK(a.restarts == restarts0 + 1 && !a.restartDownload, "the unit restarts into it");
    CHECK(a.kv["web_pass"].size() > 64 && a.kv["netupdate"] == "on", "settings are untouched");
    CHECK(post(web, "/api/fw/begin?size=100000").status == 400, "no second update before the restart");

    // What must not get in: each on a fresh unit.
    auto refused = [&](const char* what, const std::vector<uint8_t>& bad, const std::string& sha, size_t declared) {
        Bus own;  // its own network: the unit is gone again at the end of this check
        FakeUnit u(&own, {215, 0}, 130);
        u.ic->command("webpass s3cret-pass");
        u.ic->command("netupdate on");
        WebUi w(*u.ic);
        std::string why;
        bool began = post(w, "/api/fw/begin?size=" + std::to_string(declared) + (sha.empty() ? "" : "&sha=" + sha)).status == 200;
        for (size_t o = 0; began && o < bad.size(); o += 4096) {
            HttpResponse x = piece(w, bad, o, 4096);
            if (x.status != 200) { why = x.body; break; }
        }
        if (why.empty()) why = post(w, "/api/fw/end").body;
        for (int k = 0; k < 40; k++) u.step();
        CHECK(began && u.fwBooting.empty() && u.restarts == 0 && why.compare(0, 2, "ok") != 0, "%s: refused (%s)", what, why.c_str());
    };
    std::vector<uint8_t> bad = im;
    bad[0] = 0x7f;
    refused("not an ESP32 image", bad, "", bad.size());
    bad = im;
    bad[4095] ^= 1;  // breaks the tag
    refused("another project's firmware (no tag)", bad, "", bad.size());
    bad = im;
    bad[50000] ^= 0x10;
    refused("one bit flipped on the way (hash)", bad, shaOf(im), bad.size());
    bad = im;
    bad.resize(90000);
    refused("cut short", bad, "", 100000);
    refused("longer than declared", im, "", 98000);

    // Flash write error, a stalled transfer, an abort, and a new begin replacing an unfinished one.
    FakeUnit c(&bus, {216, 0}, 140);
    c.ic->command("webpass s3cret-pass");
    c.ic->command("netupdate on");
    WebUi wc(*c.ic);
    post(wc, "/api/fw/begin?size=100000");
    c.fwWriteFails = true;
    r = piece(wc, im, 0, 4096);
    CHECK(r.status == 400 && r.body.find("fake flash error") != std::string::npos && wc.firmware().state() == FwUpdate::State::Failed,
          "a flash write error stops the update and says why (%s)", r.body.c_str());
    c.fwWriteFails = false;
    post(wc, "/api/fw/begin?size=100000");
    piece(wc, im, 0, 4096);
    for (int k = 0; k < 31 * 50; k++) c.step();
    r = piece(wc, im, 4096, 4096);
    CHECK(r.status == 400 && wc.firmware().error().find("stalled") != std::string::npos, "a transfer that stalls for 30 s is dropped");
    post(wc, "/api/fw/begin?size=100000");
    piece(wc, im, 0, 4096);
    int dropped0 = c.fwDropped;
    CHECK(post(wc, "/api/fw/abort").status == 200 && c.fwDropped == dropped0 + 1 && wc.firmware().state() == FwUpdate::State::Failed, "abort drops it");
    post(wc, "/api/fw/begin?size=100000");
    piece(wc, im, 0, 4096);
    std::vector<uint8_t> im2 = image(70000, 9);
    bool ok2 = post(wc, "/api/fw/begin?size=70000").status == 200;
    for (size_t o = 0; o < im2.size() && ok2; o += 8192) ok2 = piece(wc, im2, o, 8192).status == 200;
    CHECK(ok2 && post(wc, "/api/fw/end").status == 200 && c.fwBooting == im2, "a new begin replaces an unfinished transfer (8192-byte pieces)");

    FakeUnit d(&bus, {217, 0}, 150);
    d.pw.source = PowerSource::Battery;
    d.pw.hasBattery = true;
    d.pw.batteryPct = 20;
    d.ic->command("webpass s3cret-pass");
    d.ic->command("netupdate on");
    for (int k = 0; k < 120; k++) d.step();
    WebUi wd(*d.ic);
    r = post(wd, "/api/fw/begin?size=100000");
    CHECK(r.status == 409, "on battery below 30 %%: not started (%s)", r.body.c_str());
}

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
        if ((r214.ic->rgb() != 0) == (r214.ic->lampLevel(Lamp::Other) != 0) &&
            r214.ic->lampLevel(Lamp::White) == r214.ic->lampLevel(Lamp::Other))
            rgbSync++;
        if (r101.ic->lampLevel(Lamp::Other) || r101.ic->rgb()) lampElsewhere++;
    }
    CHECK(lampOn > 15 && lampOff > 15 && !lampElsewhere, "call light blinks at 214 only (%d on, %d off frames)", lampOn, lampOff);
    CHECK(rgbOn > 15 && rgbOff > 15 && rgbSync == 57 && !r214.ic->callAnswered(),
          "the white lamp and the RGB LED (blue) blink with the call light until answered (%d on, %d off)", rgbOn, rgbOff);
    CHECK(r214.heardFrom(kOffice) > 40 && r101.heardFrom(kOffice) == 0 && gymA.heardFrom(kOffice) == 0,
          "room call: 214 heard the office (%u frames), 101 and 299 did not", r214.heardFrom(kOffice));
    // 214 answers with TALK within 10 s: only the office hears it.
    r214.held = 1;
    run(50);
    r214.held = 0;
    run(10);
    CHECK(office.replies > 40 && office.lastReplyFrom == r214.id, "214's answer went to the office (%d frames)", office.replies);
    CHECK(r101.heardFrom(r214.id) == 0 && gymA.heardFrom(r214.id) == 0, "nobody else heard the answer");
    CHECK(r214.ic->callAnswered() && r214.ic->rgb() != 0 && r214.ic->lampLevel(Lamp::White) == 0 &&
              r214.ic->lampLevel(Lamp::Other) == 0,
          "answered: white lamp and J11 off, the RGB LED stays on (steady) during the call");
    CHECK(r214.ic->canAnswer(), "no word back from the office yet: 214 can still speak to it");
    officeTarget = 214, officeFlags = 0, officeTalk = 50;  // the office acknowledges
    run(60);
    CHECK(r214.ic->canAnswer() && !r214.ic->chiming(), "the office's word back is part of the same conversation (no new chime)");
    run(100);
    CHECK(!r214.ic->canAnswer(), "the office spoke after 214's answer: the conversation ends 2 s after it stops");
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
    // The office rings 214 (no audio, from the school page's floor plan): chime every 2 s and the attention
    // lights until TALK answers; the answer goes to the office.
    CHECK(r214.ic->command("ring", CmdSource::Network).find("ringing") != std::string::npos, "ring accepted");
    run(3);
    CHECK(r214.ic->chiming() && r214.ic->canAnswer() && !r214.ic->callAnswered(), "ring: 214 chimes, TALK would answer");
    {
        bool again = false;
        for (int k = 0; k < 140; k++) {
            run(1);
            if (k > 60 && r214.ic->chiming()) again = true;
        }
        CHECK(again && !r214.ic->callAnswered(), "ring: it chimes again 2 s later while nobody answers");
    }
    int repliesBefore = office.replies;
    r214.held = 1;
    run(50);
    r214.held = 0;
    run(10);
    CHECK(r214.ic->callAnswered() && office.replies > repliesBefore + 40, "ring answered with TALK: the office hears 214 (%d frames)",
          office.replies - repliesBefore);
    run(520);
    // OFFICE (front button): a press calls the office, another makes it a call for help; held while rung, it answers.
    r214.held = 1u << kBtnOffice;
    run(3);
    r214.held = 0;
    run(10);
    CHECK(r214.ic->callingOffice(), "OFFICE pressed: 214 calls the office");
    r214.held = 1u << kBtnOffice;
    run(3);
    r214.held = 0;
    run(10);
    CHECK(r214.ic->callingOffice() && r214.ic->urgent(), "OFFICE pressed again while it rings: help needed");
    r214.ic->command("callofc off");
    run(10);
    r214.ic->command("ring", CmdSource::Network);
    run(20);
    repliesBefore = office.replies;
    r214.held = 1u << kBtnOffice;
    run(50);
    r214.held = 0;
    run(10);
    CHECK(r214.ic->callAnswered() && office.replies > repliesBefore + 40 && !r214.ic->callingOffice(),
          "rung, OFFICE held: it answers the office (%d frames), no new call", office.replies - repliesBefore);
    run(520);

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

// R3.1: event log, emergency messages, nightly self-check, settings backup, fall-back, the camera rule.
static void testR31() {
    printf("R3.1: event log, emergency, self-check, backup, fall-back, camera rule\n");
    const int64_t clock0 = g_clock0;
    g_clock0 = 1795451390;  // Mon 2026-11-23 16:29:50 UTC at frame 0
    const std::string key = "school-network-key-1";
    auto has = [](FakeUnit& u, const char* text, int kind = -1) {
        for (const auto& e : u.ic->events())
            if (e.text.find(text) != std::string::npos && (kind < 0 || int(e.kind) == kind)) return true;
        return false;
    };
    auto web = [](WebUi& w, const char* method, const std::string& path, const std::string& body = "") {
        HttpRequest q;
        q.method = method;
        q.path = path;
        q.authorization = basic("admin:s3cret-pass");
        q.xIntercom = "1";
        q.body = body;
        return w.handle(q);
    };
    {   // ---- event log and emergency ----
        Bus bus;
        g_bootNote = "power on";
        FakeUnit a(&bus, {101, 0}, 120), b(&bus, {214, 0}, 160);
        g_bootNote.clear();
        std::vector<FakeUnit*> all = {&a, &b};
        auto run = [&](int frames) { for (int k = 0; k < frames; k++) for (auto* u : all) u->step(); };
        a.ic->command("netkey " + key);
        b.ic->command("netkey " + key);
        run(100);
        CHECK(has(a, "room 101 started: power on", 2), "the start and its reason are in the log");
        CHECK(a.ic->known().size() == 1 && versionText(a.ic->known()[0].status.fwVer) == kFirmwareVersion,
              "status packets carry the firmware version (%s)", a.ic->known().empty() ? "?" : versionText(a.ic->known()[0].status.fwVer).c_str());
        b.held = 1;
        run(100);
        b.held = 0;
        run(80);
        CHECK(has(a, "room 214 to everyone", 1) && has(b, "room 214 to everyone", 1), "an all-call is in the call log of the talker and of a listener");
        b.eth = false;
        run(250);
        CHECK(has(a, "room 214 offline", 0), "a unit that goes silent is logged as offline");
        CHECK(has(b, "room 214: network lost", 0) && !has(b, "room 101 offline"), "the unit that lost its network logs that, not every room as offline");
        b.eth = true;
        run(100);
        CHECK(has(a, "room 214 back online", 2), "and as back online");
        b.pw.source = PowerSource::Battery;
        b.pw.hasBattery = true;
        b.pw.batteryPct = 80;
        run(120);
        CHECK(has(a, "room 214 on battery (80 %)", 0) && has(b, "room 214 on battery (80 %)", 0), "a unit on battery is logged, there and at the others");
        b.pw.source = PowerSource::PoE;
        run(120);
        std::string l = a.ic->command("log faults");
        CHECK(l.find("FAULT room 214 offline") != std::string::npos && l.find("to everyone") == std::string::npos &&
                  a.ic->command("log calls").find("room 214 to everyone") != std::string::npos,
              "\"log faults\" and \"log calls\" pick their lines");
        a.ic->command("webpass s3cret-pass");
        WebUi wa(*a.ic);
        HttpResponse r = web(wa, "GET", "/api/log?since=0");
        CHECK(r.status == 200 && r.body.find("room 214 offline") != std::string::npos && r.body.find("\"next\":") != std::string::npos, "the page gets the log as JSON");
        CHECK(web(wa, "GET", "/api/log?since=99999").body.find("room 214 offline") != std::string::npos, "a page with a too-high number (unit restarted) gets it all again");

        CHECK(a.ic->command("emergency lockdown all", CmdSource::Network).find("USB") != std::string::npos && a.ic->emergency() == Intercom::Emergency::None,
              "\"emergency ... all\" is refused from the network");
        b.eth = false;  // 214 is off the network when the emergency starts
        run(20);
        b.lampSeen = 0;
        b.rgbSeen = 0;
        std::string e = a.ic->command("emergency lockdown all", CmdSource::Web);
        CHECK(e.find("sent to all units") != std::string::npos && a.ic->emergency() == Intercom::Emergency::Lockdown, "the office starts a lockdown: %s", e.c_str());
        run(100);
        CHECK(a.ic->speakerLevel() > 50, "the signal plays (level %u)", unsigned(a.ic->speakerLevel()));
        b.eth = true;
        run(300);
        CHECK(b.ic->emergency() == Intercom::Emergency::Lockdown && b.ic->speakerLevel() > 50, "a unit that was off the network at the start still gets it (repeats)");
        bool loc = false;
        for (const auto& s : b.shown) loc = loc || s.compare(0, 3, "LoC") == 0;
        CHECK(loc && (b.lampSeen & 4) && (b.rgbSeen & 0xC00000), "display LoC, alarm light and red LED");
        CHECK(a.ic->known()[0].status.emergency == 1 && (a.ic->known()[0].status.flags & kStatusEmergency) && has(a, "room 214 shows EMERGENCY lockdown"),
              "the status packet and the log show it");
        uint32_t heard0 = b.heardFrom(a.id);
        a.held = 1;
        run(100);
        a.held = 0;
        CHECK(b.heardFrom(a.id) > heard0 + 80, "talking still gets through during an emergency");
        run(3100);
        CHECK(b.ic->speakerLevel() == 0 && b.ic->emergency() == Intercom::Emergency::Lockdown, "after a minute the signal stops, the emergency stays on");
        a.ic->command("emergency clear all");
        run(50);
        CHECK(a.ic->emergency() == Intercom::Emergency::None && b.ic->emergency() == Intercom::Emergency::None && has(b, "emergency cleared"),
              "all clear reaches every unit");
    }
    {   // ---- a recorded message follows the signal ----
        Bus bus;
        FakeUnit c(&bus, {105, 0}, 120);
        c.clipLockdown.assign(16000, 1000);
        for (int k = 0; k < 30; k++) c.step();
        c.ic->command("emergency lockdown");
        for (int k = 0; k < 149; k++) c.step();
        bool signal = c.lastSpk[100] != 1000;
        for (int k = 0; k < 11; k++) c.step();
        CHECK(signal && c.lastSpk[100] == 1000, "the recorded message plays after the 3 s signal");
    }
    {   // ---- console watch: the office unit chirps while no office page reads it ----
        Bus bus;
        FakeUnit o(&bus, {105, 0}, 120), r(&bus, {101, 0}, 130);
        std::vector<FakeUnit*> all = {&o, &r};
        int so = 0, sr = 0;
        auto run = [&](int frames, bool page = false) {
            so = sr = 0;
            for (int k = 0; k < frames; k++) {
                if (page && k % 100 == 0) o.ic->consoleSeen();
                for (auto* u : all) u->step();
                so += o.ic->speakerLevel() > 0;
                sr += r.ic->speakerLevel() > 0;
            }
        };
        for (auto* u : all) u->ic->command("office 105");
        run(3500);
        CHECK(so == 0 && !o.ic->consoleMissing(), "console watch is off until it is set (%d)", so);
        for (auto* u : all) u->ic->command("consolewatch 1");
        CHECK(o.kv["conwatch"] == "1" && o.ic->command("consolewatch 99").find("usage") != std::string::npos, "consolewatch N is saved, a bad time refused");
        run(3100);  // 62 s: three chirps of 0.24 s
        CHECK(o.ic->consoleMissing() && so >= 20 && so <= 60 && sr == 0 && !r.ic->consoleMissing(),
              "no office page for a minute: the office unit chirps every 30 s, a room does not (%d, %d frames)", so, sr);
        run(3200, true);
        CHECK(!o.ic->consoleMissing() && so <= 12, "the page is open again: the chirping stops (%d)", so);
        run(3200);
        CHECK(o.ic->consoleMissing() && so > 0, "the page closed for a minute: it chirps again (%d)", so);
        o.ic->command("emergency lockdown");
        run(600);
        o.ic->command("emergency clear");
        o.ic->command("consolewatch off");
        run(300);  // the all-clear sound
        run(1600);
        CHECK(o.kv["conwatch"].empty() && so == 0 && !o.ic->consoleMissing(), "consolewatch off: silent (%d)", so);
    }
    {   // ---- self-check ----
        Bus bus;
        FakeUnit a(&bus, {101, 0}, 120), b(&bus, {102, 0}, 130), c(&bus, {103, 0}, 140);
        std::vector<FakeUnit*> all = {&a, &b, &c};
        auto run = [&](int frames) { for (int k = 0; k < frames; k++) for (auto* u : all) u->step(); };
        a.loop = c.loop = true;  // 102's microphone hears nothing
        CHECK(c.ic->command("selfcheck 16:31").find("16:31") != std::string::npos && c.kv["selfcheck"] == "16:31" &&
                  c.ic->command("selfcheck 25:00").find("usage") != std::string::npos,
              "selfcheck HH:MM is saved, a bad time refused");
        run(60);
        a.ic->command("selfcheck now");
        b.ic->command("selfcheck now");
        run(100);
        CHECK(!a.ic->audioFault() && has(a, "room 101 self-check passed", 2), "a unit that hears its own tone passes");
        CHECK(!b.ic->audioFault() && !has(b, "FAILED"), "one failed run does not count yet");
        run(1700);
        CHECK(b.ic->audioFault() && has(b, "room 102 self-check FAILED: the microphone did not hear the speaker", 0) &&
                  has(a, "room 102: self-check failed", 0),
              "the second failure 30 s later counts: logged there and at the other units");
        CHECK(!has(c, "room 103 self-check passed"), "the nightly check has not run before its time");
        run(2900);  // 16:31:21 is 91 s after the start
        CHECK(has(c, "room 103 self-check passed", 2), "the nightly check runs at its time (room 103: second 21)");
        b.loop = true;
        b.ic->command("selftest");
        run(100);
        CHECK(!b.ic->audioFault(), "a passing test clears the fault");
    }
    {   // ---- settings backup ----
        Bus bus, bus2;
        FakeUnit a(&bus, {214, 0}, 120), n(&bus2, {7, 0}, 130);
        a.ic->command("netkey " + key);
        a.ic->command("webpass s3cret-pass");
        a.ic->command("sched add 11:30 mon-fri lunch");
        a.ic->command("selfcheck 02:30");
        a.ic->command("group 299");
        a.ic->command("save");
        std::string bk = a.ic->backupText();
        CHECK(bk.find("unit=214\n") != std::string::npos && bk.find("\nsched=") != std::string::npos && bk.find("\nselfcheck=02:30\n") != std::string::npos &&
                  bk.find("\ngroup=299\n") != std::string::npos && bk.find("\ntune=") != std::string::npos,
              "the backup has the room number, schedule, self-check time, group and tuning");
        CHECK(bk.find(key) == std::string::npos && bk.find("pass") == bk.find("passwords"), "no key or password in it");
        n.ic->command("webpass s3cret-pass");
        WebUi wn(*n.ic);
        HttpResponse r = web(wn, "POST", "/api/restore", bk);
        CHECK(r.status == 200 && n.id.room == 214 && n.ic->schedule().entries.size() == 1 && n.ic->group() == 299 && n.kv["selfcheck"] == "02:30" &&
                  n.kv["tune"] == a.kv["tune"],
              "restored on a replacement unit: %s", r.body.c_str());
        CHECK(web(wn, "GET", "/api/backup").body.find("unit=214\n") != std::string::npos, "the page downloads the backup");
        CHECK(n.ic->command("restore selfcheck=03:15", CmdSource::Network) == "ok" && n.kv["selfcheck"] == "03:15" &&
                  n.ic->command("restore netkey=y", CmdSource::Network).find("unknown setting") != std::string::npos && n.id.room == 214,
              "a signed network command restores a setting too (the office page's setup), never a key");
        r = web(wn, "POST", "/api/restore", "webpass=x\nnetkey=y\nunit=abc\n");
        CHECK(r.status == 400 && r.body.find("unknown setting: webpass") != std::string::npos && r.body.find("unit: not a room number") != std::string::npos &&
                  n.kv["netkey"].empty() && n.id.room == 214,
              "passwords, keys and bad values are not taken from a file");
    }
    {   // ---- fall-back ----
        Bus bus, bus2;
        FakeUnit a(&bus, {101, 0}, 120), b(&bus2, {102, 0}, 130);
        a.fwPending = b.fwPending = true;
        b.eth = false;  // the new firmware never gets a network link
        a.ic->command("webpass s3cret-pass");
        WebUi wa(*a.ic);
        for (int k = 0; k < 2990; k++) a.step();
        bool early = a.fwConfirmed != 0 || web(wa, "GET", "/api/fw").body.find("\"pending\":true") == std::string::npos;
        for (int k = 0; k < 20; k++) a.step();
        CHECK(!early && a.fwConfirmed == 1 && !a.fwPending && has(a, "new firmware") &&
                  web(wa, "GET", "/api/fw").body.find("\"pending\":false") != std::string::npos,
              "new firmware is on trial, and confirmed after a minute with a network link");
        for (int k = 0; k < 14990; k++) b.step();
        bool early2 = b.fwRejected != 0;
        for (int k = 0; k < 20; k++) b.step();
        CHECK(!early2 && b.fwRejected == 1 && b.fwConfirmed == 0, "without a link for 5 minutes it goes back to the old firmware");
    }
    {   // ---- the camera is for emergencies only ----
        Bus bus;
        FakeUnit plain(&bus, {101, 0}, 120);
        g_camera = true;
        FakeUnit cam(&bus, {102, 0}, 130);
        g_camera = false;
        plain.ic->command("webpass s3cret-pass");
        cam.ic->command("webpass s3cret-pass");
        WebUi wp(*plain.ic), wc(*cam.ic);
        for (int k = 0; k < 30; k++) { plain.step(); cam.step(); }
        CHECK(web(wp, "GET", "/api/cam.jpg").status == 404, "no camera: 404");
        HttpResponse r = web(wc, "GET", "/api/cam.jpg");
        CHECK(r.status == 403 && !r.camera && !has(cam, "camera viewed"), "no picture outside an emergency");
        cam.ic->command("emergency evacuate");
        r = web(wc, "GET", "/api/cam.jpg");
        CHECK(r.status == 200 && r.camera && has(cam, "camera viewed"), "a picture during one, and the viewing is logged");
        cam.ic->command("emergency clear");
        CHECK(web(wc, "GET", "/api/cam.jpg").status == 403, "and none again after all clear");
    }
    g_clock0 = clock0;
}

// R3.2: the optional features and their switches.
static void testR33Sync() {
    printf("R3.3: office units in order, school-wide settings kept the same on every unit\n");
    const int64_t clock0 = g_clock0;
    g_clock0 = 1795451390;
    auto has = [](FakeUnit& u, const char* text) {
        for (const auto& e : u.ic->events())
            if (e.text.find(text) != std::string::npos) return true;
        return false;
    };
    Bus bus;
    FakeUnit o1(&bus, {105, 0}, 120), o2(&bus, {104, 0}, 130), o3(&bus, {901, 0}, 140), r1(&bus, {101, 0}, 150);
    std::vector<FakeUnit*> all = {&o1, &o2, &o3, &r1};
    auto run = [&](int frames) { for (int k = 0; k < frames; k++) for (auto* u : all) u->step(); };
    for (auto* u : all) u->ic->command("netkey sync-test-key-0123456789");
    run(100);
    // everything is set on the first office unit only: the others get it by themselves
    CHECK(o1.ic->command("office 105 104 901 902 903").find("usage") != std::string::npos && o1.ic->command("office 105 105").find("usage") != std::string::npos,
          "more than four office units, or one twice, is refused");
    o1.ic->command("feature all on");
    o1.ic->command("office 105 104 901");
    o1.ic->command("zone 1 east 101-105");
    o1.ic->command("quiet 18:00 07:00 -12");
    o1.ic->command("sched add 08:30 mon-fri bell");
    o1.ic->command("consolewatch 2");
    run(1500);
    CHECK(o1.ic->syncRev() >= 1 && r1.ic->syncRev() == o1.ic->syncRev() && r1.ic->syncHash() == o1.ic->syncHash() && o2.ic->syncHash() == o1.ic->syncHash() &&
              o3.ic->syncHash() == o1.ic->syncHash(),
          "settings made on the office unit reach every unit by themselves (revision %u)", unsigned(o1.ic->syncRev()));
    CHECK(r1.ic->officeRoom() == 105 && r1.ic->offices().size() == 3 && r1.ic->features() == 255 && r1.kv["zones"] == o1.kv["zones"] &&
              r1.kv["quiet"] == o1.kv["quiet"] && r1.ic->command("sched").find("08:30") != std::string::npos && r1.ic->consoleWatchMin() == 2,
          "a room has the office list, switches, zone, quiet hours, bell and console watch");
    CHECK(r1.ic->actingOffice() == 105 && o2.ic->actingOffice() == 105, "the first office unit acts as the office");
    // the first office unit fails: the second takes over; then the second too: the third
    all = {&o2, &o3, &r1};
    run(400);
    CHECK(r1.ic->actingOffice() == 104 && o2.ic->actingOffice() == 104 && o3.ic->actingOffice() == 104 && has(o2, "room 104 is acting as the office"),
          "105 offline: 104 acts as the office");
    all = {&o3, &r1};
    run(400);
    CHECK(r1.ic->actingOffice() == 901 && o3.ic->actingOffice() == 901, "104 offline too: 901 acts as the office");
    // a setting changed while two office units are off; a room's board is replaced by a new one meanwhile
    o3.ic->command("zone 2 gym 208");
    FakeUnit r1new(&bus, {101, 0}, 160);
    r1new.ic->command("netkey sync-test-key-0123456789");
    all = {&o3, &r1new};
    run(1500);
    CHECK(r1new.ic->syncHash() == o3.ic->syncHash() && r1new.ic->officeRoom() == 105 && r1new.kv["zones"].find("gym") != std::string::npos &&
              r1new.ic->command("sched").find("08:30") != std::string::npos && has(o3, "school settings sent"),
          "a replaced board gets the school's settings from the acting office unit by itself");
    all = {&o1, &o2, &o3, &r1new};
    run(2000);
    CHECK(o1.kv["zones"].find("gym") != std::string::npos && o2.kv["zones"].find("gym") != std::string::npos && o1.ic->syncRev() == o3.ic->syncRev() &&
              o1.ic->syncHash() == o3.ic->syncHash(),
          "the office units that were off come back and take the newer settings, they do not push their old ones");
    CHECK(o1.ic->actingOffice() == 105 && r1new.ic->actingOffice() == 105 && has(o3, "room 105 is the office again"), "105 is back: it is the office again");
    // a brand-new board with the number of the first office unit
    FakeUnit o1new(&bus, {105, 0}, 170);
    o1new.ic->command("netkey sync-test-key-0123456789");
    all = {&o2, &o3, &r1new, &o1new};
    run(2000);
    CHECK(o1new.ic->syncHash() == o2.ic->syncHash() && o1new.ic->offices().size() == 3 && o1new.ic->actingOffice() == 105 && r1new.ic->actingOffice() == 105,
          "the first office unit replaced by a new board: the stand-in fills it, then it is the office again");
    g_clock0 = clock0;
}

static void testR32() {
    printf("R3.2: optional features (reason, queue, zones, dnd, quiet hours, silent help, office stand-in)\n");
    const int64_t clock0 = g_clock0;
    g_clock0 = 1795451390;  // Mon 2026-11-23 16:29:50 UTC at frame 0
    auto has = [](FakeUnit& u, const char* text, int kind = -1) {
        for (const auto& e : u.ic->events())
            if (e.text.find(text) != std::string::npos && (kind < 0 || int(e.kind) == kind)) return true;
        return false;
    };
    auto showed = [](FakeUnit& u, const char* text, uint8_t dot = 0) {
        for (const auto& s : u.shown)
            if (s.compare(0, 3, text) == 0 && (uint8_t(s[3]) & dot) == dot) return true;
        return false;
    };
    const uint8_t kOffice = uint8_t(1u << kBtnOffice);
    Bus bus;
    FakeUnit ofc(&bus, {105, 0}, 120), prin(&bus, {104, 0}, 130), r1(&bus, {101, 0}, 140), r2(&bus, {214, 0}, 150);
    std::vector<FakeUnit*> all = {&ofc, &prin, &r1, &r2};
    auto run = [&](int frames) { for (int k = 0; k < frames; k++) for (auto* u : all) u->step(); };
    run(60);

    // ---- the switches ----
    CHECK(ofc.ic->features() == 0xa1, "a new unit has help needed, the silent help call and the red button switched on (%u)", unsigned(ofc.ic->features()));
    for (auto* u : all) u->ic->command("feature all off");
    CHECK(ofc.ic->features() == 0 && r1.ic->command("dnd 10").find("feature is off") != std::string::npos &&
              r1.ic->command("callofc help").find("feature is off") != std::string::npos &&
              r1.ic->command("callofc silent").find("feature is off") != std::string::npos,
          "with every feature off, each command says so");
    r1.held = kOffice;
    run(300);
    r1.held = 0;
    run(30);
    CHECK(r1.ic->callingOffice() && !r1.ic->urgent() && !r1.ic->silentHelp(), "with the features off a long OFFICE press is a plain call");
    r1.ic->command("callofc off");
    for (auto* u : all) {
        u->ic->command("feature all on");
        u->ic->command("office 105 104");
    }
    CHECK(r1.kv["features"] == "255" && r1.ic->command("feature").find("silent=on") != std::string::npos &&
              r1.ic->command("feature bogus on").find("usage") != std::string::npos,
          "feature all on is saved; an unknown name is refused");
    CHECK(r1.ic->command("feature queue off", CmdSource::Web).find("queue=off") != std::string::npos &&
              r1.ic->command("feature queue on", CmdSource::Network).find("queue=on") != std::string::npos,
          "a feature can be switched from the web page and by a network command");
    run(300);

    // ---- a reason for the call, and the queue ----
    r2.held = kOffice;
    run(10);
    r2.held = 0;
    run(60);
    CHECK(r2.ic->callingOffice() && !r2.ic->urgent(), "a short OFFICE press is a question");
    r1.held = kOffice;
    run(100);
    r1.held = 0;
    run(60);
    CHECK(r1.ic->callingOffice() && r1.ic->urgent() && has(ofc, "room 101: HELP NEEDED", 0) && showed(r1, "\xf6LP"),
          "OFFICE held 1.5 s is \"help needed\": display HLP, logged at the office as a fault");
    auto q = ofc.ic->callQueue();
    CHECK(q.size() == 2 && q[0].room == 101 && q[0].urgent && q[1].room == 214 && !q[1].urgent, "the queue puts help first, though it came later");
    CHECK(showed(ofc, "101", 4), "the office unit shows the first waiting room");
    ofc.ic->command("webpass s3cret-pass");
    WebUi wo(*ofc.ic);
    HttpRequest sq;
    sq.method = "GET";
    sq.path = "/api/school";
    sq.authorization = basic("admin:s3cret-pass");
    std::string sj = wo.handle(sq).body;
    CHECK(sj.find("\"queue\":[{\"room\":101,\"urgent\":true") != std::string::npos && sj.find("\"features\":255") != std::string::npos &&
              sj.find("\"office\":105,\"standin\":104,\"acting\":105") != std::string::npos,
          "the school page gets the queue, the switches and the office");
    ofc.held = 1;
    run(100);
    ofc.held = 0;
    run(60);
    CHECK(r1.heardFrom(ofc.id) > 80 && r2.heardFrom(ofc.id) == 0 && !r1.ic->callingOffice() && r2.ic->callingOffice(),
          "the office unit's TALK answers the first waiting room only (101 heard %u frames, 214 %u)", r1.heardFrom(ofc.id), r2.heardFrom(ofc.id));
    r2.ic->command("callofc off");
    run(600);  // the answer windows close

    // ---- zones ----
    for (auto* u : all) u->ic->command("zone 1 east 101-103,214");
    CHECK(r1.ic->command("zone").find("this room is in it") != std::string::npos && prin.ic->command("zone").find("this room") == std::string::npos &&
              r1.ic->command("zone 9 x 1").find("usage") != std::string::npos && r1.ic->command("zone 2 bad;name 1").find("usage") != std::string::npos,
          "zone 1 east 101-103,214 is saved; bad zones are refused");
    uint32_t h1 = r1.heardFrom(ofc.id), h2 = r2.heardFrom(ofc.id), hp = prin.heardFrom(ofc.id);
    ofc.ic->command("talk 2 zone 1");
    run(160);
    CHECK(r1.heardFrom(ofc.id) > h1 + 80 && r2.heardFrom(ofc.id) > h2 + 80 && prin.heardFrom(ofc.id) == hp, "a zone call reaches the zone's rooms only");
    r2.ic->command("feature zones off");
    h2 = r2.heardFrom(ofc.id);
    ofc.ic->command("talk 2 zone 1");
    run(160);
    CHECK(r2.heardFrom(ofc.id) == h2, "a unit with zones switched off ignores zone calls");
    r2.ic->command("feature zones on");
    run(600);

    // ---- do not disturb ----
    CHECK(r2.ic->command("dnd 30").find("30 min left") != std::string::npos && r2.ic->command("dnd 999").find("usage") != std::string::npos, "dnd 30");
    run(60);
    h2 = r2.heardFrom(r1.id);
    hp = prin.heardFrom(r1.id);
    r1.held = 1;
    run(100);
    r1.held = 0;
    run(60);
    CHECK(r2.heardFrom(r1.id) == h2 && prin.heardFrom(r1.id) > hp + 80, "a room in do-not-disturb does not play another room's call");
    h2 = r2.heardFrom(ofc.id);
    ofc.held = 1;
    run(100);
    ofc.held = 0;
    run(60);
    CHECK(r2.heardFrom(ofc.id) > h2 + 80, "the office still gets through");
    r2.ic->command("emergency lockdown");
    run(20);
    CHECK(r2.ic->speakerLevel() > 50, "and so does an emergency");
    r2.ic->command("emergency clear");
    CHECK(showed(r2, "dnd") && has(ofc, "room 214: do not disturb", 2) && (ofc.ic->known().size() == 3), "display dnd; the office log has it");
    r2.ic->command("dnd off");
    run(200);

    // ---- quiet hours (the clock reads 16:3x) ----
    CHECK(r1.ic->command("quiet 16:00 17:00 -20").find("-20 dB") != std::string::npos && r1.ic->command("quiet 16:00 17:00 5").find("usage") != std::string::npos,
          "quiet 16:00 17:00 -20");
    run(60);
    r1.ic->command("play bell");
    prin.ic->command("play bell");
    run(20);
    int diff = int(prin.ic->speakerLevel()) - int(r1.ic->speakerLevel());
    CHECK(r1.ic->quietDb() == -20 && diff >= 18 && diff <= 22, "inside quiet hours the bell is 20 dB down (%d dB)", diff);
    run(150);
    r1.ic->command("emergency evacuate");
    prin.ic->command("emergency evacuate");
    run(20);
    diff = int(prin.ic->speakerLevel()) - int(r1.ic->speakerLevel());
    CHECK(diff >= -1 && diff <= 1 && r1.ic->speakerLevel() > 50, "the emergency signal is not turned down (%d dB)", diff);
    r1.ic->command("emergency clear");
    prin.ic->command("emergency clear");
    r1.ic->command("quiet 22:00 07:00 -12");
    run(120);
    CHECK(r1.ic->quietDb() == 0, "outside quiet hours (22:00 to 07:00, over midnight) nothing is turned down");

    // ---- silent help call ----
    uint32_t ho = ofc.heardFrom(r2.id), hpr = prin.heardFrom(r2.id);
    const unsigned allBefore = r1.heardFrom(r2.id);
    r2.held = kOffice;  // the two fingers do not land together: OFFICE first, TALK 0.1 s later
    run(5);
    r2.held = 1 | kOffice;
    run(40);
    CHECK(!r2.ic->silentHelp() && !r2.ic->callingOffice() && r2.ic->speakerLevel() == 0, "TALK and OFFICE together: nothing in the first second, no ring");
    run(20);
    r2.held = 1;        // and they do not leave together either
    run(5);
    r2.held = 0;
    run(100);
    CHECK(r2.ic->shutter() == Intercom::Shutter::Open && r2.shutUs == 1500 && r2.shutSlow && r1.ic->shutter() == Intercom::Shutter::Closed && r1.shutUs == 600,
          "silent help call: that room's camera shutter opens slowly, without the flag; other rooms stay closed");
    CHECK(r2.ic->silentHelp() && !r2.ic->callingOffice() && has(ofc, "room 214: SILENT HELP CALL", 0) && r1.heardFrom(r2.id) == allBefore,
          "TALK and OFFICE held 1 s is a silent help call, logged at the office; no all-call went out");
    q = ofc.ic->callQueue();
    CHECK(q.size() == 1 && q[0].room == 214 && q[0].silent, "it is first in the office queue");
    CHECK(ofc.heardFrom(r2.id) > ho + 80 && prin.heardFrom(r2.id) == hpr, "the office unit hears the room, the other units do not (%u frames)",
          ofc.heardFrom(r2.id) - ho);
    h2 = r2.heardFrom(ofc.id);
    ofc.held = 1;  // the office answers by reflex
    run(100);
    ofc.held = 0;
    run(30);
    CHECK(r2.heardFrom(ofc.id) == h2 && r2.ic->speakerLevel() == 0, "the room stays silent even when the office answers");
    r2.ic->command("callofc off", CmdSource::Network);
    run(60);
    CHECK(!r2.ic->silentHelp() && ofc.ic->callQueue().empty(), "the office clears it");
    run(600);

    // ---- the stand-in office ----
    r1.held = kOffice;
    run(10);
    r1.held = 0;
    ofc.eth = false;  // the office unit fails
    run(400);
    CHECK(r1.ic->actingOffice() == 104 && prin.ic->actingOffice() == 104 && has(r1, "room 104 is acting as the office (room 105 is offline)", 0),
          "with the office unit offline its stand-in acts as the office");
    CHECK(showed(prin, "101", 4), "the stand-in shows the waiting room");
    uint32_t hs = r1.heardFrom(prin.id);
    prin.held = 1;
    run(100);
    prin.held = 0;
    run(60);
    CHECK(r1.heardFrom(prin.id) > hs + 80 && !r1.ic->callingOffice(), "its TALK answers the waiting room, and the call is done");
    ofc.eth = true;
    run(400);
    CHECK(r1.ic->actingOffice() == 105 && has(r1, "room 105 is the office again", 2), "the office unit takes over again when it is back");
    prin.ic->command("feature failover off");
    r1.ic->command("feature failover off");
    ofc.eth = false;
    run(300);
    CHECK(r1.ic->actingOffice() == 105, "with failover switched off nobody takes over");

    std::string bk = r1.ic->backupText();
    CHECK(bk.find("\nfeatures=") != std::string::npos && bk.find("\noffice=105 104\n") != std::string::npos &&
              bk.find("\nzones=1=east:101-103,214;\n") != std::string::npos && bk.find("\nquiet=22:00 07:00 -12\n") != std::string::npos,
          "the settings backup carries the switches, office, zones and quiet hours");
    // ---- lockdown from a room: the red button (feature "lockdown") ----
    for (auto* u : all) {
        u->eth = true;  // the office unit was taken off the network above
        u->ic->command("feature lockdown on");
        u->ic->command("emergency clear");
        u->ic->command("callofc off");
        u->ic->command("netkey school-network-key-1");
        u->held = 0;
    }
    run(100);
    const auto L = Intercom::Emergency::Lockdown, N = Intercom::Emergency::None;
    const uint8_t kRed = uint8_t(1u << kBtnRed);
    r1.held = kRed;
    run(3);
    CHECK(r1.ic->emergency() == L && (r1.ic->statusFlags2() & kStatus2Red), "the red button: lockdown in that room at once");
    r1.held = 0;
    run(300);
    CHECK(r2.ic->shutter() == Intercom::Shutter::Flag && r2.shutUs == 2400 && !r2.shutSlow && has(r2, "camera shutter open, flag out"),
          "lockdown: the camera shutter goes all the way out, fast (%u us)", r2.shutUs);
    CHECK(r2.ic->emergency() == L && ofc.ic->emergency() == L && prin.ic->emergency() == L && !(r2.ic->statusFlags2() & kStatus2Red),
          "and on every unit (%d %d %d); only the room that pressed says so", int(r2.ic->emergency()), int(ofc.ic->emergency()), int(prin.ic->emergency()));
    r2.held = kRed;
    run(20);
    r2.held = 0;
    prin.held = kRed;
    run(20);
    prin.held = 0;
    run(150);
    CHECK(r2.ic->emergency() == L && ofc.ic->emergency() == L && prin.ic->emergency() == L, "no intercom can clear it, not even an office unit");
    ofc.ic->command("emergency clear all", CmdSource::Web);
    run(300);
    CHECK(r2.ic->shutter() == Intercom::Shutter::Closed && r2.shutUs == 600 && r2.shutSlow, "all clear: the shutter closes again, slowly (%u us)", r2.shutUs);
    CHECK(r2.ic->command("shutterus 700 1400 2300") == "shutterus 700 1400 2300" && r2.ic->command("shutter open") == "shutter open" &&
              r2.ic->command("shutter flag", CmdSource::Network).find("only works") != std::string::npos,
          "the servo's positions can be set, and the shutter moved by hand (not over the network)");
    run(3);
    CHECK(r2.shutUs == 1400 && r2.ic->shutter() == Intercom::Shutter::Open, "by hand: open (%u us)", r2.shutUs);
    r2.ic->command("shutter auto");
    r2.ic->command("shutterus 600 1500 2400");
    run(3);
    CHECK(r1.ic->emergency() == N && r2.ic->emergency() == N && ofc.ic->emergency() == N && prin.ic->emergency() == N && !(r1.ic->statusFlags2() & kStatus2Red),
          "All clear on the office page ends it everywhere");
    r1.ic->command("feature lockdown off");
    r1.held = kRed;
    run(20);
    r1.held = 0;
    run(100);
    CHECK(r1.ic->emergency() == N && ofc.ic->emergency() == N, "with the feature off the red button does nothing");
    r1.ic->command("feature lockdown on");
    for (auto* u : all) u->ic->command("callofc off");

    // ---- a plain call to the office rings both ends for 10 s, then gives up; a call for help stays ----
    for (auto* u : all) {
        u->ic->command("emergency clear");
        u->held = 0;
    }
    run(700);
    r2.held = kOffice;
    run(5);
    r2.held = 0;
    run(7);  // inside the first warble (0.35 s)
    CHECK(r2.ic->callingOffice() && r2.ic->speakerLevel() > 0, "214 presses OFFICE: its own unit rings (%d)", int(r2.ic->speakerLevel()));
    CHECK(ofc.ic->officeAlert() == 214 && prin.ic->officeAlert() == 214 && r1.ic->officeAlert() == 0 && ofc.ic->speakerLevel() > 0 &&
              r1.ic->speakerLevel() == 0,
          "the office unit and its stand-in ring for room 214; another room does not");
    {
        int on = 0, off = 0;
        for (int k = 0; k < 60; k++) {
            run(1);
            (ofc.ic->lampLevel(Lamp::White) ? on : off)++;
        }
        CHECK(on > 15 && off > 15 && showed(ofc, "214"), "the office unit flashes its light and shows 214 (%d on, %d off)", on, off);
    }
    run(460);
    CHECK(!r2.ic->callingOffice() && ofc.ic->officeAlert() == 0 && prin.ic->officeAlert() == 0 && ofc.ic->lampLevel(Lamp::White) == 0 &&
              has(r2, "called the office: no answer"),
          "nobody answered in 10 s: the room and the office units are back to normal");
    r2.held = kOffice;
    run(90);
    r2.held = 0;
    run(600);
    CHECK(r2.ic->callingOffice() && r2.ic->urgent() && ofc.ic->officeAlert() == 0, "a call for help (OFFICE held) stays up after the ringing stops");
    run(900);
    CHECK(ofc.ic->officeAlert() == 214 && prin.ic->officeAlert() == 214, "nobody answered it: the office units ring again after 30 s");
    r2.ic->command("callofc off");
    run(20);
    g_clock0 = clock0;
}

int main() {
    testBootLightTest();
    testTuning();
    testAutoTune();
    testConsole();
    testWeb();
    testFirmwareUpdate();
    testR31();
    testR32();
    testR33Sync();
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
