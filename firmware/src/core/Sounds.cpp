#include "Sounds.h"

#include <algorithm>
#include <cmath>

#include "Config.h"

namespace ic {

namespace {
const char* const kNames[] = {"chime", "bell", "lunch", "tone", "lockdown", "evacuate", "shelter", "allclear"};
constexpr float kTwoPi = 6.28318531f;

// Length of each sound in samples (16 kHz).
constexpr uint32_t kLen[] = {
    kSampleRate * 6 / 10,  // chime: 880 then 660 Hz, 0.3 s each (the room-call chime)
    kSampleRate * 2,       // bell: 2 s of a ringing school bell
    kSampleRate * 14 / 10, // lunch: three rising notes and a held fourth
    kSampleRate * 1,       // tone: 1 kHz, 1 s
    kSampleRate * 3,       // lockdown: slow low two-tone, 440 / 349 Hz, 0.5 s each
    kSampleRate * 3,       // evacuate: three rising sweeps, 500 -> 1500 Hz, 1 s each
    kSampleRate * 3,       // shelter: 700 Hz pulses, 0.25 s on / 0.25 s off
    kSampleRate * 12 / 10, // allclear: two calm rising notes
};

float note(float f, uint32_t i) { return std::sin(kTwoPi * f * float(i) / kSampleRate); }

// Short fade in and out (10 ms) so notes don't click.
float edge(uint32_t k, uint32_t len) { return std::min(1.0f, std::min(float(k), float(len - k)) / 160.0f); }

float sample(Sound s, uint32_t i) {
    switch (s) {
        case Sound::Chime: {
            const uint32_t half = kLen[0] / 2;
            return 0.20f * edge(i % half, half) * note(i < half ? 880.0f : 660.0f, i);
        }
        case Sound::Bell: {
            // A clapper striking about 18 times a second: each strike rings and decays.
            const uint32_t strike = kSampleRate / 18;
            float t = float(i % strike) / kSampleRate;
            float ring = std::exp(-t * 40.0f);
            float tone = 0.6f * note(1150.0f, i) + 0.3f * note(2310.0f, i) + 0.1f * note(3480.0f, i);
            return 0.30f * edge(i, kLen[1]) * ring * tone;
        }
        case Sound::Lunch: {
            static const float f[] = {523.25f, 659.25f, 783.99f, 1046.5f};  // C E G C
            const uint32_t step = kSampleRate * 3 / 10;
            uint32_t n = std::min<uint32_t>(i / step, 3);
            uint32_t k = i - n * step;
            uint32_t len = n < 3 ? step : kLen[2] - 3 * step;
            float decay = std::exp(-float(k) / kSampleRate * (n < 3 ? 3.0f : 2.0f));
            return 0.25f * edge(k, len) * decay * (0.8f * note(f[n], i) + 0.2f * note(2 * f[n], i));
        }
        case Sound::Tone:
            return 0.25f * edge(i, kLen[3]) * note(1000.0f, i);
        // The emergency signals are louder than the bells and unlike each other, so a room can tell
        // them apart without looking at the display.
        case Sound::Lockdown: {
            const uint32_t half = kSampleRate / 2;
            const uint32_t k = i % half;
            const bool low = (i / half) % 2 != 0;
            return 0.50f * edge(k, half) * (0.8f * note(low ? 349.23f : 440.0f, k) + 0.2f * note(low ? 698.46f : 880.0f, k));
        }
        case Sound::Evacuate: {
            const uint32_t seg = kSampleRate;
            const uint32_t k = i % seg;
            const float t = float(k) / kSampleRate;  // frequency 500 + 1000 t Hz, so phase = 2 pi (500 t + 500 t^2)
            return 0.50f * edge(k, seg) * std::sin(kTwoPi * (500.0f * t + 500.0f * t * t));
        }
        case Sound::Shelter: {
            const uint32_t on = kSampleRate / 4;
            const uint32_t k = i % (2 * on);
            return k < on ? 0.50f * edge(k, on) * note(700.0f, k) : 0.0f;
        }
        case Sound::AllClear: {
            const uint32_t half = kLen[7] / 2;
            const uint32_t k = i % half;
            return 0.25f * edge(k, half) * note(i < half ? 660.0f : 880.0f, k);
        }
        default:
            return 0;
    }
}
}  // namespace

const char* soundName(Sound s) { return uint8_t(s) < uint8_t(Sound::Count) ? kNames[uint8_t(s)] : "?"; }

bool soundFromName(const std::string& name, Sound* s) {
    for (uint8_t i = 0; i < uint8_t(Sound::Count); i++)
        if (name == kNames[i]) {
            *s = Sound(i);
            return true;
        }
    return false;
}

std::string soundList() {
    std::string out;
    for (uint8_t i = 0; i < uint8_t(Sound::Count); i++) out += (i ? " " : "") + std::string(kNames[i]);
    return out;
}

uint32_t soundSamples(Sound s) { return uint8_t(s) < uint8_t(Sound::Count) ? kLen[uint8_t(s)] : 0; }

bool renderSound(Sound s, uint32_t pos, int16_t* out, int n) {
    const uint32_t len = soundSamples(s);
    if (pos >= len) return false;
    for (int i = 0; i < n && pos + uint32_t(i) < len; i++) {
        int v = out[i] + int(32767.0f * sample(s, pos + uint32_t(i)));
        out[i] = int16_t(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
    }
    return pos + uint32_t(n) < len;
}

}  // namespace ic
