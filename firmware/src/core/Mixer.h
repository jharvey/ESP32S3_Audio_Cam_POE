// Mixes the loudest talkers into one speaker frame with a smooth limiter, so several
// people pressing TALK at once are all heard without clipping.
#pragma once
#include <cstdint>

#include "Config.h"

namespace ic {

struct MixInput {
    const int16_t* pcm;
    uint8_t level;
};

class Mixer {
public:
    // Mixes up to kMaxMixTalkers of the n inputs (loudest first). gain is linear.
    // Returns how many inputs were mixed.
    int mix(const MixInput* in, int n, float gain, int16_t* out);
    float limiterGain() const { return lim_; }

private:
    float lim_ = 1.0f;
};

}  // namespace ic
