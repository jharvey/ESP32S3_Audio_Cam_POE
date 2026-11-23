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
    dec_.reset();  // a new talk burst starts a fresh decoder
}

AudioDecoder* JitterBuffer::decoderFor(Codec c) {
    if (!dec_ || dec_->codec() != c) dec_ = makeDecoder(c);
    return dec_.get();
}

void JitterBuffer::push(uint16_t seq, const int16_t* pcm, uint8_t level) {
    uint8_t buf[kMaxCodedBytes];
    for (int i = 0; i < kFrameSamples; i++) {
        buf[2 * i] = uint8_t(uint16_t(pcm[i]));
        buf[2 * i + 1] = uint8_t(uint16_t(pcm[i]) >> 8);
    }
    push(seq, Codec::Pcm16, buf, sizeof(buf), level);
}

void JitterBuffer::push(uint16_t seq, Codec codec, const uint8_t* data, size_t len, uint8_t level) {
    if (len > kMaxCodedBytes || !codecAvailable(codec)) return;
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
    s.codec = codec;
    s.len = uint16_t(len);
    std::memcpy(s.data, data, len);
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
        AudioDecoder* d = decoderFor(s.codec);
        if (!d || !d->decode(s.data, s.len, out)) {
            if (d) d->conceal(out);
            else std::memset(out, 0, sizeof(int16_t) * kFrameSamples);
        }
        *level = lastLevel_ = s.level;
        s.full = false;
        concealRun_ = 0;
    } else {
        if (++concealRun_ > kMaxConceal) {  // talker released the button (or link died)
            reset();
            return false;
        }
        concealedTotal_++;
        if (dec_) dec_->conceal(out);
        else std::memset(out, 0, sizeof(int16_t) * kFrameSamples);
        *level = lastLevel_ > 6 ? uint8_t(lastLevel_ - 6) : 0;
        lastLevel_ = *level;
    }
    playSeq_++;
    return true;
}

}  // namespace ic
