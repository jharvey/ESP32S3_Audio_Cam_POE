// ESP-SR echo canceller (Espressif's esp_aec, prebuilt for the ESP32-S3) behind the core's
// EchoCanceller interface. Firmware only; the simulator keeps NlmsAec.
//
// esp_aec works on 16 ms blocks (256 samples); the intercom runs 20 ms frames (320). Small
// FIFOs bridge the two and add 16 ms to the sent audio. The echo reference is the speaker
// output, lined up one frame behind the mic plus the tuned delay (delay_ms).
//
// STATUS: builds and links (aec_pro_create from libesp_audio_processor); not yet run on a board.
// To confirm on hardware: aec_pro_create's mode values, CPU time per block, ERLE against NlmsAec.
// Next (roadmap 1.6a): the full ESP-SR audio front end with both mics, noise suppression and AGC.
#pragma once
#include <cstdint>
#include <vector>

#include "EchoCanceller.h"

namespace board {

class EspSrAec : public ic::EchoCanceller {
public:
    EspSrAec();
    ~EspSrAec() override;
    bool ok() const { return handle_ != nullptr; }  // false: esp_aec could not start, use NlmsAec

    void capture(const int16_t* mic, int16_t* out) override;
    void playback(const int16_t* speaker) override;
    void setEnabled(bool on) override { enabled_ = on; }
    bool enabled() const override { return enabled_; }
    void reset() override;
    void configure(const ic::TuneParams& p) override;
    const ic::AecStats& stats() const override { return stats_; }
    const char* name() const override { return "ESP-SR"; }

    static constexpr int kBlock = 256;  // 16 ms at 16 kHz

private:
    struct Fifo {
        std::vector<int16_t> buf;
        size_t head = 0, count = 0;
        void init(size_t cap) { buf.assign(cap, 0); head = count = 0; }
        void push(const int16_t* p, size_t n);
        void pushZeros(size_t n);
        void pop(int16_t* p, size_t n);
    };
    void create(int filterBlocks);
    void prime();

    void* handle_ = nullptr;
    int filterBlocks_ = 4;
    int delaySamples_ = 0;
    bool enabled_ = true;
    Fifo mic_, ref_, out_;
    int16_t* in_ = nullptr;   // 16-byte aligned blocks for esp_aec
    int16_t* refb_ = nullptr;
    int16_t* outb_ = nullptr;
    ic::AecStats stats_;
    double inEnergy_ = 0, outEnergy_ = 0;
    int blocks_ = 0;
};

}  // namespace board
