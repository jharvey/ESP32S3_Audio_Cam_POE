// Headless checks of the intercom core: packets, jitter buffer, mixer, echo canceller
// (against simulated classroom and gym echo), and several units talking over an
// in-memory network, including Ethernet -> Wi-Fi failover.
//   ic-selftest            run all, exit code 0 = pass
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "Intercom.h"
#include "NlmsAec.h"
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

int main() {
    testPackets();
    testJitter();
    testMixer();
    testIntercom();
    testAec();
    printf(g_fail ? "\n%d FAILED\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
