#include "Coupling.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "Config.h"

namespace ic {

namespace {
constexpr double kPi = 3.14159265358979;
constexpr double kF0 = 300, kF1 = 3400;  // sweep band (the speaker and mics both work well here)
constexpr float kAmp = 0.25f;            // -12 dBFS
}  // namespace

std::string CouplingTest::start(const std::vector<uint16_t>& order, uint16_t me, uint32_t now, uint32_t slotFrames, bool autoGroup) {
    auto it = std::find(order.begin(), order.end(), me);
    if (it == order.end()) return "this unit's room is not in the list";
    if (order.size() > 60) return "at most 60 units per run";
    if (slotFrames < 40 || slotFrames > 150) return "slot must be 0.8-3 s";
    order_ = order;
    me_ = me;
    mySlot_ = int(it - order.begin());
    t0_ = now + 1;
    slot_ = slotFrames;
    autoGroup_ = autoGroup;
    noise_ = 0;
    noiseN_ = 0;
    acc_.assign(order.size(), 0.0);
    accN_.assign(order.size(), 0);
    sweepPos_ = 0;
    phase_ = 0;
    results_.clear();
    ok_ = false;
    active_ = true;
    return "";
}

bool CouplingTest::frame(uint32_t now, const int16_t* mic, int16_t* speaker, int n) {
    if (!active_ || now < t0_) return false;
    const uint32_t t = now - t0_;
    double p = 0;
    for (int i = 0; i < n; i++) p += double(mic[i]) * mic[i];
    p /= n;
    if (t < kLeadFrames) {
        if (t >= 5) {  // skip the first 0.1 s (sound still dying away)
            noise_ += p;
            noiseN_++;
        }
    } else {
        uint32_t k = (t - kLeadFrames) / slot_, off = (t - kLeadFrames) % slot_;
        if (k < order_.size()) {
            if (off >= 8 && off + 8 < slot_) {  // the middle of the slot: the sweep is playing everywhere
                acc_[k] += p;
                accN_[k]++;
            }
            const uint32_t sweepFrames = slot_ - 6;
            if (int(k) == mySlot_ && off >= 3 && off < 3 + sweepFrames) {  // our sweep
                const double len = double(sweepFrames) * n;
                for (int i = 0; i < n; i++, sweepPos_++) {
                    double x = sweepPos_ / len;                                 // 0..1
                    double f = kF0 * std::pow(kF1 / kF0, x);                     // exponential sweep
                    phase_ += 2 * kPi * f / kSampleRate;
                    double env = std::min(1.0, std::min(double(sweepPos_), len - sweepPos_) / 160.0);  // 10 ms fades
                    int v = speaker[i] + int(32767.0 * kAmp * env * std::sin(phase_));
                    speaker[i] = int16_t(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
                }
            }
        }
    }
    if (t + 1 < totalFrames()) return false;

    // Done: levels above the noise floor, relative to our own sweep.
    active_ = false;
    const double noise = noiseN_ ? noise_ / noiseN_ : 0;
    auto level = [&](size_t k, bool* heard) {
        double s = accN_[k] ? acc_[k] / accN_[k] : 0;
        *heard = s > noise * 1.6 + 1.0;  // about 2 dB above the noise
        return float(10 * std::log10(std::max(s - noise, 1e-3)));
    };
    bool ownHeard;
    ownDb_ = level(size_t(mySlot_), &ownHeard);
    ok_ = ownHeard;
    for (size_t k = 0; k < order_.size(); k++) {
        if (int(k) == mySlot_) continue;
        CouplingResult r;
        r.room = order_[k];
        float db = level(k, &r.heard);
        r.db = (r.heard && ok_) ? db - ownDb_ : -99;
        results_.push_back(r);
    }
    return true;
}

std::string CouplingTest::summary() const {
    if (!ok_ && !results_.empty()) return "coupling test FAILED: this unit didn't hear its own sweep (speaker or mic?)";
    if (results_.empty()) return "no coupling test run yet";
    std::string out = "coupling (dB vs this unit's own speaker):";
    for (const auto& r : results_) {
        char b[64];
        if (!r.heard) snprintf(b, sizeof(b), " %u not heard;", unsigned(r.room));
        else snprintf(b, sizeof(b), " %u %.0f dB%s;", unsigned(r.room), double(r.db),
                      r.db >= kSameRoomDb ? " (same room)" : r.db >= kCoupledDb ? " (through wall/door)" : "");
        out += b;
    }
    return out;
}

std::string CouplingTest::toString(const std::vector<CouplingResult>& r) {
    std::string s;
    char b[32];
    for (const auto& c : r) {
        snprintf(b, sizeof(b), "%s%u:%.1f", s.empty() ? "" : ",", unsigned(c.room), double(c.heard ? c.db : -99));
        s += b;
    }
    return s;
}

std::vector<CouplingResult> CouplingTest::fromString(const std::string& s) {
    std::vector<CouplingResult> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t c = item.find(':');
        if (c == std::string::npos) continue;
        CouplingResult r;
        r.room = uint16_t(std::strtoul(item.c_str(), nullptr, 10));
        r.db = std::strtof(item.c_str() + c + 1, nullptr);
        r.heard = r.db > -98;
        if (r.room) out.push_back(r);
    }
    return out;
}

}  // namespace ic
