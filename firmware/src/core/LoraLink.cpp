// LoRa side channel (DRAFT): message formats, signing, and who sends what when. See LoraLink.h.
#include "LoraLink.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Auth.h"
#include "Config.h"

namespace ic {
namespace lora {
namespace {

const char* const kKinds[] = {"all clear", "lockdown", "evacuate", "shelter"};
const char* const kTypeNames[] = {"?", "EMERGENCY", "HELP", "PEOPLE", "STATUS", "ACK", "QUERY", "TEST"};

void put16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
    put16(p, uint16_t(v));
    put16(p + 2, uint16_t(v >> 16));
}
uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t get32(const uint8_t* p) { return uint32_t(get16(p)) | uint32_t(get16(p + 2)) << 16; }

// The first 8 bytes of HMAC-SHA256(key, "lora1|" + message), the same HMAC as the network commands.
void signature(const std::string& key, const uint8_t* msg, size_t n, uint8_t out[kSigBytes]) {
    const std::string hex = hmacSha256Hex(key, std::string("lora1|") + std::string(reinterpret_cast<const char*>(msg), n));
    for (size_t i = 0; i < kSigBytes; i++) out[i] = uint8_t(std::strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16));
}

}  // namespace

uint8_t quietCode(uint8_t minutes) {
    if (minutes == 255) return 7;
    if (minutes >= 254) return 5;
    return minutes >= 60 ? 4 : minutes >= 15 ? 3 : minutes >= 5 ? 2 : minutes >= 1 ? 1 : 0;
}

std::string peopleText(const PeopleEntry& e) {
    static const char* const kQuiet[] = {"", " for 1 min or more", " for 5 min or more", " for 15 min or more", " for an hour or more", " for over 4 hours", "", " since the sensor started"};
    std::string t = "room " + std::to_string(e.room) + ": ";
    if (e.code == kPeopleEmpty) return t + "empty" + kQuiet[e.quiet & 7];
    if (e.code == kPeopleSomeone) return t + "someone";
    if (e.code > kPeopleSomeone) return t + "?";
    return t + std::to_string(e.code) + (e.code >= kPeopleThreePlus ? " or more" : "");
}

size_t encode(const Msg& m, const std::string& key, uint8_t* out) {
    size_t n = 0;
    out[n++] = kMagic;
    out[n++] = uint8_t(uint8_t(m.type) | (key.empty() ? 0 : kFlagSigned));
    put16(out + n, m.from);
    put32(out + n + 2, m.counter);
    n += 6;
    switch (m.type) {
        case Type::Emergency:
            out[n++] = m.kind;
            put16(out + n, m.origin);
            put16(out + n + 2, m.ageS);
            n += 4;
            break;
        case Type::Help: {
            const size_t k = std::min(m.help.size(), kMaxHelp);
            out[n++] = uint8_t(k);
            for (size_t i = 0; i < k; i++) {
                put16(out + n, m.help[i].room);
                out[n + 2] = uint8_t((m.help[i].urgent ? 1 : 0) | (m.help[i].silent ? 2 : 0));
                put16(out + n + 3, m.help[i].waitS);
                n += 5;
            }
            break;
        }
        case Type::People: {
            const size_t k = std::min(m.people.size(), kMaxPeople);
            out[n++] = uint8_t((m.part & 15) << 4 | (m.parts & 15));
            for (size_t i = 0; i < k; i++) {
                const PeopleEntry& e = m.people[i];
                put16(out + n, uint16_t((e.room & 0x3ff) | (e.code & 7) << 10 | (e.quiet & 7) << 13));
                n += 2;
            }
            break;
        }
        case Type::Status:
            out[n++] = m.online;
            out[n++] = m.offline;
            out[n++] = m.kind;
            out[n++] = m.power;
            out[n++] = m.battery;
            out[n++] = m.helpWaiting;
            put16(out + n, m.uptimeMin);
            n += 2;
            break;
        case Type::Ack:
            put16(out + n, m.ackRoom);
            put32(out + n + 2, m.ackCounter);
            out[n + 6] = m.ackType;
            out[n + 7] = uint8_t(m.ackRssi);
            n += 8;
            break;
        case Type::Query:
            out[n++] = m.what;
            break;
        case Type::Test:
            put32(out + n, m.uptimeS);
            n += 4;
            break;
    }
    if (!key.empty()) {
        signature(key, out, n, out + n);
        n += kSigBytes;
    }
    return n;
}

Check decode(const uint8_t* buf, size_t len, const std::string& key, Msg* m) {
    if (len < kHeaderBytes || len > kMaxBytes || buf[0] != kMagic) return Check::Malformed;
    if (buf[1] & 0x70) return Check::Malformed;  // a newer format
    const bool sig = (buf[1] & kFlagSigned) != 0;
    if (sig && len < kHeaderBytes + kSigBytes) return Check::Malformed;
    const size_t end = sig ? len - kSigBytes : len;  // the payload is buf[8 .. end)
    const uint8_t* p = buf + kHeaderBytes;
    const size_t n = end - kHeaderBytes;
    *m = Msg();
    m->type = Type(buf[1] & 15);
    m->from = get16(buf + 2);
    m->counter = get32(buf + 4);
    m->isSigned = sig;
    switch (m->type) {
        case Type::Emergency:
            if (n != 5 || p[0] > 3) return Check::Malformed;
            m->kind = p[0];
            m->origin = get16(p + 1);
            m->ageS = get16(p + 3);
            break;
        case Type::Help:
            if (n < 1 || p[0] > kMaxHelp || n != 1 + 5u * p[0]) return Check::Malformed;
            for (size_t i = 0; i < p[0]; i++) {
                const uint8_t* e = p + 1 + 5 * i;
                HelpEntry h;
                h.room = get16(e);
                h.urgent = (e[2] & 1) != 0;
                h.silent = (e[2] & 2) != 0;
                h.waitS = get16(e + 3);
                m->help.push_back(h);
            }
            break;
        case Type::People:
            if (n < 1 || (n - 1) % 2 || (n - 1) / 2 > kMaxPeople) return Check::Malformed;
            m->part = uint8_t(p[0] >> 4);
            m->parts = uint8_t(p[0] & 15);
            if (!m->parts || m->part >= m->parts) return Check::Malformed;
            for (size_t i = 1; i + 1 < n; i += 2) {
                const uint16_t v = get16(p + i);
                PeopleEntry e;
                e.room = uint16_t(v & 0x3ff);
                e.code = uint8_t((v >> 10) & 7);
                e.quiet = uint8_t(v >> 13);
                m->people.push_back(e);
            }
            break;
        case Type::Status:
            if (n != 8 || p[2] > 3) return Check::Malformed;
            m->online = p[0];
            m->offline = p[1];
            m->kind = p[2];
            m->power = p[3];
            m->battery = p[4];
            m->helpWaiting = p[5];
            m->uptimeMin = get16(p + 6);
            break;
        case Type::Ack:
            if (n != 8) return Check::Malformed;
            m->ackRoom = get16(p);
            m->ackCounter = get32(p + 2);
            m->ackType = p[6];
            m->ackRssi = int8_t(p[7]);
            break;
        case Type::Query:
            if (n != 1) return Check::Malformed;
            m->what = p[0];
            break;
        case Type::Test:
            if (n != 4) return Check::Malformed;
            m->uptimeS = get32(p);
            break;
        default:
            return Check::Malformed;
    }
    if (!sig) return Check::NotSigned;
    if (key.empty()) return Check::NoKey;
    uint8_t want[kSigBytes];
    signature(key, buf, end, want);
    uint8_t diff = 0;
    for (size_t i = 0; i < kSigBytes; i++) diff |= uint8_t(want[i] ^ buf[end + i]);
    return diff ? Check::BadSignature : Check::Ok;
}

std::string describe(const Msg& m) {
    char b[160];
    std::string t;
    switch (m.type) {
        case Type::Emergency:
            if (!m.kind) return "ALL CLEAR (the emergency is over)";
            snprintf(b, sizeof(b), "EMERGENCY %s, started by %s%s, ", kKinds[m.kind & 3], m.origin ? "the red button in room " : "the office",
                     m.origin ? std::to_string(m.origin).c_str() : "");
            t = b;
            if (m.ageS == 65535) return t + "over 18 hours ago";
            return t + (m.ageS >= 120 ? std::to_string(m.ageS / 60) + " min ago" : std::to_string(m.ageS) + " s ago");
        case Type::Help:
            if (m.help.empty()) return "no call for help waiting any more";
            for (const HelpEntry& h : m.help) {
                snprintf(b, sizeof(b), "%sroom %u %s, waiting %u s", t.empty() ? "" : "; ", unsigned(h.room), h.silent ? "SILENT HELP CALL" : "needs HELP", unsigned(h.waitS));
                t += b;
            }
            return t;
        case Type::People: {
            unsigned in = 0;
            for (const PeopleEntry& e : m.people) in += e.code ? 1 : 0;
            snprintf(b, sizeof(b), "people list part %u of %u: %u rooms, people in %u", unsigned(m.part + 1), unsigned(m.parts), unsigned(m.people.size()), in);
            return b;
        }
        case Type::Status:
            snprintf(b, sizeof(b), "status: %u units online, %u offline, %s, %u waiting for help, sender on %s", unsigned(m.online), unsigned(m.offline),
                     m.kind ? (std::string("EMERGENCY ") + kKinds[m.kind & 3]).c_str() : "no emergency", unsigned(m.helpWaiting), m.power ? "BATTERY" : "PoE");
            t = b;
            if (m.battery != 255) t += " (battery " + std::to_string(m.battery) + " %)";
            return t + ", running " + std::to_string(m.uptimeMin) + " min";
        case Type::Ack:
            snprintf(b, sizeof(b), "got the %s message of room %u (signal there %d dBm)", kTypeNames[m.ackType < 8 ? m.ackType : 0], unsigned(m.ackRoom), int(m.ackRssi));
            return b;
        case Type::Query:
            return m.what == 1 ? "asks for the people list" : "asks for something this firmware does not know";
        case Type::Test:
            return "test message (running " + std::to_string(m.uptimeS) + " s)";
    }
    return "?";
}

uint32_t airtimeMs(size_t bytes, const LoraConfig& c) {
    const double ts = double(1u << c.sf) * 1000.0 / double(c.bwHz);  // one symbol, ms
    const int de = ts > 16.0 ? 2 : 0;                                // low data rate optimisation
    const double num = 8.0 * double(bytes) - 4.0 * c.sf + 28 + 16;   // explicit header, CRC on
    const double sym = 8 + std::max(0.0, std::ceil(num / (4.0 * (c.sf - de)))) * c.cr;
    return uint32_t((8 + 4.25 + sym) * ts + 0.999);
}

}  // namespace lora

namespace {
constexpr uint32_t kSec = 1000 / kFrameMs;       // frames in a second
constexpr int kFastSends = 7;                    // at 0, 10 ... 60 s; then one a minute
constexpr uint32_t kFastGap = 10 * kSec, kSlowGap = 60 * kSec, kHeartbeat = 300 * kSec;
constexpr int kPeopleAfter = 3;                  // a people list follows each of the first 3 emergency messages
constexpr int kClearSends = 3;
constexpr int32_t kBudgetMs = 10000;             // airtime allowance: 10 s in hand, 1 % comes back
constexpr size_t kMaxQueue = 16, kMaxSenders = 64;
constexpr uint32_t kCounterBlock = 256;          // the counter is written to flash once per this many messages
constexpr uint32_t kSeenBlock = 32;              // ... and a sender's last counter once per this many
constexpr uint32_t kMinKHz = 903000, kMaxKHz = 927000;  // 500 kHz wide, inside 902-928 MHz

const char* const kUsage =
    "usage: lora | lora on|off | lora freq MHZ (903-927) | lora sf 7-12 | lora power 2-17 (dBm) | lora test | lora people | lora query | lora heard | lora forget";
}  // namespace

void LoraLink::begin(IStore* store, LoraRadio* radio, uint32_t seed, PeopleFn people) {
    store_ = store;
    radio_ = radio;
    people_ = std::move(people);
    rng_ = seed | 1;
    load();
    counter_ = counterSaved_ = uint32_t(std::strtoul(store_->loadText("lora_ctr").c_str(), nullptr, 10));
    seenSaved_.clear();
    const std::string s = store_->loadText("lora_seen");  // "room:counter,room:counter"
    for (size_t i = 0; i < s.size();) {
        unsigned room = 0;
        unsigned long ctr = 0;
        if (std::sscanf(s.c_str() + i, "%u:%lu", &room, &ctr) == 2 && seenSaved_.size() < kMaxSenders) seenSaved_[uint16_t(room)] = uint32_t(ctr);
        const size_t c = s.find(',', i);
        if (c == std::string::npos) break;
        i = c + 1;
    }
    seen_ = seenSaved_;
    if (on_) start();
}

void LoraLink::load() {
    on_ = store_->loadText("lora") == "on";
    cfg_ = LoraConfig();
    const unsigned long khz = std::strtoul(store_->loadText("lora_freq").c_str(), nullptr, 10);
    if (khz >= kMinKHz && khz <= kMaxKHz) cfg_.freqHz = uint32_t(khz) * 1000u;
    const int sf = std::atoi(store_->loadText("lora_sf").c_str());
    if (sf >= 7 && sf <= 12) cfg_.sf = uint8_t(sf);
    const std::string p = store_->loadText("lora_pwr");
    if (!p.empty() && std::atoi(p.c_str()) >= 2 && std::atoi(p.c_str()) <= 17) cfg_.powerDbm = int8_t(std::atoi(p.c_str()));
}

void LoraLink::start() {
    fitted_ = radio_ && radio_->begin(cfg_);
    sending_ = false;
}

// Each message takes the next number. The number in flash runs ahead by a block, so a restart never uses one twice
// and flash is written once per 256 messages.
uint32_t LoraLink::nextCounter() {
    if (counter_ >= counterSaved_) {
        counterSaved_ = counter_ + kCounterBlock;
        store_->saveText("lora_ctr", std::to_string(counterSaved_));
    }
    return ++counter_;
}

uint32_t LoraLink::rnd(uint32_t n) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return n ? rng_ % n : 0;
}

// Replay protection: a signed message is taken only if its counter is higher than the last one from that sender.
bool LoraLink::fresh(uint16_t from, uint32_t counter) {
    auto it = seen_.find(from);
    if (it != seen_.end() && counter <= it->second) return false;
    if (it == seen_.end() && seen_.size() >= kMaxSenders) return false;
    seen_[from] = counter;
    auto sv = seenSaved_.find(from);
    if (sv == seenSaved_.end() || counter >= sv->second + kSeenBlock) {
        seenSaved_[from] = counter;
        std::string t;
        for (const auto& kv : seenSaved_) t += (t.empty() ? "" : ",") + std::to_string(kv.first) + ":" + std::to_string(kv.second);
        store_->saveText("lora_seen", t);
    }
    return true;
}

void LoraLink::queue(const lora::Msg& m, bool front) {
    if (txq_.empty()) txAt_ = std::max(txAt_, frame_ + rnd(16));  // a small random wait before each send (0-300 ms)
    if (front) {
        txq_.push_front(m);
        if (txq_.size() > kMaxQueue) txq_.pop_back();
    } else if (txq_.size() < kMaxQueue) {
        txq_.push_back(m);
    }
}

void LoraLink::queuePeople() {
    const std::vector<lora::PeopleEntry> rows = people_ ? people_() : std::vector<lora::PeopleEntry>();
    const size_t parts = std::min<size_t>(15, std::max<size_t>(1, (rows.size() + lora::kMaxPeople - 1) / lora::kMaxPeople));
    for (size_t p = 0; p < parts; p++) {
        lora::Msg m;
        m.type = lora::Type::People;
        m.part = uint8_t(p);
        m.parts = uint8_t(parts);
        for (size_t i = p * lora::kMaxPeople; i < rows.size() && i < (p + 1) * lora::kMaxPeople; i++) m.people.push_back(rows[i]);
        queue(m);
    }
}

// The acting office unit's plan. A new emergency or call for help goes out at once, then every 10 s for the first
// minute, then once a minute while it lasts. "All clear" goes out 3 times. A heartbeat goes out every 5 minutes.
void LoraLink::schedule(const State& st) {
    if (!st.sender) {
        wasSender_ = false;
        emKind_ = 0;
        clearLeft_ = helpOverLeft_ = 0;
        helpRooms_.clear();
        return;
    }
    if (!wasSender_) {
        wasSender_ = true;
        statusNext_ = frame_ + 10 * kSec;
    }
    if (st.emergency != emKind_) {
        if (st.emergency) emSends_ = clearLeft_ = 0;
        else clearLeft_ = kClearSends;
        emKind_ = st.emergency;
        emNext_ = frame_;
    }
    if ((emKind_ || clearLeft_) && frame_ >= emNext_) {
        lora::Msg m;
        m.type = lora::Type::Emergency;
        m.kind = emKind_;
        m.origin = emKind_ ? st.origin : 0;
        m.ageS = emKind_ ? uint16_t(std::min<uint32_t>(st.emergencyS, 65535)) : 0;
        queue(m, true);  // ahead of anything else that waits
        if (emKind_) {
            emSends_++;
            emNext_ = frame_ + (emSends_ < kFastSends ? kFastGap : kSlowGap);
            if (emSends_ <= kPeopleAfter) queuePeople();
        } else {
            clearLeft_--;
            emNext_ = frame_ + kFastGap;
        }
    }

    std::vector<uint16_t> rooms;
    for (const lora::HelpEntry& h : st.help) rooms.push_back(h.room);
    std::sort(rooms.begin(), rooms.end());
    bool added = false;
    for (uint16_t r : rooms) added = added || !std::binary_search(helpRooms_.begin(), helpRooms_.end(), r);
    if (added) {
        helpSends_ = helpOverLeft_ = 0;
        helpNext_ = frame_;
    }
    if (rooms.empty() && !helpRooms_.empty()) {  // the last one was answered: say so twice
        helpOverLeft_ = 2;
        helpNext_ = frame_;
    }
    helpRooms_ = rooms;
    if ((!rooms.empty() || helpOverLeft_) && frame_ >= helpNext_) {
        lora::Msg m;
        m.type = lora::Type::Help;
        for (size_t i = 0; i < st.help.size() && i < lora::kMaxHelp; i++) m.help.push_back(st.help[i]);
        queue(m, !emKind_);
        if (rooms.empty()) {
            helpOverLeft_--;
            helpNext_ = frame_ + kFastGap;
        } else {
            helpSends_++;
            helpNext_ = frame_ + (helpSends_ < kFastSends ? kFastGap : kSlowGap);
        }
    }

    if (frame_ >= statusNext_) {  // the heartbeat: the receiver also sees from it that the link is alive
        statusNext_ = frame_ + kHeartbeat;
        lora::Msg m;
        m.type = lora::Type::Status;
        m.online = st.online;
        m.offline = st.offline;
        m.kind = st.emergency;
        m.power = st.power;
        m.battery = st.battery;
        m.helpWaiting = uint8_t(std::min<size_t>(st.help.size(), 255));
        m.uptimeMin = uint16_t(std::min<uint32_t>(st.uptimeS / 60, 65535));
        queue(m);
    }
}

// Sends the next waiting message once the radio is free. Starts the transmission and returns: it does not wait for it.
void LoraLink::transmit(const State& st) {
    // Asked every frame while a message is on the air: the driver goes back to receiving when it sees the end.
    if (sending_) sending_ = radio_->busy();
    if (sending_ || txq_.empty() || frame_ < txAt_) return;
    lora::Msg m = txq_.front();
    txq_.pop_front();
    m.from = st.room;
    uint8_t buf[lora::kMaxBytes];
    // Sized before the counter is used, so a message held back does not use up a number.
    const std::string key = netauth::loadKey(*store_);
    const size_t n0 = lora::encode(m, key, buf);
    const int32_t cost = int32_t(lora::airtimeMs(n0, cfg_));
    const bool alarm = m.type == lora::Type::Emergency || m.type == lora::Type::Help;
    // Airtime budget: alarms may overdraw it by as much again; everything else waits for airtime in hand.
    if (alarm ? budgetMs_ <= -kBudgetMs : budgetMs_ < cost) {
        overBudget_++;
        return;
    }
    m.counter = nextCounter();
    const size_t n = lora::encode(m, key, buf);
    if (!radio_->send(buf, n)) {
        sendFailed_++;
        return;
    }
    sent_++;
    sending_ = true;
    budgetMs_ -= cost;
    lastTx_ = lora::describe(m) + (key.empty() ? " (not signed)" : "") + ", " + std::to_string(n) + " bytes, " + std::to_string(cost) + " ms";
    lastTxAt_ = frame_;
    txAt_ = frame_ + uint32_t(cost) / kFrameMs + 6 + rnd(16);  // its time on the air, a gap for answers, a random wait
}

void LoraLink::receiveOne(const State& st, std::vector<Note>* notes) {
    uint8_t buf[lora::kMaxBytes + 4];
    int rssi = 0;
    float snr = 0;
    const int n = radio_->receive(buf, sizeof(buf), &rssi, &snr);
    if (n <= 0) return;
    const std::string key = netauth::loadKey(*store_);
    lora::Msg m;
    const lora::Check c = lora::decode(buf, size_t(n), key, &m);
    if (c == lora::Check::Malformed) {  // not one of ours, or damaged
        malformed_++;
        return;
    }
    if (m.from == st.room) return;  // a unit set to our own room number
    char sig[48];
    snprintf(sig, sizeof(sig), " (%d dBm, SNR %.1f dB)", rssi, double(snr));
    if (c == lora::Check::BadSignature) {
        badSig_++;
        lastRx_ = "REFUSED, wrong signature: a message that says it is from room " + std::to_string(m.from) + sig;
        lastRxAt_ = frame_;
        return;
    }
    if (c == lora::Check::Ok && !fresh(m.from, m.counter)) {
        replayed_++;
        lastRx_ = "REFUSED, old or repeated: a message from room " + std::to_string(m.from) + sig;
        lastRxAt_ = frame_;
        return;
    }
    received_++;
    const char* mark = c == lora::Check::Ok ? "" : c == lora::Check::NotSigned ? " (NOT SIGNED)" : " (signed, but not checked: no network key on this unit)";
    const std::string what = "radio, room " + std::to_string(m.from) + ": " + lora::describe(m) + mark;
    lastRx_ = what + sig;
    lastRxAt_ = frame_;
    // Trusted enough to answer: signed with our key, or nobody here uses a key. An alarm is only shown as an alarm
    // (Note::alarm) when it is signed with our key.
    const bool trusted = c == lora::Check::Ok || (c == lora::Check::NotSigned && key.empty());
    auto note = [&](bool fault, uint16_t room, const std::string& text) -> Note* {
        // Messages that are not signed go into the log once a minute at most, so a stranger cannot fill it.
        static const uint32_t kUntrustedGap = 60 * kSec;
        if (c != lora::Check::Ok) {
            if (untrustedAt_ && frame_ - untrustedAt_ < kUntrustedGap) return nullptr;
            untrustedAt_ = frame_ ? frame_ : 1;
        }
        notes->push_back(Note());
        Note& nn = notes->back();
        nn.fault = fault;
        nn.room = room;
        nn.text = text;
        return &nn;
    };
    // The same news is written to the log once, not at every repeat.
    auto changed = [&](uint32_t value) {
        const uint32_t k = uint32_t(m.from) << 8 | uint8_t(m.type);
        auto it = logged_.find(k);
        if (it != logged_.end() && it->second == value) return false;
        if (it == logged_.end() && logged_.size() >= 4 * kMaxSenders) logged_.clear();
        logged_[k] = value;
        return true;
    };
    auto ack = [&](uint8_t kind) {
        if (!trusted) return;
        auto it = acked_.find(m.from);
        if (it != acked_.end() && it->second.type == uint8_t(m.type) && it->second.kind == kind && frame_ - it->second.at < kSlowGap) return;  // once a minute is enough
        if (it == acked_.end() && acked_.size() >= kMaxSenders) acked_.clear();
        acked_[m.from] = AckDone{uint8_t(m.type), kind, frame_};
        lora::Msg a;
        a.type = lora::Type::Ack;
        a.ackRoom = m.from;
        a.ackCounter = m.counter;
        a.ackType = uint8_t(m.type);
        a.ackRssi = int8_t(std::max(-128, std::min(127, rssi)));
        queue(a);
        txAt_ = std::max(txAt_, frame_ + 15 + rnd(100));  // 0.3 to 2.3 s later, so several receivers do not answer at once
    };

    switch (m.type) {
        case lora::Type::Ack:
            if (st.sender && m.ackRoom == st.room) note(false, m.from, "radio: room " + std::to_string(m.from) + " " + lora::describe(m) + mark);
            return;
        case lora::Type::Query:
            if (st.sender && trusted && m.what == 1 && frame_ >= queryAt_) {
                queryAt_ = frame_ + 30 * kSec;
                queuePeople();
            }
            return;
        case lora::Type::Test:
            note(false, m.from, what + sig);
            return;
        default:
            break;
    }
    // The office's broadcast messages. A unit with a network link knows all this from the network already.
    if (st.linkUp) return;
    switch (m.type) {
        case lora::Type::Emergency: {
            Note* nn = nullptr;
            if (changed(uint32_t(m.kind + 1) | uint32_t(m.origin) << 8)) nn = note(m.kind != 0, m.origin ? m.origin : m.from, what);
            if (c == lora::Check::Ok) {
                if (!nn) {  // a repeat: nothing for the log, but the unit is told again
                    notes->push_back(Note());
                    nn = &notes->back();
                }
                nn->alarm = m.kind;
                nn->from = m.from;
            }
            ack(m.kind);
            break;
        }
        case lora::Type::Help: {
            uint32_t h = uint32_t(m.help.size());
            for (const lora::HelpEntry& e : m.help) h = h * 31 + e.room * 4u + (e.urgent ? 1 : 0) + (e.silent ? 2 : 0);
            if (changed(h)) {
                Note* nn = note(!m.help.empty(), m.help.empty() ? m.from : m.help[0].room, what);
                if (nn && c == lora::Check::Ok && !m.help.empty()) nn->chime = true;
            }
            if (!m.help.empty()) ack(uint8_t(m.help.size()));
            break;
        }
        case lora::Type::People:
            if (m.part == 0) {
                heard_.clear();
                heardMask_ = 0;
                heardParts_ = m.parts;
                heardFrom_ = m.from;
                heardSigned_ = c == lora::Check::Ok;
            }
            if (m.from == heardFrom_ && m.parts == heardParts_ && !(heardMask_ & (1u << m.part))) {
                heardMask_ |= 1u << m.part;
                heard_.insert(heard_.end(), m.people.begin(), m.people.end());
            }
            break;
        default:
            break;  // Status: kept as the last message received
    }
}

void LoraLink::tick(const State& st, std::vector<Note>* notes) {
    frame_ = st.frame;
    if (!on_ || !fitted_) return;
    if (frame_ - budgetAt_ >= kSec) {
        budgetAt_ = frame_;
        budgetMs_ = std::min(kBudgetMs, budgetMs_ + 10);
    }
    receiveOne(st, notes);
    schedule(st);
    transmit(st);
}

std::string LoraLink::status(const State& st) const {
    char b[200];
    std::string t = "LoRa side channel (draft): ";
    t += !on_ ? "off" : fitted_ ? "on, radio answers" : radio_ ? "on, but NO LoRa radio fitted (the chip does not answer)" : "on, but this unit has no LoRa radio";
    snprintf(b, sizeof(b), "\n  %.3f MHz, %u kHz wide, SF%u, coding 4/%u, %+d dBm; a %u-byte message is %u ms on the air", cfg_.freqHz / 1e6, unsigned(cfg_.bwHz / 1000),
             unsigned(cfg_.sf), unsigned(cfg_.cr), int(cfg_.powerDbm), unsigned(lora::kMaxBytes), unsigned(lora::airtimeMs(lora::kMaxBytes, cfg_)));
    t += b;
    t += std::string("\n  this unit ") + (st.sender ? "SENDS the alarms and the heartbeat (it is the acting office)" : "listens (only the acting office unit sends)");
    if (!st.linkUp) t += "; it has no network link, so it shows what it hears";
    t += std::string("\n  messages are ") + (netauth::loadKey(*store_).empty() ? "NOT SIGNED (no network key set: netkey)" : "signed with the network key");
    snprintf(b, sizeof(b), "\n  sent %u, received %u, refused %u (wrong signature %u, old or repeated %u, damaged or foreign %u), not sent %u (airtime limit %u, radio %u)",
             unsigned(sent_), unsigned(received_), unsigned(badSig_ + replayed_ + malformed_), unsigned(badSig_), unsigned(replayed_), unsigned(malformed_),
             unsigned(overBudget_ + sendFailed_), unsigned(overBudget_), unsigned(sendFailed_));
    t += b;
    snprintf(b, sizeof(b), "; airtime in hand %.1f s", budgetMs_ / 1000.0);
    t += b;
    auto ago = [&](uint32_t at) { return " (" + std::to_string((frame_ - at) / kSec) + " s ago)"; };
    t += "\n  last sent: " + (lastTx_.empty() ? std::string("nothing yet") : lastTx_ + ago(lastTxAt_));
    t += "\n  last received: " + (lastRx_.empty() ? std::string("nothing yet") : lastRx_ + ago(lastRxAt_));
    return t;
}

std::string LoraLink::command(const std::vector<std::string>& a, const State& st) {
    frame_ = st.frame;
    if (a.size() == 1) return status(st);
    const std::string& c = a[1];
    auto needRadio = [&]() -> const char* {
        if (!on_) return "LoRa is off (lora on)";
        if (!fitted_) return radio_ ? "no LoRa radio fitted (the chip does not answer)" : "this unit has no LoRa radio";
        return nullptr;
    };
    if ((c == "on" || c == "off") && a.size() == 2) {
        store_->saveText("lora", c == "on" ? "on" : "");
        on_ = c == "on";
        if (on_) {
            start();
            if (!fitted_) return radio_ ? "LoRa on, but NO LoRa radio fitted (the chip does not answer)" : "LoRa on, but this unit has no LoRa radio";
            return "LoRa on";
        }
        fitted_ = sending_ = false;
        txq_.clear();
        if (radio_) radio_->sleep();
        return "LoRa off";
    }
    if ((c == "freq" || c == "sf" || c == "power") && a.size() == 3) {
        if (c == "freq") {
            const double v = std::atof(a[2].c_str());
            const unsigned long khz = (unsigned long)(v < 10000 ? v * 1000.0 + 0.5 : v + 0.5);  // MHz, or kHz
            if (khz < kMinKHz || khz > kMaxKHz) return "the frequency must be 903 to 927 (MHz)";
            store_->saveText("lora_freq", std::to_string(khz));
        } else if (c == "sf") {
            const int v = std::atoi(a[2].c_str());
            if (v < 7 || v > 12) return "the spreading factor must be 7 to 12";
            store_->saveText("lora_sf", std::to_string(v));
        } else {
            const int v = std::atoi(a[2].c_str());
            if (v < 2 || v > 17) return "the power must be 2 to 17 (dBm)";
            store_->saveText("lora_pwr", std::to_string(v));
        }
        load();
        if (on_) start();
        return status(st) + "\n  every unit must use the same frequency and spreading factor";
    }
    if (c == "test" && a.size() == 2) {
        if (const char* e = needRadio()) return e;
        lora::Msg m;
        m.type = lora::Type::Test;
        m.uptimeS = st.uptimeS;
        queue(m);
        return "test message waiting to be sent (see: lora)";
    }
    if (c == "people" && a.size() == 2) {
        if (const char* e = needRadio()) return e;
        const size_t before = txq_.size();
        queuePeople();
        return "people list waiting to be sent, in " + std::to_string(txq_.size() - before) + " message(s)";
    }
    if (c == "query" && a.size() == 2) {
        if (const char* e = needRadio()) return e;
        lora::Msg m;
        m.type = lora::Type::Query;
        m.what = 1;
        queue(m);
        return "asking the office unit for the people list (see: lora heard)";
    }
    if (c == "heard" && a.size() == 2) {
        if (!heardParts_) return "no people list heard by radio yet";
        std::string t = "people list heard by radio from room " + std::to_string(heardFrom_) + (heardSigned_ ? "" : " (NOT SIGNED or not checked)") +
                        (heardPeopleComplete() ? ":" : " (NOT COMPLETE, a part is missing):");
        if (heard_.empty()) t += " no room with a people sensor";
        for (const lora::PeopleEntry& e : heard_) t += "\n" + lora::peopleText(e);
        return t;
    }
    if (c == "forget" && a.size() == 2) {
        seen_.clear();
        seenSaved_.clear();
        store_->saveText("lora_seen", "");
        return "message counters of the other units forgotten";
    }
    return kUsage;
}

}  // namespace ic
