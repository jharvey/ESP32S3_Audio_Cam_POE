// Portable NLMS acoustic echo canceller with double-talk detection and a simple
// non-linear processor (residual echo suppression).
//
// The filter covers `taps` samples of echo path (1024 = 64 ms, 2048 = 128 ms at 16 kHz).
// Echo that arrives later than that (a gym's reverb tail) is not cancelled; the NLP and
// the intercom's ducking handle what is left.
#pragma once
#include <vector>

#include "Config.h"
#include "EchoCanceller.h"

namespace ic {

class NlmsAec : public EchoCanceller {
public:
    // bulkDelay: extra delay (samples) between speaker write and mic capture beyond the
    // one-frame pipeline delay (DMA buffers, converters). Measured at install time.
    explicit NlmsAec(int taps = 2048, int bulkDelay = 0, float mu = 0.15f);

    void capture(const int16_t* mic, int16_t* out) override;
    void playback(const int16_t* speaker) override;
    void setEnabled(bool on) override { enabled_ = on; }
    bool enabled() const override { return enabled_; }
    void reset() override;
    const AecStats& stats() const override { return stats_; }
    const char* name() const override { return "NLMS"; }

    void configure(const TuneParams& p) override;
    void setNlp(bool on) { nlp_ = on; }
    int taps() const { return taps_; }
    void setBulkDelay(int samples);

private:
    void allocate(int taps);
    int windowStart(long long t) const;  // mirrored-buffer index of the oldest tap for mic sample t

    int taps_;
    int delay_;  // total: one frame + bulk
    float mu_;
    float dtFactor_ = 8.0f;   // ~9 dB
    float nlpAtten_ = 0.25f;  // -12 dB
    bool enabled_ = true;
    bool nlp_ = true;
    int ringN_;
    std::vector<float> ring_;  // mirrored: 2 * ringN_
    std::vector<float> w_;
    long long refCount_ = 0;
    long long micCount_ = 0;
    double refAvg_ = 0;
    float nlpGain_ = 1.0f;
    int dtRun_ = 0;
    int dtHang_ = 0;
    AecStats stats_;
};

}  // namespace ic
