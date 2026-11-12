// Per-talker jitter buffer: reorders frames by sequence number, holds a small playout
// delay, conceals short gaps by fading the last frame, and stops when the talker stops.
#pragma once
#include <cstdint>

#include "Config.h"

namespace ic {

class JitterBuffer {
public:
    static constexpr int kSlots = 16;
    static constexpr int kTargetDepth = 3;  // frames buffered before playout (60 ms)
    static constexpr int kMaxConceal = 3;   // missing frames bridged before stopping

    void push(uint16_t seq, const int16_t* pcm, uint8_t level);
    // Called once per frame tick. Returns false when there is nothing to play.
    bool pop(int16_t* out, uint8_t* level);
    bool playing() const { return started_; }
    void reset();

    uint32_t lateDrops() const { return lateDrops_; }
    uint32_t concealed() const { return concealedTotal_; }

private:
    struct Slot {
        bool full = false;
        uint16_t seq = 0;
        uint8_t level = 0;
        int16_t pcm[kFrameSamples];
    };
    Slot slots_[kSlots];
    bool any_ = false;      // at least one frame since reset
    bool started_ = false;  // playout running
    uint16_t playSeq_ = 0;
    uint16_t highest_ = 0;
    int concealRun_ = 0;
    int idleTicks_ = 0;
    uint8_t lastLevel_ = 0;
    int16_t last_[kFrameSamples] = {};
    uint32_t lateDrops_ = 0;
    uint32_t concealedTotal_ = 0;
};

}  // namespace ic
