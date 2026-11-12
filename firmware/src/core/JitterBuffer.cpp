#include "JitterBuffer.h"

#include <cstring>

namespace ic {

namespace {
int16_t diff(uint16_t a, uint16_t b) { return int16_t(uint16_t(a - b)); }
}  // namespace

void JitterBuffer::reset() {
    for (auto& s : slots_) s.full = false;
    any_ = started_ = false;
    concealRun_ = idleTicks_ = 0;
}

void JitterBuffer::push(uint16_t seq, const int16_t* pcm, uint8_t level) {
    if (!any_) {
        any_ = true;
        playSeq_ = highest_ = seq;
    }
    if (diff(seq, playSeq_) < 0) {
        if (started_) {  // arrived after its playout time
            lateDrops_++;
            return;
        }
        playSeq_ = seq;  // reordered before playout began: start from the earliest frame
    }
    if (diff(seq, highest_) > 0) highest_ = seq;
    // Sender's clock ran ahead of ours (or a burst arrived): skip forward, keep target depth.
    if (diff(highest_, playSeq_) >= kSlots - 1) playSeq_ = uint16_t(highest_ - kTargetDepth + 1);

    Slot& s = slots_[seq % kSlots];
    s.full = true;
    s.seq = seq;
    s.level = level;
    std::memcpy(s.pcm, pcm, sizeof(s.pcm));
    idleTicks_ = 0;
    if (!started_ && diff(highest_, playSeq_) >= kTargetDepth - 1) started_ = true;
}

bool JitterBuffer::pop(int16_t* out, uint8_t* level) {
    if (!started_) {
        // A talker that sent fewer frames than the target depth still gets played.
        if (any_ && ++idleTicks_ > kTargetDepth) started_ = true;
        else return false;
    }
    Slot& s = slots_[playSeq_ % kSlots];
    if (s.full && s.seq == playSeq_) {
        std::memcpy(out, s.pcm, sizeof(s.pcm));
        std::memcpy(last_, s.pcm, sizeof(last_));
        *level = lastLevel_ = s.level;
        s.full = false;
        concealRun_ = 0;
    } else {
        if (++concealRun_ > kMaxConceal) {  // talker released the button (or link died)
            reset();
            return false;
        }
        concealedTotal_++;
        for (int i = 0; i < kFrameSamples; i++) last_[i] = int16_t(last_[i] / 2);  // -6 dB per frame
        std::memcpy(out, last_, sizeof(last_));
        *level = lastLevel_ > 6 ? uint8_t(lastLevel_ - 6) : 0;
        lastLevel_ = *level;
    }
    playSeq_++;
    return true;
}

}  // namespace ic
