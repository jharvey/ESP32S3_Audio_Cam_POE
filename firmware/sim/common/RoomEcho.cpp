#include "RoomEcho.h"

#include <cmath>

namespace sim {

namespace {
constexpr int kFs = 16000;
constexpr int kHist = 4096;
const int kLineLen[4] = {613, 857, 1123, 1409};  // 38..88 ms, mutually prime
int ms(float m) { return int(m * kFs / 1000.0f); }
}  // namespace

const char* presetName(RoomPreset p) {
    switch (p) {
        case RoomPreset::Classroom: return "Classroom (RT60 0.6 s)";
        case RoomPreset::Gym: return "Gym (RT60 2 s)";
        default: return "None";
    }
}

RoomEcho::RoomEcho() {
    hist_.assign(kHist, 0.0f);
    for (int i = 0; i < kLines; i++) line_[i].assign(size_t(kLineLen[i]), 0.0f);
}

void RoomEcho::setPreset(RoomPreset p) {
    preset_ = p;
    taps_.clear();
    float rt60 = 0;
    switch (p) {
        case RoomPreset::None:
            revIn_ = 0;
            break;
        case RoomPreset::Classroom:
            taps_ = {{ms(2), 0.5f}, {ms(9), 0.22f}, {ms(16), 0.15f}, {ms(27), 0.10f}};
            preDelay_ = ms(20);
            revIn_ = 0.06f;
            rt60 = 0.6f;
            break;
        case RoomPreset::Gym:
            taps_ = {{ms(2), 0.5f}, {ms(24), 0.28f}, {ms(48), 0.22f}, {ms(75), 0.18f}};
            preDelay_ = ms(60);
            revIn_ = 0.10f;
            rt60 = 2.0f;
            break;
    }
    for (int i = 0; i < kLines; i++) {
        fb_[i] = rt60 > 0 ? std::pow(10.0f, -3.0f * kLineLen[i] / (kFs * rt60)) : 0.0f;
        for (auto& v : line_[i]) v = 0;
        lp_[i] = 0;
    }
}

void RoomEcho::process(const int16_t* speaker, float* echo, int n) {
    for (int j = 0; j < n; j++) {
        hist_[size_t(histPos_)] = float(speaker[j]);
        float y = 0;
        for (const Tap& t : taps_) y += t.gain * hist_[size_t((histPos_ - t.delay + kHist) % kHist)];

        if (revIn_ > 0) {
            float in = revIn_ * hist_[size_t((histPos_ - preDelay_ + kHist) % kHist)];
            float out[kLines];
            for (int i = 0; i < kLines; i++) out[i] = line_[i][size_t(linePos_[i])];
            // 4x4 Hadamard / 2: lossless mixing between the lines.
            float a = out[0] + out[1], b = out[0] - out[1], c = out[2] + out[3], d = out[2] - out[3];
            float mix[kLines] = {0.5f * (a + c), 0.5f * (b + d), 0.5f * (a - c), 0.5f * (b - d)};
            for (int i = 0; i < kLines; i++) {
                lp_[i] += 0.6f * (mix[i] - lp_[i]);  // air / wall absorption of highs
                line_[i][size_t(linePos_[i])] = in + fb_[i] * lp_[i];
                linePos_[i] = (linePos_[i] + 1) % kLineLen[i];
                y += 0.5f * out[i];
            }
        }
        echo[j] = y;
        histPos_ = (histPos_ + 1) % kHist;
    }
}

}  // namespace sim
