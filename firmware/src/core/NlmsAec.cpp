#include "NlmsAec.h"

#include <algorithm>
#include <cmath>

namespace ic {

namespace {
constexpr int kMaxBulkDelay = 8000;          // 0.5 s
constexpr double kFarActivePow = 1.0e4;      // mean square, ~ -50 dBFS
constexpr int kDtHangFrames = 5;
constexpr int kDtGiveUpFrames = 100;         // 2 s of "double talk" = echo path changed, re-adapt
constexpr float kNlpAtten = 0.25f;           // -12 dB on residual echo
}  // namespace

NlmsAec::NlmsAec(int taps, int bulkDelay, float mu) : taps_(taps), mu_(mu) {
    ringN_ = taps_ + 3 * kFrameSamples + kMaxBulkDelay;
    ring_.assign(2 * size_t(ringN_), 0.0f);
    w_.assign(size_t(taps_), 0.0f);
    setBulkDelay(bulkDelay);
}

void NlmsAec::setBulkDelay(int samples) {
    delay_ = kFrameSamples + std::max(0, std::min(samples, kMaxBulkDelay));
}

void NlmsAec::reset() {
    std::fill(w_.begin(), w_.end(), 0.0f);
    nlpGain_ = 1.0f;
    dtRun_ = dtHang_ = 0;
    stats_ = AecStats();
}

void NlmsAec::playback(const int16_t* speaker) {
    for (int i = 0; i < kFrameSamples; i++) {
        int pos = int(refCount_ % ringN_);
        ring_[pos] = ring_[pos + ringN_] = float(speaker[i]);
        refCount_++;
    }
}

int NlmsAec::windowStart(long long t) const {
    long long oldest = t - delay_ - taps_ + 1;
    long long m = oldest % ringN_;
    if (m < 0) m += ringN_;
    return int(m);
}

void NlmsAec::capture(const int16_t* mic, int16_t* out) {
    float e[kFrameSamples];
    double micP = 0, errP = 0, refP = 0;

    // Pass 1: filter with the current weights (output + statistics).
    for (int j = 0; j < kFrameSamples; j++) {
        const float* x = &ring_[size_t(windowStart(micCount_ + j))];
        double y = 0;
        for (int i = 0; i < taps_; i++) y += double(w_[size_t(i)]) * x[i];
        float d = float(mic[j]);
        e[j] = d - float(y);
        micP += double(d) * d;
        errP += double(e[j]) * e[j];
        refP += double(x[taps_ - 1]) * x[taps_ - 1];
    }
    micP /= kFrameSamples;
    errP /= kFrameSamples;
    refP /= kFrameSamples;

    // Far end counts as active while its last ~100 ms are audible (echo keeps arriving after
    // a syllable ends), not just the newest frame.
    refAvg_ = 0.8 * refAvg_ + 0.2 * refP;
    stats_.farActive = refAvg_ > kFarActivePow;
    // Double talk: this frame's echo reduction fell about 9 dB below what the filter
    // normally achieves. A fixed threshold fails in reverberant rooms, where the uncancellable
    // tail keeps the error high all the time.
    double expectErr = micP * std::pow(10.0, -double(stats_.erleDb) / 10.0);
    bool dtNow = stats_.converged && stats_.farActive && errP > 8.0 * expectErr && micP > 4.0 * kFarActivePow / 100.0;
    if (dtNow) {
        dtHang_ = kDtHangFrames;
        if (++dtRun_ > kDtGiveUpFrames) {  // not double talk: the echo path moved
            stats_.converged = false;
            stats_.erleDb = 0;
            dtRun_ = 0;
        }
    } else {
        dtRun_ = 0;
        if (dtHang_ > 0) dtHang_--;
    }
    stats_.doubleTalk = dtHang_ > 0;

    if (stats_.farActive && !stats_.doubleTalk) {
        float inst = float(10.0 * std::log10((micP + 1.0) / (errP + 1.0)));
        stats_.erleDb = 0.9f * stats_.erleDb + 0.1f * inst;
        stats_.converged = stats_.erleDb > 6.0f;
    }

    // Pass 2: NLMS adaptation, only while the far end talks and the near end does not.
    // Adapt only while the far end is loud right now. During its pauses the mic still holds
    // reverb of earlier speech that the filter window cannot explain; adapting on that
    // (small reference power, large error) throws the weights around.
    if (enabled_ && refP > kFarActivePow && !stats_.doubleTalk) {
        const float eps = float(taps_) * 1000.0f;
        const float* x0 = &ring_[size_t(windowStart(micCount_))];
        double px = 0;
        for (int i = 0; i < taps_; i++) px += double(x0[i]) * x0[i];
        for (int j = 0; j < kFrameSamples; j++) {
            const float* x = &ring_[size_t(windowStart(micCount_ + j))];
            if (j > 0) {
                float xin = x[taps_ - 1];
                float xout = ring_[size_t(windowStart(micCount_ + j - 1))];
                px += double(xin) * xin - double(xout) * xout;
                if (px < 0) px = 0;
            }
            double y = 0;
            for (int i = 0; i < taps_; i++) y += double(w_[size_t(i)]) * x[i];
            float err = float(mic[j]) - float(y);
            float k = mu_ * err / (float(px) + eps);
            for (int i = 0; i < taps_; i++) w_[size_t(i)] += k * x[i];
        }
    }
    micCount_ += kFrameSamples;

    // Residual echo suppression (NLP).
    float target = (enabled_ && nlp_ && stats_.farActive && !stats_.doubleTalk) ? kNlpAtten : 1.0f;
    float g0 = nlpGain_;
    nlpGain_ += (target - nlpGain_) * 0.5f;
    stats_.nlpGain = nlpGain_;

    for (int j = 0; j < kFrameSamples; j++) {
        float v;
        if (enabled_) {
            float g = g0 + (nlpGain_ - g0) * float(j) / kFrameSamples;
            v = e[j] * g;
        } else {
            v = float(mic[j]);
        }
        out[j] = int16_t(std::max(-32767.0f, std::min(32767.0f, v)));
    }
}

}  // namespace ic
