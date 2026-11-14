#include "Calibrator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "NlmsAec.h"

namespace ic {

namespace {
constexpr int kCorrLen = 16000;    // 1 s of burst used for the correlation
constexpr int kMaxLag = 5120;      // 320 ms delay search window
constexpr int kBlock = 160;        // 10 ms envelope blocks
constexpr int kMarginSamples = 32; // start the filter 2 ms before the direct path

double dbPow(double p) { return 10.0 * std::log10(p > 1e-12 ? p : 1e-12); }

// Replays a recording through a canceller, returns linear ERLE (no NLP) over frames [from, to).
float replayErle(const TuneParams& p, const std::vector<int16_t>& ref, const std::vector<int16_t>& mic, int from, int to) {
    TuneParams q = p;
    q.aec = true;
    q.nlp = false;
    NlmsAec aec(q.taps, 0, q.mu);
    aec.configure(q);
    aec.setNlp(false);
    int frames = std::min(int(mic.size()) / kFrameSamples, to);  // nothing after the window is needed
    double mp = 0, op = 0;
    int16_t out[kFrameSamples], zero[kFrameSamples] = {};
    for (int f = 0; f < frames; f++) {
        const int16_t* m = &mic[size_t(f) * kFrameSamples];
        aec.capture(m, out);
        size_t r = size_t(f) * kFrameSamples;
        aec.playback(r + kFrameSamples <= ref.size() ? &ref[r] : zero);
        if (f >= from && f < to) {
            for (int i = 0; i < kFrameSamples; i++) {
                mp += double(m[i]) * m[i];
                op += double(out[i]) * out[i];
            }
        }
    }
    return float(dbPow(mp + 1) - dbPow(op + 1));
}
}  // namespace

std::string CalResult::summary() const {
    char buf[400], rt[16] = "?";
    if (rt60S > 0) snprintf(rt, sizeof(rt), "%.2f s", double(rt60S));
    snprintf(buf, sizeof(buf),
             "%s | delay %.1f ms (lag %.1f) | tail %.0f ms | RT60 %s | coupling %+.1f dB | noise %.1f dBFS | "
             "echo/floor %.0f dB | ERLE before %.1f dB, after %.1f dB%s%s",
             message.c_str(), double(delayMs), double(lagMs), double(tailMs), rt,
             double(couplingDb), double(noiseDb), double(peakToFloorDb), double(erleBeforeDb), double(erleAfterDb),
             warnings.empty() ? "" : " | warn: ", warnings.c_str());
    return buf;
}

void Calibrator::abort() {
    if (phase_ == Phase::Analyzing) cancelled_ = true;  // the job still owns the buffers
    else phase_ = Phase::Idle;
}

bool Calibrator::poll() {
    if (phase_ != Phase::Analyzing || !done_.load(std::memory_order_acquire)) return false;
    phase_ = Phase::Idle;
    bool keep = !cancelled_;
    cancelled_ = false;
    return keep;
}

void Calibrator::start(const TuneParams& current, int maxTaps) {
    if (phase_ == Phase::Analyzing) return;
    cancelled_ = false;
    before_ = current;
    tuned_ = current;
    maxTaps_ = maxTaps;
    phase_ = Phase::Settle;
    count_ = 0;
    noiseAcc_ = 0;
    ref_.clear();
    mic_.clear();
    ref_.reserve(size_t(kBurstFrames) * kFrameSamples);
    mic_.reserve(size_t(kBurstFrames + kTailFrames) * kFrameSamples);
    res_ = CalResult();
}

int Calibrator::progressPct() const {
    int done = 0;
    switch (phase_) {
        case Phase::Settle: done = count_; break;
        case Phase::Noise: done = kSettleFrames + count_; break;
        case Phase::Burst: done = kSettleFrames + kNoiseFrames + count_; break;
        case Phase::Tail: done = kSettleFrames + kNoiseFrames + kBurstFrames + count_; break;
        case Phase::Analyzing: return 99;
        default: return 0;
    }
    return done * 100 / (kSettleFrames + kNoiseFrames + kBurstFrames + kTailFrames);
}

bool Calibrator::frame(const int16_t* mic, int16_t* speaker) {
    std::memset(speaker, 0, sizeof(int16_t) * kFrameSamples);
    switch (phase_) {
        case Phase::Idle:
        case Phase::Analyzing:
            return false;
        case Phase::Settle:
            if (++count_ >= kSettleFrames) { phase_ = Phase::Noise; count_ = 0; }
            return false;
        case Phase::Noise:
            for (int i = 0; i < kFrameSamples; i++) noiseAcc_ += double(mic[i]) * mic[i];
            if (++count_ >= kNoiseFrames) { phase_ = Phase::Burst; count_ = 0; }
            return false;
        case Phase::Burst: {
            // This tick's mic (recorded first) pairs with this tick's speaker frame, as in the
            // live pipeline: capture, then playback.
            mic_.insert(mic_.end(), mic, mic + kFrameSamples);
            float amp = 32768.0f * std::pow(10.0f, before_.calLevelDb / 20.0f) * 1.7320508f;  // uniform: rms = a / sqrt(3)
            for (int i = 0; i < kFrameSamples; i++) {
                int n = count_ * kFrameSamples + i, total = kBurstFrames * kFrameSamples;
                float fade = std::min(1.0f, std::min(float(n), float(total - 1 - n)) / 80.0f);  // 5 ms ramps
                rng_ = rng_ * 1664525u + 1013904223u;
                float u = float(rng_ >> 8) / float(1u << 24) * 2.0f - 1.0f;
                speaker[i] = int16_t(std::max(-32767.0f, std::min(32767.0f, u * amp * fade)));
            }
            ref_.insert(ref_.end(), speaker, speaker + kFrameSamples);
            if (++count_ >= kBurstFrames) { phase_ = Phase::Tail; count_ = 0; }
            return false;
        }
        case Phase::Tail:
            mic_.insert(mic_.end(), mic, mic + kFrameSamples);
            if (++count_ >= kTailFrames) {
                double noise = noiseAcc_ / (double(kNoiseFrames) * kFrameSamples);
                if (runner_) {
                    // Heavy part (cross-correlation, replays through the echo canceller) off the
                    // audio thread. Only the job touches ref_, mic_, res_ and tuned_ until done_.
                    phase_ = Phase::Analyzing;
                    done_.store(false);
                    runner_([this, noise] {
                        analyze(ref_, mic_, noise);
                        done_.store(true, std::memory_order_release);
                    });
                    return false;
                }
                phase_ = Phase::Idle;
                analyze(ref_, mic_, noise);
                return true;
            }
            return false;
    }
    return false;
}

void Calibrator::analyze(const std::vector<int16_t>& ref, const std::vector<int16_t>& mic, double noisePow) {
    res_ = CalResult();
    tuned_ = before_;
    res_.noiseDb = float(dbPow(noisePow) - dbPow(32768.0 * 32768.0));
    if (ref.size() < size_t(kCorrLen) || mic.size() < size_t(kCorrLen + kMaxLag)) {
        res_.message = "FAILED: recording too short";
        return;
    }
    if (res_.noiseDb > -30) {
        res_.message = "FAILED: room too noisy to measure, try again when quiet";
        return;
    }

    // Cross-correlation r(l) = sum x[n] y[n+l] / sum x^2  ~  impulse response for white x.
    double ex = 0;
    for (int n = 0; n < kCorrLen; n++) ex += double(ref[size_t(n)]) * ref[size_t(n)];
    std::vector<float> r(kMaxLag);
    for (int l = 0; l < kMaxLag; l++) {
        double s = 0;
        const int16_t* y = &mic[size_t(l)];
        for (int n = 0; n < kCorrLen; n++) s += double(ref[size_t(n)]) * y[n];
        r[size_t(l)] = float(s / ex);
    }
    float maxAbs = 0;
    for (float v : r) maxAbs = std::max(maxAbs, std::fabs(v));
    int lag = 0;
    while (lag < kMaxLag && std::fabs(r[size_t(lag)]) < 0.5f * maxAbs) lag++;  // first strong arrival = direct path

    // Measurement floor: lags before the direct path hold no echo (causality).
    int floorEnd = std::max(16, lag - 64);
    double fl = 0;
    for (int l = 0; l < floorEnd; l++) fl += double(r[size_t(l)]) * r[size_t(l)];
    double floorRms = std::sqrt(fl / floorEnd) + 1e-12;
    res_.peakToFloorDb = float(20.0 * std::log10(maxAbs / floorRms));
    res_.lagMs = float(lag) * 1000.0f / kSampleRate;

    // Coupling: echo level during the burst vs the speaker drive.
    double mp = 0, rp = 0;
    for (size_t i = size_t(10 * kFrameSamples); i < size_t(95 * kFrameSamples); i++) {
        mp += double(mic[i]) * mic[i];
        rp += double(ref[i]) * ref[i];
    }
    res_.couplingDb = float(dbPow(mp) - dbPow(rp));

    if (res_.peakToFloorDb < 15 || res_.couplingDb < -60) {
        res_.message = "FAILED: no clear echo - check the speaker and mics are working, or raise cal_level_db";
        return;
    }

    // Reverb: interrupted-noise method. When the burst stops, the room keeps ringing; the mic
    // level decays at 60 dB per RT60 down to the room noise. (The correlation above has too little
    // dynamic range for faint reverb; this has the full mic-signal-to-room-noise range.)
    int endAt = kBurstFrames * kFrameSamples + lag;  // burst end as heard at the mic
    double steady = 0;
    for (int i = endAt - 800; i < endAt - 80; i++) steady += double(mic[size_t(i)]) * mic[size_t(i)];
    steady /= 720.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int npts = 0;
    double noiseRel = dbPow(noisePow) - dbPow(steady);
    double lowLimit = std::max(-35.0, noiseRel + 10.0);
    float lastAboveMs = 0;
    for (int b = endAt + 80; b + kBlock <= int(mic.size()); b += kBlock) {
        double e = 0;
        for (int i = b; i < b + kBlock; i++) e += double(mic[size_t(i)]) * mic[size_t(i)];
        double d = dbPow(e / kBlock) - dbPow(steady);
        double t = double(b - endAt) / kSampleRate;
        if (d > lowLimit) lastAboveMs = float(t * 1000.0);
        if (d < -5 && d > lowLimit) {
            sx += t; sy += d; sxx += t * t; sxy += t * d; npts++;
        }
    }
    if (npts >= 3) {
        double slope = (npts * sxy - sx * sy) / (npts * sxx - sx * sx);
        if (slope < -1) res_.rt60S = float(-60.0 / slope);
    }
    // Still ringing above the room noise when the recording ends: reverb longer than measured.
    float recMs = float(int(mic.size()) - endAt) * 1000.0f / kSampleRate;
    bool tailCut = lastAboveMs > recMs - 60.0f;
    // Echo needing cancellation: until it is ~25 dB down.
    res_.tailMs = res_.rt60S > 0 ? res_.rt60S * 1000.0f * 25.0f / 60.0f : 60.0f;

    // New settings.
    res_.delayMs = float(std::max(0, lag - kFrameSamples - kMarginSamples)) * 1000.0f / kSampleRate;
    int need = int((res_.tailMs + 4.0f) * kSampleRate / 1000.0f) + kMarginSamples;
    int taps = ((need + 255) / 256) * 256;
    taps = std::max(256, std::min(taps, maxTaps_));
    tuned_.aec = true;
    tuned_.delayMs = res_.delayMs;

    // Try a few filter lengths on the recording and keep the best. Longer is not always better:
    // more taps adapt more slowly and pick up more noise. Prefer the shorter one within 1 dB.
    res_.erleBeforeDb = replayErle(before_, ref, mic, 70, kBurstFrames);
    int cands[4] = {taps, std::max(256, ((taps / 2 + 255) / 256) * 256), std::min(maxTaps_, 2048), std::min(maxTaps_, 1024)};
    float best = -100;
    int bestTaps = taps;
    for (int i = 0; i < 4; i++) {
        int c = cands[i];
        bool dup = false;
        for (int j = 0; j < i; j++) dup = dup || cands[j] == c;
        if (dup) continue;
        tuned_.taps = c;
        float e = replayErle(tuned_, ref, mic, 70, kBurstFrames);
        if (e > best + 1.0f || (e > best - 1.0f && c < bestTaps)) {
            if (e > best) best = e;
            bestTaps = c;
        }
    }
    tuned_.taps = bestTaps;
    res_.erleAfterDb = replayErle(tuned_, ref, mic, 70, kBurstFrames);
    taps = bestTaps;

    // Residual handling from how much the linear canceller achieves.
    float erle = res_.erleAfterDb;
    tuned_.nlp = true;
    // Starting points from the simulator; the console can override any of them per room.
    if (erle >= 15) { tuned_.nlpDb = -12; tuned_.duck = false; }
    else if (erle >= 8) { tuned_.nlpDb = -18; tuned_.duck = false; }
    else { tuned_.nlpDb = -24; tuned_.duck = true; }

    if (res_.noiseDb > -45) res_.warnings += "noisy-room ";
    if (res_.couplingDb > 6) res_.warnings += "strong-coupling(mic-too-close-or-case-leak) ";
    // Reverb longer than the canceller can reach (gym): echo leaks through during double talk,
    // so turn the speaker down while TALK is held.
    if (tailCut || res_.rt60S > 1.2f) {
        res_.warnings += "long-reverb(ducking-on) ";
        tuned_.duck = true;
    }
    if (!res_.warnings.empty() && res_.warnings.back() == ' ') res_.warnings.pop_back();
    char m[96];
    snprintf(m, sizeof(m), "OK taps=%d delay_ms=%.1f nlp_db=%.0f duck=%d", tuned_.taps, double(tuned_.delayMs),
             double(tuned_.nlpDb), tuned_.duck ? 1 : 0);
    res_.message = m;
    res_.ok = true;
}

}  // namespace ic
