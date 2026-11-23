// Per-talker jitter buffer: reorders frames by sequence number, holds a small playout
// delay, and stops when the talker stops. Frames are kept encoded (PCM or Opus) and decoded
// in order at playout, so an Opus decoder sees its stream in sequence; a missing frame is
// concealed by the decoder (Opus PLC, or a fading repeat for PCM).
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "AudioCodec.h"
#include "Config.h"

namespace ic {

class JitterBuffer {
public:
    static constexpr int kSlots = 16;
    static constexpr int kTargetDepth = 3;  // frames buffered before playout (60 ms)
    static constexpr int kMaxConceal = 3;   // missing frames bridged before stopping

    // One encoded frame as it arrived (payload of an audio packet).
    void push(uint16_t seq, Codec codec, const uint8_t* data, size_t len, uint8_t level);
    // Raw PCM frame (tests and the PC console's own audio).
    void push(uint16_t seq, const int16_t* pcm, uint8_t level);
    // Called once per frame tick: decodes the next frame (or conceals a missing one) into out.
    // Returns false when there is nothing to play.
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
        Codec codec = Codec::Pcm16;
        uint16_t len = 0;
        uint8_t data[kMaxCodedBytes];
    };
    AudioDecoder* decoderFor(Codec c);

    Slot slots_[kSlots];
    std::unique_ptr<AudioDecoder> dec_;
    bool any_ = false;      // at least one frame since reset
    bool started_ = false;  // playout running
    uint16_t playSeq_ = 0;
    uint16_t highest_ = 0;
    int concealRun_ = 0;
    int idleTicks_ = 0;
    uint8_t lastLevel_ = 0;
    uint32_t lateDrops_ = 0;
    uint32_t concealedTotal_ = 0;
};

}  // namespace ic
