// Audio codecs for the intercom's 20 ms frames (16 kHz mono, 320 samples).
//
//   PCM   raw 16-bit samples (bring-up and debugging; 256 kbit/s per talker)
//   Opus  libopus, voice mode, about 24 kbit/s; packet-loss concealment, optional in-band FEC
//
// Opus is compiled in when IC_HAVE_OPUS is defined (firmware: arduino-libopus; simulator: libopus from source).
// Plain C++17 plus libopus: no Arduino or ESP-IDF headers, so the simulator runs the same code.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "Packet.h"

namespace ic {

constexpr size_t kMaxCodedBytes = kFrameSamples * 2;  // PCM is the largest payload

class AudioEncoder {
public:
    virtual ~AudioEncoder() = default;
    virtual Codec codec() const = 0;
    // Encodes one frame (kFrameSamples samples); returns the payload size, 0 on error.
    virtual size_t encode(const int16_t* pcm, uint8_t* out, size_t cap) = 0;
    virtual void setBitrate(int bitsPerSecond) { (void)bitsPerSecond; }
    // In-band forward error correction for a lossy link (Wi-Fi); lossPct = expected packet loss.
    virtual void setFec(bool on, int lossPct) { (void)on; (void)lossPct; }
};

class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;
    virtual Codec codec() const = 0;
    // Decodes one payload into kFrameSamples samples; false if the payload is bad.
    virtual bool decode(const uint8_t* data, size_t len, int16_t* out) = 0;
    // A frame is missing: fill kFrameSamples samples from what came before.
    virtual void conceal(int16_t* out) = 0;
};

bool codecAvailable(Codec c);
std::unique_ptr<AudioEncoder> makeEncoder(Codec c);  // nullptr if that codec isn't built in
std::unique_ptr<AudioDecoder> makeDecoder(Codec c);
const char* codecName(Codec c);

}  // namespace ic
