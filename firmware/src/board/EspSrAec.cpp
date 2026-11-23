#include "EspSrAec.h"

#include <cmath>
#include <cstring>

#include <esp_heap_caps.h>

extern "C" {
#include "esp_aec.h"
}

#include "Config.h"

namespace board {

namespace {
constexpr int kFrame = ic::kFrameSamples;  // 320
constexpr size_t kFifoCap = 4096;          // > frames + blocks + the longest delay (100 ms)
int16_t* alignedBlock() {
    return static_cast<int16_t*>(heap_caps_aligned_alloc(16, EspSrAec::kBlock * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}
}  // namespace

void EspSrAec::Fifo::push(const int16_t* p, size_t n) {
    for (size_t i = 0; i < n && count < buf.size(); i++, count++) buf[(head + count) % buf.size()] = p[i];
}
void EspSrAec::Fifo::pushZeros(size_t n) {
    for (size_t i = 0; i < n && count < buf.size(); i++, count++) buf[(head + count) % buf.size()] = 0;
}
void EspSrAec::Fifo::pop(int16_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (count) {
            p[i] = buf[head];
            head = (head + 1) % buf.size();
            count--;
        } else {
            p[i] = 0;
        }
    }
}

EspSrAec::EspSrAec() {
    in_ = alignedBlock();
    refb_ = alignedBlock();
    outb_ = alignedBlock();
    mic_.init(kFifoCap);
    ref_.init(kFifoCap);
    out_.init(kFifoCap);
    create(filterBlocks_);
    prime();
}

EspSrAec::~EspSrAec() {
    if (handle_) aec_destroy(handle_);
    heap_caps_free(in_);
    heap_caps_free(refb_);
    heap_caps_free(outb_);
}

void EspSrAec::create(int filterBlocks) {
    if (handle_) aec_destroy(handle_);
    // This ESP-SR build exports only aec_pro_create (16 ms frames, 1 mic). The mode values are
    // not documented in esp_aec.h; 0 is taken as the low-cost voice mode. To confirm on hardware.
    // filterBlocks is kept for the tuning interface; aec_pro sizes its own filter.
    handle_ = (in_ && refb_ && outb_) ? aec_pro_create(16, 1, 0) : nullptr;
    filterBlocks_ = filterBlocks;
}

// Output runs one block behind the input so a full 20 ms frame is always ready; the reference
// starts one frame (plus the tuned delay) behind the mic: capture() comes before playback().
void EspSrAec::prime() {
    mic_.init(kFifoCap);
    ref_.init(kFifoCap);
    out_.init(kFifoCap);
    out_.pushZeros(kBlock);
    ref_.pushZeros(size_t(kFrame + delaySamples_));
}

void EspSrAec::reset() {
    create(filterBlocks_);
    prime();
    stats_ = ic::AecStats();
    inEnergy_ = outEnergy_ = 0;
    blocks_ = 0;
}

void EspSrAec::configure(const ic::TuneParams& p) {
    enabled_ = p.aec;
    int blocks = p.taps / kBlock;
    blocks = blocks < 1 ? 1 : blocks > 8 ? 8 : blocks;  // 16..128 ms of echo tail
    int delay = int(p.delayMs * ic::kSampleRate / 1000.0f + 0.5f);
    delay = delay < 0 ? 0 : delay > 1600 ? 1600 : delay;
    if (blocks != filterBlocks_ || delay != delaySamples_ || !handle_) {
        delaySamples_ = delay;
        filterBlocks_ = blocks;
        reset();
    }
}

void EspSrAec::playback(const int16_t* speaker) { ref_.push(speaker, kFrame); }

void EspSrAec::capture(const int16_t* mic, int16_t* out) {
    if (!enabled_ || !handle_) {
        std::memcpy(out, mic, sizeof(int16_t) * kFrame);
        return;
    }
    mic_.push(mic, kFrame);
    while (mic_.count >= size_t(kBlock)) {
        mic_.pop(in_, kBlock);
        ref_.pop(refb_, kBlock);  // missing reference (startup) reads as silence
        aec_process(handle_, in_, refb_, outb_);
        out_.push(outb_, kBlock);

        double ei = 0, eo = 0, er = 0;
        for (int i = 0; i < kBlock; i++) {
            ei += double(in_[i]) * in_[i];
            eo += double(outb_[i]) * outb_[i];
            er += double(refb_[i]) * refb_[i];
        }
        stats_.farActive = er / kBlock > 100.0 * 100.0;  // reference above about -50 dBFS
        if (stats_.farActive) {  // ERLE only means something while the speaker plays
            inEnergy_ = 0.95 * inEnergy_ + 0.05 * ei;
            outEnergy_ = 0.95 * outEnergy_ + 0.05 * eo;
            stats_.erleDb = float(10.0 * std::log10((inEnergy_ + 1.0) / (outEnergy_ + 1.0)));
            stats_.converged = ++blocks_ > 60 && stats_.erleDb > 6.0f;
        }
    }
    out_.pop(out, kFrame);
}

}  // namespace board
