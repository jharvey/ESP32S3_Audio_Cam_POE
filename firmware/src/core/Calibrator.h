// Auto-tune: measures the real room and speaker/mic path, then picks echo-canceller settings.
//
// About 5.5 seconds, run from the panel (hold MODE + UP 3 s) or the console ("cal"):
//   1 s   silence, let the room settle
//   0.5 s silence, measure the noise floor
//   2 s   white-noise burst from the speaker, recording the mic
//   1 s   silence, recording how fast the room goes quiet (reverb time)
// Then:
//   cross-correlation of speaker vs mic -> echo delay (bulk delay) and impulse response
//   decay after the burst stops         -> RT60 (reverb time), echo tail length
//   mic vs speaker level                 -> acoustic coupling through the case
//   replay the recording through the canceller with old and new settings -> ERLE before/after
//   ERLE                                 -> residual echo suppression depth, ducking on/off
// Results are applied and saved; the previous settings can be restored with "undo".
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Config.h"
#include "Tuning.h"

namespace ic {

struct CalResult {
    bool ok = false;
    std::string message;     // one-line verdict (reason when !ok)
    std::string warnings;    // space-separated, may be empty
    float noiseDb = 0;       // room noise, dBFS at the mic
    float couplingDb = 0;    // mic echo level relative to speaker drive
    float delayMs = 0;       // chosen bulk delay
    float lagMs = 0;         // measured speaker->mic lag incl. the one-frame pipeline
    float tailMs = 0;        // time for the echo to fall 25 dB (from RT60)
    float rt60S = 0;         // 0 = could not estimate
    float peakToFloorDb = 0; // how clearly the echo stood out in the measurement
    float erleBeforeDb = 0;  // replayed with the settings before auto-tune
    float erleAfterDb = 0;   // replayed with the new settings
    std::string summary() const;
};

class Calibrator {
public:
    using Runner = std::function<void(std::function<void()>)>;
    // Where the analysis runs. Empty (default) = inline, inside frame().
    void setRunner(Runner r) { runner_ = std::move(r); }

    void start(const TuneParams& current, int maxTaps);
    // Stops the measurement. A background analysis already running finishes, but its result
    // is dropped.
    void abort();
    bool active() const { return phase_ != Phase::Idle; }
    bool measuring() const { return active() && phase_ != Phase::Analyzing; }  // room must stay quiet
    bool analyzing() const { return phase_ == Phase::Analyzing; }
    int progressPct() const;
    // One frame while measuring: raw mic in, speaker out. Returns true on the frame the result
    // is ready (only when the analysis runs inline).
    bool frame(const int16_t* mic, int16_t* speaker);
    // Call once per frame while analyzing in the background: true once, when the result is ready.
    bool poll();
    const CalResult& result() const { return res_; }
    const TuneParams& tuned() const { return tuned_; }

    // Analysis on a recording (public for tests). ref: speaker samples from burst start;
    // mic: recorded from the same frame. noisePow: mean square of the quiet mic.
    void analyze(const std::vector<int16_t>& ref, const std::vector<int16_t>& mic, double noisePow);

private:
    enum class Phase { Idle, Settle, Noise, Burst, Tail, Analyzing };
    static constexpr int kSettleFrames = 50, kNoiseFrames = 25, kBurstFrames = 100, kTailFrames = 50;

    Phase phase_ = Phase::Idle;
    int count_ = 0;
    TuneParams before_, tuned_;
    int maxTaps_ = 2048;
    std::vector<int16_t> ref_, mic_;
    double noiseAcc_ = 0;
    uint32_t rng_ = 1;
    CalResult res_;
    Runner runner_;
    std::atomic<bool> done_{false};
    bool cancelled_ = false;
};

}  // namespace ic
