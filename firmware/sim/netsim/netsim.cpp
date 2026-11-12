// ic-netsim: whole-school network load simulation.
//
// Every unit runs the real intercom core (firmware/src/core) on a packet-level model of
// the school network: unit -> floor PoE switch -> uplink -> core switch -> NVR / office
// console, plus Wi-Fi units behind an access point. Links have a bit rate, propagation
// delay and a finite buffer; optional QoS gives audio strict priority over video at every
// queue. In emergency mode every camera streams MJPEG to the NVR (dummy frames of the
// real size and rate, fragmented into 1400-byte packets).
//
// Reports, per scenario: audio loss, audio one-way latency (p50 / p99 / max), jitter-buffer
// concealment and late drops (from the real core), video frames delivered and frame latency,
// and the busiest link.
//
//   ic-netsim                    run the standard scenario suite, print a Markdown table
//   ic-netsim --units 35 --uplink 100 --nvr 100 --video 1 --qos 0 --seconds 20   one scenario
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <vector>

#include "Intercom.h"
#include "NlmsAec.h"

using namespace ic;

namespace {

constexpr int kOverhead = 42;      // Ethernet + IP + UDP header bytes
constexpr int kVideoMtu = 1400;    // video payload per packet
constexpr int kMulticast = -1;

enum Cls { kAudio = 0, kVideo = 1 };

struct Packet {
    int src = 0, dst = kMulticast;
    int bytes = 0;
    Cls cls = kAudio;
    bool isAudioFrame = false;     // a voice packet (vs status / command)
    double t0 = 0;
    std::shared_ptr<std::vector<uint8_t>> data;  // core packets; null for video
    long frameId = -1;             // video
    int frags = 0;
};
using PacketP = std::shared_ptr<Packet>;

struct Scenario {
    std::string name;
    int units = 35, floors = 3, wifiUnits = 0;
    double unitTxMbps = 15;        // W5500 over SPI: the unit's real send ceiling
    double portMbps = 100;         // unit switch ports (W5500 is 100BASE-TX)
    double uplinkMbps = 1000;      // floor switch -> core switch
    double nvrMbps = 1000;         // core switch -> NVR port
    double consoleMbps = 1000;
    double wifiMbps = 20;          // effective per-client Wi-Fi throughput
    double wifiLoss = 0.01;
    bool qos = false;
    bool video = false;
    bool wifiVideo = true;         // Wi-Fi units stream video too
    double fps = 12, frameKB = 30; // MJPEG VGA
    int talkers = 4;
    double seconds = 20;
    double videoStart = 1.5;       // emergency begins (before the 2 s measurement start)
    int switchBufKB = 128;         // per egress port
};

struct Link {
    int from = 0, to = 0;
    double bps = 1e8, prop = 2e-6, loss = 0;
    int bufBytes = 128 * 1024;
    bool qos = false;
    std::deque<PacketP> q[2];
    int qBytes[2] = {0, 0};
    bool busy = false;
    // stats
    long sent[2] = {0, 0}, drops[2] = {0, 0};
    double busyTime = 0;
    std::string name;
};

struct FakeUnit;

struct Node {
    enum Kind { Unit, Switch, Ap, Nvr, Console } kind = Switch;
    std::vector<int> out;          // outgoing link ids
    std::map<int, int> nextHop;    // dst node -> outgoing link id
    bool member = false;           // multicast group member (units, console)
    FakeUnit* unit = nullptr;
    std::string name;
};

struct Event {
    double t;
    long seq;
    std::function<void()> fn;
    bool operator<(const Event& o) const { return t != o.t ? t > o.t : seq > o.seq; }
};

class Net;

// One unit: the real intercom core with HAL calls bridged to the simulated network.
struct FakeUnit : INet, ILinks, IPower, IPanel, IStore, ILog {
    Net* net = nullptr;
    int node = 0;
    UnitId id;
    bool wifi = false;
    uint8_t held = 0;
    std::deque<std::shared_ptr<std::vector<uint8_t>>> rx;
    std::map<std::string, std::string> kv;
    NlmsAec aec{128, 0};
    std::unique_ptr<Intercom> ic;
    long audioSent = 0;

    void send(const uint8_t* d, size_t n) override;
    size_t recv(uint8_t* buf, size_t cap) override {
        if (rx.empty()) return 0;
        auto p = rx.front();
        rx.pop_front();
        size_t n = std::min(cap, p->size());
        memcpy(buf, p->data(), n);
        return n;
    }
    bool ethernetUp() override { return !wifi; }
    bool wifiUp() override { return wifi; }
    void useLink(LinkType) override {}
    PowerState read() override { return PowerState(); }
    uint8_t buttons() override { return held; }
    void show(const char*, uint8_t) override {}
    UnitId loadUnit() override { return id; }
    void saveUnit(const UnitId& u) override { id = u; }
    std::string loadText(const char* k) override { return kv[k]; }
    void saveText(const char* k, const std::string& v) override { kv[k] = v; }
    void log(const char*) override {}
};

class Net {
public:
    explicit Net(const Scenario& sc) : sc_(sc), rng_(12345) {}

    double now = 0;
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<std::unique_ptr<FakeUnit>> units;
    int nvr = -1, console = -1;

    // results
    std::vector<double> audioLatMs;
    long audioExpected = 0, audioDelivered = 0;
    std::map<long, int> fragsSeen;
    std::map<long, double> frameT0;
    std::map<long, int> frameFrags;
    long videoFramesSent = 0, videoFramesOk = 0;
    std::map<long, double> frameLatMs;  // delivered frames: latency

    int addNode(Node::Kind k, const std::string& name) {
        Node n;
        n.kind = k;
        n.name = name;
        n.member = (k == Node::Unit || k == Node::Console);
        nodes.push_back(n);
        return int(nodes.size()) - 1;
    }
    void connect(int a, int b, double mbps, int bufBytes, double loss, const std::string& name) {
        for (int dir = 0; dir < 2; dir++) {
            Link l;
            l.from = dir ? b : a;
            l.to = dir ? a : b;
            l.bps = mbps * 1e6;
            l.bufBytes = bufBytes;
            l.loss = loss;
            l.qos = sc_.qos;
            l.name = dir ? name + " (down)" : name + " (up)";  // up = toward the core switch
            links.push_back(l);
            nodes[size_t(l.from)].out.push_back(int(links.size()) - 1);
        }
    }
    void buildRoutes() {
        // Tree: BFS from every node gives the first link toward each destination.
        for (size_t s = 0; s < nodes.size(); s++) {
            std::vector<int> first(nodes.size(), -1);
            std::deque<int> q;
            std::vector<bool> seen(nodes.size(), false);
            seen[s] = true;
            for (int li : nodes[s].out) {
                int t = links[size_t(li)].to;
                if (!seen[size_t(t)]) { seen[size_t(t)] = true; first[size_t(t)] = li; q.push_back(t); }
            }
            while (!q.empty()) {
                int u = q.front();
                q.pop_front();
                for (int li : nodes[size_t(u)].out) {
                    int t = links[size_t(li)].to;
                    if (!seen[size_t(t)]) { seen[size_t(t)] = true; first[size_t(t)] = first[size_t(u)]; q.push_back(t); }
                }
            }
            for (size_t d = 0; d < nodes.size(); d++)
                if (first[d] >= 0) nodes[s].nextHop[int(d)] = first[d];
        }
        // Multicast: a link carries the group if any member lies beyond it (IGMP snooping).
        hasMember_.assign(links.size(), false);
        for (size_t li = 0; li < links.size(); li++) {
            int from = links[li].from, to = links[li].to;
            for (size_t d = 0; d < nodes.size(); d++)
                if (nodes[d].member && int(d) != from && nodes[size_t(from)].nextHop.count(int(d)) &&
                    nodes[size_t(from)].nextHop[int(d)] == int(li))
                    hasMember_[li] = true;
            (void)to;
        }
    }

    void at(double t, std::function<void()> fn) { ev_.push(Event{t, seq_++, std::move(fn)}); }

    void inject(int fromNode, PacketP p) {
        // Hosts have a single uplink.
        enqueue(nodes[size_t(fromNode)].out[0], p);
    }

    void enqueue(int li, PacketP p) {
        Link& l = links[size_t(li)];
        if (l.loss > 0 && uni_(rng_) < l.loss) { l.drops[p->cls]++; return; }
        int c = l.qos ? int(p->cls) : 0;
        if (l.qBytes[c] + p->bytes > l.bufBytes) { l.drops[p->cls]++; return; }
        l.q[c].push_back(p);
        l.qBytes[c] += p->bytes;
        if (!l.busy) startTx(li);
    }

    void startTx(int li) {
        Link& l = links[size_t(li)];
        int c = !l.q[0].empty() ? 0 : (!l.q[1].empty() ? 1 : -1);
        if (c < 0) { l.busy = false; return; }
        PacketP p = l.q[c].front();
        l.q[c].pop_front();
        l.qBytes[c] -= p->bytes;
        l.busy = true;
        double tx = p->bytes * 8.0 / l.bps;
        l.busyTime += tx;
        l.sent[p->cls]++;
        at(now + tx, [this, li, p] {
            Link& L = links[size_t(li)];
            at(now + L.prop, [this, li, p] { arrive(links[size_t(li)].to, links[size_t(li)].from, p); });
            startTx(li);
        });
    }

    void arrive(int node, int from, PacketP p) {
        Node& n = nodes[size_t(node)];
        if (n.kind == Node::Switch || n.kind == Node::Ap) {
            if (p->dst == kMulticast) {
                for (int li : n.out)
                    if (links[size_t(li)].to != from && hasMember_[size_t(li)]) enqueue(li, p);
            } else {
                auto it = n.nextHop.find(p->dst);
                if (it != n.nextHop.end()) enqueue(it->second, p);
            }
            return;
        }
        // Host.
        if (p->dst != kMulticast && p->dst != node) return;
        if (n.kind == Node::Unit && p->data) {
            n.unit->rx.push_back(p->data);
            if (p->isAudioFrame) {
                audioDelivered++;
                audioLatMs.push_back((now - p->t0) * 1000.0);
            }
        } else if (n.kind == Node::Nvr && p->cls == kVideo) {
            if (++fragsSeen[p->frameId] == p->frags) {
                videoFramesOk++;
                frameLatMs[p->frameId] = (now - frameT0[p->frameId]) * 1000.0;
                fragsSeen.erase(p->frameId);
            }
        }
    }

    void run(double until) {
        while (!ev_.empty() && ev_.top().t <= until) {
            Event e = ev_.top();
            ev_.pop();
            now = e.t;
            e.fn();
        }
    }

    double uniform() { return uni_(rng_); }
    double normal() { return nrm_(rng_); }
    const Scenario& sc() const { return sc_; }
    const std::vector<bool>& hasMember() const { return hasMember_; }

private:
    Scenario sc_;
    std::priority_queue<Event> ev_;
    long seq_ = 0;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> uni_{0.0, 1.0};
    std::normal_distribution<double> nrm_{0.0, 1.0};
    std::vector<bool> hasMember_;
};

void FakeUnit::send(const uint8_t* d, size_t n) {
    auto p = std::make_shared<Packet>();
    p->src = node;
    p->dst = kMulticast;
    p->bytes = int(n) + kOverhead;
    p->cls = kAudio;  // voice, status and console traffic all ride the priority class
    p->isAudioFrame = n > 3 && d[3] == uint8_t(PacketType::Audio);
    p->t0 = net->now;
    p->data = std::make_shared<std::vector<uint8_t>>(d, d + n);
    if (p->isAudioFrame) audioSent++;
    net->inject(node, p);
}

struct Result {
    std::string name;
    double videoMbps = 0;
    double audioLossPct = 0, p50 = 0, p99 = 0, maxMs = 0;
    double concealPct = 0;
    long late = 0;
    double videoOkPct = 0, videoP99 = 0;
    std::string busiest;
    double busiestUtil = 0;
};

double pct(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[size_t(std::min(v.size() - 1, size_t(q * double(v.size()))))];
}

Result runScenario(const Scenario& sc) {
    Net net(sc);
    // Topology: floor switches on a core switch; NVR and office console on the core switch;
    // one Wi-Fi AP on floor 1's switch.
    int core = net.addNode(Node::Switch, "core switch");
    net.nvr = net.addNode(Node::Nvr, "NVR");
    net.console = net.addNode(Node::Console, "office console");
    net.connect(net.nvr, core, sc.nvrMbps, sc.switchBufKB * 1024, 0, "NVR port");
    net.connect(net.console, core, sc.consoleMbps, sc.switchBufKB * 1024, 0, "console port");
    std::vector<int> floorSw;
    for (int f = 0; f < sc.floors; f++) {
        int s = net.addNode(Node::Switch, "floor " + std::to_string(f + 1) + " switch");
        net.connect(s, core, sc.uplinkMbps, sc.switchBufKB * 1024 * 2, 0, "floor " + std::to_string(f + 1) + " uplink");
        floorSw.push_back(s);
    }
    int ap = -1;
    if (sc.wifiUnits > 0) {
        ap = net.addNode(Node::Ap, "Wi-Fi AP");
        net.connect(ap, floorSw[0], 1000, sc.switchBufKB * 1024, 0, "AP port");
    }

    int wired = sc.units - sc.wifiUnits;
    for (int i = 0; i < sc.units; i++) {
        auto u = std::make_unique<FakeUnit>();
        u->net = &net;
        bool wifi = i >= wired;
        int floor = wifi ? 0 : i * sc.floors / std::max(1, wired);
        int idx = wifi ? 90 + (i - wired) : i - floor * wired / sc.floors + 1;
        u->id = UnitId{uint16_t((floor + 1) * 100 + idx), 0};
        u->wifi = wifi;
        u->node = net.addNode(Node::Unit, "unit " + std::to_string(u->id.room));
        net.nodes[size_t(u->node)].unit = u.get();
        if (wifi) {
            // Unit -> AP: Wi-Fi rate; the unit's own TX ceiling is lower of the two.
            net.connect(u->node, ap, sc.wifiMbps, 64 * 1024, sc.wifiLoss, "Wi-Fi " + std::to_string(u->id.room));
        } else {
            net.connect(u->node, floorSw[size_t(floor)], sc.portMbps, sc.switchBufKB * 1024, 0, "unit " + std::to_string(u->id.room));
        }
        u->kv["tune"] = "aec=0 taps=128";  // echo is not what this test is about
        IntercomOptions io;
        io.maxTaps = 128;
        u->ic.reset(new Intercom(Hal{u.get(), u.get(), u.get(), u.get(), u.get(), u.get()}, u->aec, io));
        u->ic->begin();
        net.units.push_back(std::move(u));
    }
    // The unit's send ceiling (W5500 SPI) applies to its first link.
    for (auto& u : net.units)
        if (!u->wifi) net.links[size_t(net.nodes[size_t(u->node)].out[0])].bps = sc.unitTxMbps * 1e6;
    net.buildRoutes();

    // Talkers spread across floors hold TALK from t = 1 s.
    std::vector<FakeUnit*> talkers;
    for (int k = 0; k < sc.talkers && k < sc.units; k++) talkers.push_back(net.units[size_t(k * sc.units / sc.talkers)].get());

    // 20 ms frame clocks, each unit with its own phase.
    for (auto& up : net.units) {
        FakeUnit* u = up.get();
        double phase = net.uniform() * 0.02;
        std::shared_ptr<std::function<void()>> tick = std::make_shared<std::function<void()>>();
        *tick = [&net, u, tick, &talkers] {
            bool talk = net.now >= 1.0 && std::find(talkers.begin(), talkers.end(), u) != talkers.end();
            u->held = talk ? 1 : 0;
            int16_t mic[kFrameSamples], spk[kFrameSamples];
            for (int i = 0; i < kFrameSamples; i++) mic[i] = talk ? int16_t(3000 * std::sin(i * 0.2)) : 0;
            u->ic->processFrame(mic, spk);
            net.at(net.now + 0.02, *tick);
        };
        net.at(phase, *tick);
    }

    // Emergency video: every camera streams MJPEG to the NVR.
    long frameId = 0;
    if (sc.video) {
        for (auto& up : net.units) {
            FakeUnit* u = up.get();
            if (u->wifi && !sc.wifiVideo) continue;
            double start = sc.videoStart + net.uniform() / sc.fps;
            std::shared_ptr<std::function<void()>> frame = std::make_shared<std::function<void()>>();
            *frame = [&net, u, frame, &sc, &frameId] {
                int bytes = std::max(4000, int(sc.frameKB * 1024 * (1.0 + 0.2 * net.normal())));
                int frags = (bytes + kVideoMtu - 1) / kVideoMtu;
                long id = frameId++;
                net.frameT0[id] = net.now;
                net.videoFramesSent++;
                for (int k = 0; k < frags; k++) {
                    auto p = std::make_shared<Packet>();
                    p->src = u->node;
                    p->dst = net.nvr;
                    p->cls = kVideo;
                    p->bytes = std::min(kVideoMtu, bytes - k * kVideoMtu) + kOverhead;
                    p->t0 = net.now;
                    p->frameId = id;
                    p->frags = frags;
                    net.inject(u->node, p);
                }
                net.at(net.now + 1.0 / sc.fps, *frame);
            };
            net.at(start, *frame);
        }
    }

    // Measure from 2 s (talk settled); video window starts with the emergency.
    net.run(2.0);
    std::vector<long> sent0;
    for (auto& u : net.units) sent0.push_back(u->audioSent);
    std::vector<AudioStats> st0;
    for (auto& u : net.units) st0.push_back(u->ic->audioStats());
    long delivered0 = net.audioDelivered;
    net.audioLatMs.clear();
    for (auto& l : net.links) l.busyTime = 0;
    long vSent0 = net.videoFramesSent, vOk0 = net.videoFramesOk;
    net.run(sc.seconds);
    double window = sc.seconds - 2.0;

    Result r;
    r.name = sc.name;
    // Each talker's voice packet should reach every unit outside its room, and the console.
    long expected = 0;
    for (size_t i = 0; i < net.units.size(); i++) {
        long s = net.units[i]->audioSent - sent0[i];
        if (s) expected += s * long(net.units.size() - 1);
    }
    // (console receives too but is not a Unit host; count unit deliveries only)
    long got = net.audioDelivered - delivered0;
    r.audioLossPct = expected ? 100.0 * double(expected - got) / double(expected) : 0;
    if (r.audioLossPct < 0) r.audioLossPct = 0;
    r.p50 = pct(net.audioLatMs, 0.5);
    r.p99 = pct(net.audioLatMs, 0.99);
    r.maxMs = net.audioLatMs.empty() ? 0 : *std::max_element(net.audioLatMs.begin(), net.audioLatMs.end());
    long played = 0, conc = 0;
    for (size_t i = 0; i < net.units.size(); i++) {
        AudioStats s = net.units[i]->ic->audioStats();
        played += s.framesPlayed - st0[i].framesPlayed;
        conc += s.concealed - st0[i].concealed;
        r.late += s.lateDrops - st0[i].lateDrops;
    }
    r.concealPct = played ? 100.0 * double(conc) / double(played) : 0;
    // Video: frames started inside the window, excluding the last 0.5 s still in flight.
    long elig = 0, ok = 0;
    std::vector<double> vlat;
    for (auto& kv : net.frameT0) {
        if (kv.second < 2.0 || kv.second > sc.seconds - 0.5) continue;
        elig++;
        auto it = net.frameLatMs.find(kv.first);
        if (it != net.frameLatMs.end()) { ok++; vlat.push_back(it->second); }
    }
    (void)vSent0; (void)vOk0;
    r.videoOkPct = elig ? 100.0 * double(ok) / double(elig) : 0;
    r.videoP99 = pct(vlat, 0.99);
    r.videoMbps = sc.video ? sc.fps * sc.frameKB * 1024 * 8 * double(net.units.size() - (sc.wifiVideo ? 0 : sc.wifiUnits)) / 1e6 : 0;
    for (auto& l : net.links) {
        double u = l.busyTime / window;
        if (u > r.busiestUtil) { r.busiestUtil = u; r.busiest = l.name; }
    }
    return r;
}

void printHeader() {
    printf("| Scenario | Video load | Busiest link | Audio loss | Audio latency p50 / p99 / max | Concealed | Late frames | Video frames OK / p99 |\n");
    printf("|---|---|---|---|---|---|---|---|\n");
}

void printRow(const Result& r) {
    char video[48] = "-";
    if (r.videoMbps > 0) snprintf(video, sizeof(video), "%.1f %% / %.0f ms", r.videoOkPct, r.videoP99);
    printf("| %s | %.0f Mbit/s | %s %.0f %% | %.2f %% | %.1f / %.1f / %.1f ms | %.2f %% | %ld | %s |\n", r.name.c_str(),
           r.videoMbps, r.busiest.c_str(), r.busiestUtil * 100, r.audioLossPct, r.p50, r.p99, r.maxMs, r.concealPct, r.late,
           video);
    fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        Scenario s;
        s.name = "custom";
        for (int i = 1; i + 1 < argc; i += 2) {
            std::string k = argv[i];
            double v = atof(argv[i + 1]);
            if (k == "--units") s.units = int(v);
            else if (k == "--floors") s.floors = int(v);
            else if (k == "--wifi-units") s.wifiUnits = int(v);
            else if (k == "--uplink") s.uplinkMbps = v;
            else if (k == "--nvr") s.nvrMbps = v;
            else if (k == "--port") s.portMbps = v;
            else if (k == "--unit-tx") s.unitTxMbps = v;
            else if (k == "--qos") s.qos = v != 0;
            else if (k == "--video") s.video = v != 0;
            else if (k == "--fps") s.fps = v;
            else if (k == "--frame-kb") s.frameKB = v;
            else if (k == "--talkers") s.talkers = int(v);
            else if (k == "--seconds") s.seconds = v;
            else if (k == "--buf-kb") s.switchBufKB = int(v);
            else { fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
        }
        printHeader();
        printRow(runScenario(s));
        return 0;
    }

    // Standard suite: OVCS-sized school, 35 units on 3 floors, 4 people talking.
    std::vector<Scenario> suite;
    Scenario base;
    base.units = 35;
    base.floors = 3;
    base.talkers = 4;
    base.seconds = 15;

    Scenario a = base; a.name = "Normal day, 100M uplinks, no video"; a.uplinkMbps = 100; a.nvrMbps = 100; suite.push_back(a);
    Scenario b = base; b.name = "Emergency, all 1 Gbit, no QoS"; b.video = true; suite.push_back(b);
    Scenario c = base; c.name = "Emergency, 100M uplinks + NVR port, no QoS"; c.video = true; c.uplinkMbps = 100; c.nvrMbps = 100; suite.push_back(c);
    Scenario d = c; d.name = "Emergency, 100M uplinks + NVR port, audio QoS"; d.qos = true; suite.push_back(d);
    Scenario e = base; e.name = "Emergency, 100M uplinks, 1G NVR, no QoS"; e.video = true; e.uplinkMbps = 100; suite.push_back(e);
    Scenario f = base; f.name = "Emergency, heavy video (15 fps, 45 KB), 1G, no QoS"; f.video = true; f.fps = 15; f.frameKB = 45; suite.push_back(f);
    Scenario f2 = base; f2.name = "Emergency, 720p-size frames (100 KB, 10 fps), 1G, no QoS"; f2.video = true; f2.fps = 10; f2.frameKB = 100; suite.push_back(f2);
    Scenario f3 = f2; f3.name = "Emergency, 720p-size frames (100 KB, 10 fps), 1G, audio QoS"; f3.qos = true; suite.push_back(f3);
    Scenario g = base; g.name = "Emergency, 1G, QoS, 3 Wi-Fi units also streaming"; g.video = true; g.qos = true; g.units = 38; g.wifiUnits = 3; suite.push_back(g);
    Scenario h = g; h.name = "Emergency, 1G, no QoS, 3 Wi-Fi units also streaming"; h.qos = false; suite.push_back(h);
    Scenario i = g; i.name = "Emergency, 1G, QoS, Wi-Fi units audio only"; i.wifiVideo = false; suite.push_back(i);

    printf("School model: %d wired units on %d floor switches, core switch with NVR + office console, %d people talking.\n"
           "Unit send ceiling %.0f Mbit/s (W5500/SPI), unit ports %.0f Mbit/s, %d KB buffer per switch port.\n\n",
           base.units, base.floors, base.talkers, base.unitTxMbps, base.portMbps, base.switchBufKB);
    printHeader();
    for (auto& s : suite) printRow(runScenario(s));
    return 0;
}
