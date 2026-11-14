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
    CHECK(!strcmp(ip, "10.42.20.14"), "room 214 -> %s", ip);
    formatIp(UnitId{214, 1}, ip);
    formatUnit(UnitId{214, 1}, u);
    CHECK(!strcmp(ip, "10.42.21.14") && !strcmp(u, "214a"), "room %s -> %s", u, ip);
    formatIp(UnitId{300, 0}, ip);
    CHECK(!strcmp(ip, "10.42.30.100"), "room 300 -> %s (no .0 host)", ip);

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
static AecResult runAec(sim::RoomPreset preset, int taps) {
    sim::RoomEcho room;
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
}

// ---------------------------------------------------------------------------------
// In-memory network: every send() is delivered to every unit (like multicast with loopback).
struct Bus {
    std::vector<std::deque<std::vector<uint8_t>>*> queues;
};

struct FakeUnit : INet, ILinks, IPower, IPanel, IStore, ILog {
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

    FakeUnit(Bus* b, UnitId u, float f0) : bus(b), id(u), voice(f0, u.room) {
        bus->queues.push_back(&rx);
        ic.reset(new Intercom(Hal{this, this, this, this, this, this}, aec));
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
    PowerState read() override { return PowerState(); }
    uint8_t buttons() override { return held; }
    void show(const char*, uint8_t) override {}
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
    FakeUnit r101(&bus, {101, 0}, 120), r214(&bus, {214, 0}, 160), gymA(&bus, {150, 1}, 190), gymB(&bus, {150, 2}, 230);
    std::vector<FakeUnit*> all = {&r101, &r214, &gymA, &gymB};
    auto run = [&](int frames) { for (int k = 0; k < frames; k++) for (auto* u : all) u->step(); };

    run(20);  // Ethernet settles
    CHECK(r101.used == LinkType::Ethernet, "units come up on Ethernet");
    // 101 and gym A talk at the same time for 2 s.
    r101.held = gymA.held = 1;
    run(100);
    r101.held = gymA.held = 0;
    run(20);
    CHECK(r214.heardFrom(r101.id) > 90 && r214.heardFrom(gymA.id) > 90, "214 heard 101 (%u frames) and 150a (%u) at once",
          r214.heardFrom(r101.id), r214.heardFrom(gymA.id));
    CHECK(gymB.heardFrom(r101.id) > 90 && gymB.heardFrom(gymA.id) == 0,
          "150b heard 101, not 150a in the same room (%u)", gymB.heardFrom(gymA.id));
    CHECK(r101.heardFrom(r101.id) == 0, "nobody plays their own voice");
    CHECK(r214.ic->peers().size() == 3, "214 sees %zu peers via status", r214.ic->peers().size());

    // Portable unit 214 is unplugged: Wi-Fi was already associated.
    r214.wifi = true;
    run(5);
    r214.eth = false;
    run(1);
    CHECK(r214.used == LinkType::WiFi, "214 unplugged -> Wi-Fi within 1 frame (20 ms)");
    r101.held = 1;
    run(50);
    r101.held = 0;
    run(10);
    CHECK(r214.heardFrom(r101.id) > 130, "214 still hears 101 over Wi-Fi (%u frames total)", r214.heardFrom(r101.id));
    r214.eth = true;
    run(15);
    CHECK(r214.used == LinkType::Ethernet, "214 plugged back in -> Ethernet after 200 ms debounce");
    for (auto& l : r214.logs) printf("      214 log: %s\n", l.c_str());

    // Room-number setup on the panel: hold MODE 3 s, set 3-1-5, sub 'a'.
    FakeUnit& u = r101;
    auto press = [&](uint8_t bit, int frames) { u.held = bit; run(frames); u.held = 0; run(3); };
    press(1 << kBtnMode, 155);
    CHECK(u.ic->inSetup(), "MODE held 3 s enters setup");
    press(1 << kBtnUp, 2); press(1 << kBtnUp, 2);  // 1 -> 3
    press(1 << kBtnMode, 2);
    press(1 << kBtnMode, 2);  // 0 stays... next digit
    for (int i = 0; i < 4; i++) press(1 << kBtnUp, 2);  // 1 -> 5
    press(1 << kBtnMode, 2);
    press(1 << kBtnUp, 2);  // sub '-' -> 'a'
    press(1 << kBtnMode, 2);
    char s[8];
    formatUnit(u.ic->unit(), s);
    CHECK(!u.ic->inSetup() && !strcmp(s, "305a"), "panel setup saved unit %s", s);
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

    struct Case { uint8_t code; Button b; const char* name; };
    const Case cases[] = {{0xF7, kBtnTalk, "PB1 TALK"}, {0xEF, kBtnMode, "PB2 MODE"}, {0xF5, kBtnUp, "PB4 UP"}, {0xEE, kBtnDown, "PB5 DOWN"}};
    for (const Case& c : cases) {
        chip.keyCode = c.code;
        uint8_t got = 0;
        bool r = tm.readKeys(&got);
        CHECK(r && got == c.code && board::buttonsFromKey(got) == (1u << c.b), "key scan 0x%02X -> %s", got, c.name);
    }
    chip.keyCode = 0xFF;
    uint8_t got = 0;
    tm.readKeys(&got);
    CHECK(got == 0xFF && board::buttonsFromKey(got) == 0, "no key -> no buttons");

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

    HttpResponse r = web.handle(req);
    CHECK(r.status == 401 && r.askLogin && r.realm == "Intercom 214", "no login: 401 asks for the password");
    req.authorization = basic("admin:wrong-pass");
    CHECK(web.handle(req).status == 401, "wrong password refused");
    req.authorization = basic("admin:s3cret-pass");
    r = web.handle(req);
    CHECK(r.status == 200 && r.body.find("<html") != std::string::npos, "right password: page served (%zu bytes)", r.body.size());

    req.path = "/api/status";
    r = web.handle(req);
    CHECK(r.status == 200 && r.body.find("\"unit\":\"214\"") != std::string::npos && r.body.find("\"ip\":\"10.42.20.14\"") != std::string::npos &&
              r.body.find("\"key\":\"mu\"") != std::string::npos && r.body.find("\"unit\":\"101\"") != std::string::npos,
          "status JSON has unit, IP, settings and peers");

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

int main() {
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
    testAec();
    printf(g_fail ? "\n%d FAILED\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
