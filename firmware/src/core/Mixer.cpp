#include "Mixer.h"

#include <algorithm>
#include <cmath>

namespace ic {

int Mixer::mix(const MixInput* in, int n, float gain, int16_t* out) {
    int order[16];
    int count = std::min(n, 16);
    for (int i = 0; i < count; i++) order[i] = i;
    std::sort(order, order + count, [in](int a, int b) { return in[a].level > in[b].level; });
    count = std::min(count, kMaxMixTalkers);

    float acc[kFrameSamples] = {};
    for (int k = 0; k < count; k++) {
        const int16_t* p = in[order[k]].pcm;
        for (int i = 0; i < kFrameSamples; i++) acc[i] += p[i];
    }
    float peak = 0;
    for (int i = 0; i < kFrameSamples; i++) {
        acc[i] *= gain;
        peak = std::max(peak, std::fabs(acc[i]));
    }
    // Limiter: fast attack (this frame), slow release (~0.5 s), target peak -1 dBFS.
    const float ceiling = 29000.0f;
    float target = peak > ceiling ? ceiling / peak : 1.0f;
    float start = lim_;
    lim_ = target < lim_ ? target : lim_ + (target - lim_) * 0.04f;
    for (int i = 0; i < kFrameSamples; i++) {
        float g = start + (lim_ - start) * float(i) / kFrameSamples;  // ramp, no zipper noise
        float v = acc[i] * g;
        out[i] = int16_t(std::max(-32767.0f, std::min(32767.0f, v)));
    }
    return count;
}

}  // namespace ic
