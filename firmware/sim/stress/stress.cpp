// ic-stress: chaos and fuzz tests of the intercom core. Build it with -fsanitize=address,undefined.
//
//   fuzz 1  packet decoders: random and mutated packets (no crash, no out-of-range read)
//   fuzz 2  the command line from USB, the network and the web, and the web server: random tokens
//   chaos   a whole school (35 units) on a network that loses, repeats, delays and corrupts packets, while
//           buttons are pressed at random, links drop, units reboot and random commands arrive. Then the
//           chaos stops and every unit must come back to rest, and a call must still reach everyone.
//
//   ic-stress [--seed N] [--seeds K] [--seconds S] [--fuzz N]      exit code 0 = pass
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Auth.h"
#include "Intercom.h"
#include "NlmsAec.h"
#include "Packet.h"
#include "TestVoice.h"
#include "WebUi.h"

using namespace ic;

static int g_fail = 0;
static void fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("  [FAIL] ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    g_fail++;
}

using Rng = std::mt19937;
static bool chance(Rng& r, double p) { return std::uniform_real_distribution<double>(0, 1)(r) < p; }
static int pick(Rng& r, int n) { return n > 0 ? int(r() % uint32_t(n)) : 0; }

// --------------------------------------------------------------------------------- network
struct Net {
    struct Item {
        int due;
        std::vector<uint8_t> data;
    };
    std::vector<std::deque<Item>*> queues;
    Rng rng{1};
    double loss = 0, dup = 0, corrupt = 0, delay = 0;
    int now = 0;
    std::vector<std::vector<uint8_t>> old;  // for replays

    void deliver(std::deque<Item>* q, std::vector<uint8_t> d) {
        if (chance(rng, loss)) return;
        if (chance(rng, corrupt) && !d.empty()) {
            int how = pick(rng, 4);
            if (how == 0) d[size_t(pick(rng, int(d.size())))] ^= uint8_t(1u << pick(rng, 8));
            else if (how == 1) d.resize(size_t(pick(rng, int(d.size()))));
            else if (how == 2) d.resize(d.size() + size_t(pick(rng, 40)), uint8_t(rng()));
            else for (auto& b : d) if (chance(rng, 0.05)) b = uint8_t(rng());
        }
        int due = now + (chance(rng, delay) ? 1 + pick(rng, 6) : 0);
        q->push_back({due, d});
        if (chance(rng, dup)) q->push_back({due + pick(rng, 3), d});
        if (q->size() > 4000) q->pop_front();  // a real socket buffer is finite too
    }
    void send(const uint8_t* p, size_t n) {
        std::vector<uint8_t> d(p, p + n);
        for (auto* q : queues) deliver(q, d);
        if (old.size() < 200) old.push_back(d);
        else if (chance(rng, 0.02)) old[size_t(pick(rng, 200))] = d;
    }
    void garbage() {
        std::vector<uint8_t> d(size_t(pick(rng, int(kMaxPacketBytes) + 30)));
        for (auto& b : d) b = uint8_t(rng());
        if (!d.empty() && chance(rng, 0.7)) d[0] = uint8_t(pick(rng, 8));  // plausible type byte
        for (auto* q : queues) deliver(q, d);
    }
    void replay() {
        if (old.empty()) return;
        auto d = old[size_t(pick(rng, int(old.size())))];
        for (auto* q : queues) deliver(q, d);
    }
};

// --------------------------------------------------------------------------------- one unit
struct Unit : INet, ILinks, IPower, IPanel, IStore, ILog, ILamps, IDiag, IFirmware {
    // the spare firmware slot: nothing may be written outside begin..finish or past the declared size
    size_t fwSize = 0, fwGot = 0;
    bool fwOpen = false;
    int fwCommits = 0;
    bool begin(size_t size) override { fwSize = size; fwGot = 0; fwOpen = true; return true; }
    bool write(const uint8_t*, size_t n) override {
        if (!fwOpen || n > fwSize - fwGot) { fprintf(stderr, "firmware write outside an update or past its size\n"); abort(); }
        fwGot += n;
        return true;
    }
    bool finish(bool commit) override {
        if (commit && (!fwOpen || fwGot != fwSize)) { fprintf(stderr, "incomplete firmware committed\n"); abort(); }
        fwOpen = false;
        if (commit) fwCommits++;
        return true;
    }
    Net* net;
    std::deque<Net::Item> rx;
    UnitId id, id0;
    std::map<std::string, std::string> kv;  // "flash": survives reboots
    bool eth = true, wifi = false;
    LinkType used = LinkType::None;
    uint8_t held = 0;
    PowerState pw;
    uint8_t outFaults = 0;
    int restarts = 0, boots = 0;
    bool restartAsked = false;
    bool webTalk = false;
    std::unique_ptr<NlmsAec> aec;
    std::unique_ptr<Intercom> ic;
    std::unique_ptr<WebUi> web;
    sim::TestVoice voice;
    int16_t spk[kFrameSamples] = {0};
    size_t logBytes = 0;

    Unit(Net* n, UnitId u, float f0) : net(n), id(u), id0(u), voice(f0, u.room) {
        net->queues.push_back(&rx);
        kv["boottest"] = "off";
        boot();
    }
    void boot() {
        web.reset();
        ic.reset();
        aec.reset(new NlmsAec(256, 0));
        IntercomOptions opt;
        opt.unitUid = 0xA0000000u + id0.room;
        opt.utcNow = [this] { return int64_t(1795000000) + net->now / 50; };
        opt.restart = [this](bool) { restarts++; restartAsked = true; };
        opt.runBackground = [](std::function<void()> f) { f(); };
        used = LinkType::None;
        held = 0;
        webTalk = false;
        ic.reset(new Intercom(Hal{this, this, this, this, this, this, this, this, this}, *aec, opt));
        ic->begin();
        web.reset(new WebUi(*ic));
        boots++;
    }
    void send(const uint8_t* d, size_t n) override {
        if (used == LinkType::None) return;
        if (used == LinkType::Ethernet && !eth) return;
        net->send(d, n);
    }
    size_t recv(uint8_t* buf, size_t cap) override {
        for (auto it = rx.begin(); it != rx.end(); ++it) {
            if (it->due > net->now) continue;
            size_t n = std::min(cap, it->data.size());
            if (n) memcpy(buf, it->data.data(), n);
            rx.erase(it);
            if (!n) return recv(buf, cap);  // 0 means "nothing waiting": skip empty datagrams
            return n;
        }
        return 0;
    }
    bool ethernetUp() override { return eth; }
    bool wifiUp() override { return wifi; }
    void useLink(LinkType l) override { used = l; }
    PowerState read() override { return pw; }
    uint8_t buttons() override { return held; }
    void show(const char*, uint8_t) override {}
    void set(Lamp, uint8_t) override {}
    UnitId loadUnit() override { return id; }
    void saveUnit(const UnitId& u) override { id = u; }
    void log(const char* m) override { logBytes += strlen(m); }
    std::map<std::string, std::string> blobs;
    bool hasBlobs() const override { return true; }
    bool saveBlob(const char* n, const std::string& d) override { blobs[n] = d; return true; }
    bool loadBlob(const char* n, std::string* o) override { *o = blobs[n]; return !o->empty(); }
    std::string loadText(const char* k) override { return kv[k]; }
    void saveText(const char* k, const std::string& v) override { kv[k] = v; }
    std::string boardTest() override { return "ok fake board\n"; }
    uint8_t outputFaults() override { return outFaults; }

    void step() {
        int16_t mic[kFrameSamples];
        bool talk = (held & 1) || webTalk;
        if (talk) voice.render(mic, kFrameSamples);
        else memset(mic, 0, sizeof(mic));
        if (webTalk) {  // the browser's microphone, not the unit's
            ic->webTalk(mic, kFrameSamples);
            if (!(held & 1)) memset(mic, 0, sizeof(mic));
        }
        ic->processFrame(mic, spk);
        if (restartAsked) {
            restartAsked = false;
            boot();
        }
    }
    bool silent() const {
        for (int16_t s : spk) if (s) return false;
        return true;
    }
};

// --------------------------------------------------------------------------------- random commands
static const char* kWords[] = {
    "boottest", "cal", "callofc", "couple", "dayoff", "defaults", "get", "group", "help", "lamp", "load", "mute", "netkey",
    "ntp", "feature", "office", "zone", "dnd", "quiet", "silent", "help", "reason", "queue", "zones", "failover", "log", "selfcheck", "emergency", "backup", "restore", "play", "reboot", "ring", "save", "sched", "selftest", "send", "set", "skip", "stats", "talk", "testpass", "tests",
    "time", "tone", "tz", "undo", "unit", "version", "webpass", "wifi", "netupdate", "run", "open", "closed", "off", "on", "all", "floor",
    "room", "add", "del", "list", "clear", "bell", "chime", "alarm", "white", "ir", "other", "rgb", "download", "opus", "vol",
    "gain", "taps", "mu", "nlp", "duck", "-", "?", "status", "stop", "map", "show", "now", "mon", "tue", "sat", "daily",
    "0", "1", "2", "5", "50", "100", "101", "105", "214", "299", "999", "65535", "65536", "-1", "4294967296", "1e9", "0x10",
    "12:30", "25:99", "2026-11-25", "214a", "214z", "3.5", "-40", "nan", "inf", "x", "ssid", "password", "k3y", "%s%s%n", "\"", ";"};
static std::string randomCommand(Rng& r) {
    std::string s;
    int n = 1 + pick(r, 6);
    if (chance(r, 0.02)) n = 200;  // a very long line
    for (int i = 0; i < n; i++) {
        if (i) s += chance(r, 0.05) ? "\t" : " ";
        if (chance(r, 0.06)) {
            int len = pick(r, chance(r, 0.1) ? 900 : 12);
            for (int k = 0; k < len; k++) s += char(chance(r, 0.8) ? 33 + pick(r, 94) : pick(r, 256));
        } else if (chance(r, 0.15)) {
            s += std::to_string(int(r() % 400) - 20);
        } else {
            s += kWords[pick(r, int(sizeof(kWords) / sizeof(kWords[0])))];
        }
    }
    return s;
}

// --------------------------------------------------------------------------------- fuzz
static void fuzzDecoders(Rng& r, long n) {
    printf("fuzz: packet decoders, %ld packets\n", n);
    uint8_t buf[kMaxPacketBytes + 64];
    int16_t pcm[kFrameSamples];
    long okA = 0, okS = 0, okT = 0;
    for (long k = 0; k < n; k++) {
        size_t len;
        int kind = pick(r, 4);
        if (kind == 0) {  // random bytes
            len = size_t(pick(r, int(sizeof(buf))));
            for (size_t i = 0; i < len; i++) buf[i] = uint8_t(r());
        } else {  // a valid packet, then damaged
            if (kind == 1) {
                AudioHeader h;
                h.from = {uint16_t(r()), uint8_t(r())};
                h.seq = uint16_t(r());
                h.target = uint16_t(r());
                h.flags = uint8_t(r());
                for (auto& s : pcm) s = int16_t(r());
                len = encodeAudio(h, pcm, buf);
            } else if (kind == 2) {
                StatusPacket s;
                s.from = {uint16_t(r()), uint8_t(r())};
                s.uptimeS = r();
                s.group = uint16_t(r());
                len = encodeStatus(s, buf);
            } else {
                TextPacket t;
                t.type = chance(r, 0.5) ? PacketType::Command : PacketType::Reply;
                t.text = randomCommand(r);
                len = encodeText(t, buf);
            }
            int dmg = pick(r, 5);
            for (int d = 0; d < dmg && len; d++) buf[size_t(pick(r, int(len)))] = uint8_t(r());
            if (chance(r, 0.3)) len = size_t(pick(r, int(len) + 1));
            if (chance(r, 0.1)) len = std::min(sizeof(buf), len + size_t(pick(r, 60)));
        }
        // exact-size heap copy, so the address sanitizer sees any read past the end
        std::unique_ptr<uint8_t[]> p(new uint8_t[len ? len : 1]);
        memcpy(p.get(), buf, len);
        bool ok = false;
        peekType(p.get(), len, &ok);
        AudioHeader h;
        const uint8_t* data = nullptr;
        size_t dn = 0;
        if (decodeAudioData(p.get(), len, &h, &data, &dn)) {
            okA++;
            if (dn && (data < p.get() || data + dn > p.get() + len)) fail("decodeAudioData payload outside the packet");
            if (h.nsamples > kFrameSamples) fail("decodeAudioData nsamples %u", unsigned(h.nsamples));
        }
        decodeAudio(p.get(), len, &h, pcm);
        StatusPacket s;
        if (decodeStatus(p.get(), len, &s)) okS++;
        TextPacket t;
        if (decodeText(p.get(), len, &t)) {
            okT++;
            if (t.text.size() > kMaxTextBytes) fail("decodeText text %zu bytes", t.text.size());
            std::string cmd;
            uint64_t nonce = 0;
            netauth::verify("key", 0, t, &cmd, &nonce);
            netauth::verify("", 0, t, &cmd, &nonce);
        }
        // codecs on arbitrary payloads
        if (k % 16 == 0 && dn) {
            static auto dec = makeDecoder(Codec::Opus);
            if (dec) dec->decode(data, dn, pcm);
        }
    }
    printf("  [ok] no crash (accepted: %ld audio, %ld status, %ld text)\n", okA, okS, okT);
}

static void fuzzCommands(Rng& r, long n) {
    printf("fuzz: commands (USB, network, web) and web requests, %ld each\n", n);
    Net net;
    Unit a(&net, {214, 0}, 150), b(&net, {105, 0}, 120);
    Unit* us[] = {&a, &b};
    size_t longest = 0;
    static const char* paths[] = {"/", "/school", "/floorplan.js", "/api/status", "/api/school", "/api/cmd", "/api/talk",
                                  "/api/target", "/api/replies", "/api/x", "", "/api/cmd/../..", "//", "/api/talk?x=1",
                                  "/api/fw", "/api/fw/begin?size=70000", "/api/fw/begin?size=", "/api/fw/begin?size=70000&sha=zz",
                                  "/api/fw/data?off=0", "/api/fw/data?off=", "/api/fw/data", "/api/fw/end", "/api/fw/abort", "/api/fwx", "/api/log?since=3", "/api/log", "/api/backup", "/api/restore", "/api/cam.jpg"};
    static const char* auths[] = {"", "Basic ", "Basic YWRtaW46", "Basic YWRtaW46YWRtaW4=", "Basic !!!!", "Bearer x", "Basic =",
                                  "Basic YWRtaW46ZnV6ei1wYXNzLTE="};  // admin:fuzz-pass-1, set below
    for (long k = 0; k < n; k++) {
        Unit* u = us[k & 1];
        if (k % 500 < 2) {  // the web interface and network updates on (random commands switch them off again)
            u->ic->command("webpass fuzz-pass-1");
            u->ic->command("netupdate on");
        }
        std::string line = randomCommand(r);
        std::string out = u->ic->command(line, CmdSource(pick(r, 3)));
        longest = std::max(longest, out.size());
        HttpRequest q;
        q.method = chance(r, 0.5) ? "POST" : (chance(r, 0.9) ? "GET" : "PUT");
        q.path = paths[pick(r, int(sizeof(paths) / sizeof(paths[0])))];
        q.authorization = auths[pick(r, int(sizeof(auths) / sizeof(auths[0])))];
        if (chance(r, 0.1)) q.authorization = "Basic " + randomCommand(r);
        q.xIntercom = chance(r, 0.5) ? "1" : "";
        q.body = chance(r, 0.6) ? randomCommand(r) : std::string(size_t(pick(r, 3000)), char(r()));
        if (chance(r, 0.2)) {  // base64 of something that may start like firmware
            q.body.assign(size_t(pick(r, 11000)), 'A');
            if (chance(r, 0.7)) q.body.replace(0, std::min<size_t>(2, q.body.size()), "6Q");
        }
        u->web->handle(q);
        if (k % 4 == 0) {
            net.now++;
            a.step();
            b.step();
        }
    }
    printf("  [ok] no crash (%d + %d restarts asked, %d firmware images committed, longest reply %zu bytes)\n", a.restarts, b.restarts,
           a.fwCommits + b.fwCommits, longest);
}

// --------------------------------------------------------------------------------- chaos
static bool chaosRun(uint32_t seed, int seconds, bool verbose) {
    int fail0 = g_fail;
    Rng r(seed);
    Net net;
    net.rng.seed(seed * 7919u + 1);
    std::vector<std::unique_ptr<Unit>> units;
    auto add = [&](uint16_t room) { units.emplace_back(new Unit(&net, {room, 0}, 100.0f + float(room % 97))); };
    for (int f = 1; f <= 3; f++)
        for (int n = 1; n <= (f == 3 ? 7 : 12); n++) add(uint16_t(f * 100 + n));
    for (uint16_t g = 296; g <= 299; g++) add(g);
    for (auto& u : units)
        if (u->id.room >= 296) u->ic->command("group 299");
    const int N = int(units.size());
    auto stepAll = [&] {
        net.now++;
        for (auto& u : units) u->step();
    };
    for (int k = 0; k < 50; k++) stepAll();

    // ---- chaos
    net.loss = 0.03 + 0.1 * (seed % 3);
    net.dup = 0.02;
    net.corrupt = 0.02;
    net.delay = 0.15;
    long cmds = 0, reboots = 0, flaps = 0, presses = 0;
    for (int fr = 0; fr < seconds * 50; fr++) {
        for (auto& up : units) {
            Unit& u = *up;
            if (chance(r, 0.004)) { u.held ^= 1; presses++; }                       // TALK
            if (chance(r, 0.002)) { u.held ^= uint8_t(1u << kBtnOffice); presses++; }
            if (chance(r, 0.0006)) u.held ^= uint8_t(1u << (1 + pick(r, 4)));          // MODE UP DOWN SETUP
            if (chance(r, 0.0004)) u.held = uint8_t(r() & 0x3f);                       // several at once
            if (chance(r, 0.0008)) { u.eth = !u.eth; flaps++; }
            if (chance(r, 0.0005)) u.wifi = !u.wifi;
            if (chance(r, 0.0003)) {
                u.pw.source = PowerSource(pick(r, 3));
                u.pw.hasBattery = chance(r, 0.5);
                u.pw.batteryPct = uint8_t(pick(r, 101));
            }
            if (chance(r, 0.0002)) u.outFaults = uint8_t(r() & 0x7f);
            if (chance(r, 0.0015)) { u.ic->command(randomCommand(r), CmdSource(pick(r, 3))); cmds++; }
            if (chance(r, 0.0006)) {
                TalkTarget t;
                t.kind = TalkTarget::Kind(pick(r, 4));
                t.n = uint16_t(100 + pick(r, 220));
                u.ic->setWebTarget(t);
                u.webTalk = !u.webTalk;
            }
            if (chance(r, 0.0003)) {
                TalkTarget t;
                t.kind = TalkTarget::Kind(pick(r, 4));
                t.n = uint16_t(100 + pick(r, 220));
                u.ic->setTalkTarget(t);
            }
            if (chance(r, 0.0002)) { u.boot(); reboots++; }                            // power cut
            if (chance(r, 0.0002)) {                                                    // the school status page
                u.web->statusJson();
                u.web->schoolJson();
            }
            // sizes that must stay bounded however long this runs
            if (u.ic->known().size() > 200 || u.ic->replies().size() > 200 || u.ic->peers().size() > 400 || u.ic->heard().size() > 400)
                fail("seed %u: unit %u lists grew: known %zu replies %zu peers %zu heard %zu", seed, unsigned(u.id.room),
                     u.ic->known().size(), u.ic->replies().size(), u.ic->peers().size(), u.ic->heard().size());
            if (u.ic->talkersPlaying() > 16) fail("seed %u: %d talkers playing", seed, u.ic->talkersPlaying());
        }
        if (chance(r, 0.05)) net.garbage();
        if (chance(r, 0.03)) net.replay();
        stepAll();
        if (g_fail > fail0 + 5) return false;
    }

    // ---- the chaos stops: everything must come back to rest
    net.loss = net.dup = net.corrupt = net.delay = 0;
    for (auto& u : units) {
        u->held = 0;
        u->webTalk = false;
        u->eth = true;
        u->wifi = false;
        u->pw = PowerState();
        u->outFaults = 0;
    }
    auto busy = [&](const Unit& u) -> const char* {
        if (u.ic->transmitting()) return "transmitting";
        if (u.ic->webTalking()) return "web talking";
        if (u.ic->talkersPlaying()) return "playing a talker";
        if (u.ic->calibrating()) return "calibrating";
        if (u.ic->couplingTest()) return "coupling test";
        if (u.ic->selfTesting()) return "self test";
        if (u.ic->inSetup()) return "room setup";
        if (u.ic->soundPlaying()) return "playing a sound";
        if (u.ic->toneActive()) return "test tone";
        if (u.ic->inCall()) return "in a call";
        if (u.ic->groupTalking()) return "group talking";
        if (u.ic->link() != LinkType::Ethernet) return "not on Ethernet";
        if (!u.silent()) return "speaker not silent";
        // packets sent this frame by a unit later in the list (its once-a-second status) are not a backlog
        for (auto& it : u.rx)
            if (it.due < net.now) return "packets waiting";
        return nullptr;
    };
    const int kRestLimit = 15 * 60 * 50;  // 15 minutes
    int rest = -1;
    for (int fr = 0; fr < kRestLimit; fr++) {
        stepAll();
        if (fr % 50) continue;
        bool any = false;
        for (auto& u : units) any = any || busy(*u);
        if (!any) { rest = fr; break; }
    }
    if (rest < 0) {
        std::map<std::string, int> why;
        for (auto& u : units)
            if (const char* b = busy(*u)) why[b]++;
        std::string s;
        for (auto& w : why) s += w.first + " x" + std::to_string(w.second) + "; ";
        std::map<std::string, int> what;  // what is still arriving, and from whom
        size_t deepest = 0;
        for (auto& u : units) {
            deepest = std::max(deepest, u->rx.size());
            for (auto& it : u->rx) {
                char b[64];
                if (it.data.size() >= 8)
                    snprintf(b, sizeof(b), "type %u from %u flags %02x", unsigned(it.data[3]), unsigned(it.data[4] | (it.data[5] << 8)), unsigned(it.data[7]));
                else
                    snprintf(b, sizeof(b), "%zu bytes", it.data.size());
                what[b]++;
            }
        }
        s += "deepest queue " + std::to_string(deepest) + "; ";
        int shown = 0;
        for (auto& w : what)
            if (shown++ < 8) s += w.first + " x" + std::to_string(w.second) + "; ";
        fail("seed %u: not at rest 15 min after the chaos stopped: %s", seed, s.c_str());
    }

    // ---- and still work: put the settings back, then one room calls everyone
    for (auto& u : units) {
        u->ic->command("defaults");
        u->ic->command("mute off");
        u->ic->command("feature all off");
        u->ic->command("dnd off");
        u->ic->command("callofc off");
        u->ic->command("emergency clear");
        u->ic->command(u->id0.room >= 296 ? "group 299" : "group 0");
        TalkTarget all;
        u->ic->setTalkTarget(all);
    }
    for (int k = 0; k < 100; k++) stepAll();
    int heardOk = 0, quietBy = 0;
    Unit& t = *units[size_t(pick(r, N))];
    t.held = 1;
    std::vector<int> got(size_t(N), 0);
    for (int k = 0; k < 150; k++) {
        stepAll();
        for (int i = 0; i < N; i++)
            if (!units[size_t(i)]->silent()) got[size_t(i)]++;
    }
    t.held = 0;
    std::string deaf;
    for (int i = 0; i < N; i++) {
        Unit& u = *units[size_t(i)];
        if (&u == &t) continue;
        bool sameRoom = u.ic->group() == t.ic->group();
        if (sameRoom) { quietBy++; if (got[size_t(i)]) fail("seed %u: %u played its own room's talker %u", seed, unsigned(u.id.room), unsigned(t.id.room)); continue; }
        if (got[size_t(i)] > 50) heardOk++;
        else deaf += std::to_string(u.id.room) + (u.ic->muted() ? "(muted) " : u.ic->duplicate() ? "(dup) " : " ");
    }
    if (!t.ic->transmitting() && deaf.size()) deaf += "[talker never transmitted] ";
    if (heardOk != N - 1 - quietBy)
        fail("seed %u: after the chaos, %u talks and only %d of %d rooms play it; silent: %s", seed, unsigned(t.id.room), heardOk,
             N - 1 - quietBy, deaf.c_str());
    for (int k = 0; k < 200; k++) stepAll();

    bool ok = g_fail == fail0;
    if (verbose || !ok)
        printf("  [%s] seed %u: %d s of chaos (loss %.0f%%), %ld presses, %ld commands, %ld link flaps, %ld power cuts; at rest after %d s; "
               "%d of %d rooms hear a call\n",
               ok ? "ok" : "FAIL", seed, seconds, (0.03 + 0.1 * (seed % 3)) * 100, presses, cmds, flaps, reboots, rest / 50, heardOk,
               N - 1 - quietBy);
    return ok;
}

int main(int argc, char** argv) {
    uint32_t seed = 1;
    int seeds = 6, seconds = 120;
    long fuzz = 300000;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--seed")) seed = uint32_t(atol(argv[i + 1]));
        else if (!strcmp(argv[i], "--seeds")) seeds = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--seconds")) seconds = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--fuzz")) fuzz = atol(argv[i + 1]);
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    Rng r(seed);
    if (fuzz) {
        fuzzDecoders(r, fuzz);
        fuzzCommands(r, fuzz / 10);
    }
    printf("chaos: 35 units, %d runs of %d s\n", seeds, seconds);
    for (int k = 0; k < seeds; k++) chaosRun(seed + uint32_t(k), seconds, true);
    printf(g_fail ? "\n%d FAILED\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
