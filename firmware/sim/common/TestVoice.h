// Synthetic talker: a pitched pulse train through vowel formants, in syllables and
// phrases, so each simulated room sounds distinct and audio can be measured.
#pragma once
#include <cstdint>

namespace sim {

class TestVoice {
public:
    explicit TestVoice(float f0 = 140.0f, uint32_t seed = 1, float levelDb = -22.0f);
    void render(int16_t* out, int n);
    void restart();

private:
    float next();
    float rnd();
    float f0_, levelDb_;
    uint32_t seed0_, seed_;
    double t_ = 0;
    float phase_ = 0;
    int vowel_ = 0;
    int sylLeft_ = 0, sylLen_ = 1, pause_ = 0, phraseLeft_ = 0;
    float y1_[3] = {}, y2_[3] = {};
    float agc_ = 1.0f, rms_ = 0.0f;
};

}  // namespace sim
