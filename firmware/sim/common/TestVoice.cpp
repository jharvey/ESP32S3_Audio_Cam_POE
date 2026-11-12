#include "TestVoice.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {
constexpr float kFs = 16000.0f;
constexpr float kPi = 3.14159265f;
const float kFormants[5][3] = {
    {730, 1090, 2440},  // a
    {530, 1840, 2480},  // e
    {270, 2290, 3010},  // i
    {570, 840, 2410},   // o
    {300, 870, 2240},   // u
};
const float kBw[3] = {90, 110, 170};
}  // namespace

TestVoice::TestVoice(float f0, uint32_t seed, float levelDb) : f0_(f0), levelDb_(levelDb), seed0_(seed), seed_(seed) {}

void TestVoice::restart() {
    seed_ = seed0_;
    sylLeft_ = pause_ = phraseLeft_ = 0;
    for (int i = 0; i < 3; i++) y1_[i] = y2_[i] = 0;
}

float TestVoice::rnd() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / float(1u << 24);
}

float TestVoice::next() {
    if (sylLeft_ <= 0) {
        if (pause_ > 0) {
            pause_--;
            t_ += 1.0 / kFs;
            for (int i = 0; i < 3; i++) { float y = y1_[i] * 0.995f; y2_[i] = y1_[i]; y1_[i] = y; }
            return 0.0f;
        }
        if (phraseLeft_ <= 0) {  // between phrases: breathe
            phraseLeft_ = 4 + int(rnd() * 6);
            pause_ = int(kFs * (0.25f + 0.35f * rnd()));
            return 0.0f;
        }
        phraseLeft_--;
        vowel_ = int(rnd() * 5) % 5;
        sylLen_ = sylLeft_ = int(kFs * (0.12f + 0.12f * rnd()));
    }
    float pos = 1.0f - float(sylLeft_) / float(sylLen_);
    sylLeft_--;
    float env = std::sin(kPi * pos);
    env *= env;

    float f0 = f0_ * (1.0f + 0.06f * std::sin(float(t_) * 2 * kPi * 0.7f) + 0.04f * (pos - 0.5f));
    phase_ += f0 / kFs;
    float src = 0.02f * (rnd() - 0.5f);
    if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
        src += 1.0f;
    }
    float y = 0;
    for (int i = 0; i < 3; i++) {
        float r = std::exp(-kPi * kBw[i] / kFs);
        float a1 = 2 * r * std::cos(2 * kPi * kFormants[vowel_][i] / kFs);
        float a2 = -r * r;
        float v = (1 - r) * src + a1 * y1_[i] + a2 * y2_[i];
        y2_[i] = y1_[i];
        y1_[i] = v;
        y += v * (i == 0 ? 1.0f : 0.6f);
    }
    t_ += 1.0 / kFs;
    return y * env;
}

void TestVoice::render(int16_t* out, int n) {
    const float target = 32768.0f * std::pow(10.0f, levelDb_ / 20.0f);
    for (int j = 0; j < n; j++) {
        float v = next();
        if (v != 0.0f) {  // level tracking only while voiced, so pauses don't pump the gain
            rms_ = 0.9995f * rms_ + 0.0005f * v * v;
            if (rms_ > 1e-10f) agc_ += 0.001f * (0.6f * target / std::sqrt(rms_) - agc_);
        }
        float s = std::max(-32000.0f, std::min(32000.0f, v * agc_));
        out[j] = int16_t(s);
    }
}

}  // namespace sim
