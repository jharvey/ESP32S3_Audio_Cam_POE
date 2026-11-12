// Echo canceller interface. The unit's own speaker output is the reference.
//
// Per frame, in this order:
//   capture(mic, out)   mic frame in, echo-cancelled frame out
//   playback(speaker)   the frame just sent to the speaker
//
// Implementations:
//   NlmsAec   portable time-domain NLMS (simulator, and a fallback on the S3)
//   EspSrAec  ESP-SR audio front end on the ESP32-S3 (AEC + NS + AGC), firmware only
#pragma once
#include <cstdint>

namespace ic {

struct AecStats {
    float erleDb = 0;      // echo return loss enhancement, smoothed, while far end is active
    bool farActive = false;
    bool doubleTalk = false;
    bool converged = false;
    float nlpGain = 1.0f;  // residual echo suppression gain applied this frame
};

class EchoCanceller {
public:
    virtual ~EchoCanceller() = default;
    virtual void capture(const int16_t* mic, int16_t* out) = 0;
    virtual void playback(const int16_t* speaker) = 0;
    virtual void setEnabled(bool on) = 0;
    virtual bool enabled() const = 0;
    virtual void reset() = 0;
    virtual const AecStats& stats() const = 0;
    virtual const char* name() const = 0;
};

}  // namespace ic
