#include "AudioCodec.h"

#include <cstring>

#ifdef IC_HAVE_OPUS
#include <opus.h>
#endif

namespace ic {

namespace {

// ---- PCM: little-endian 16-bit samples -------------------------------------------------
class PcmEncoder : public AudioEncoder {
public:
    Codec codec() const override { return Codec::Pcm16; }
    size_t encode(const int16_t* pcm, uint8_t* out, size_t cap) override {
        if (cap < size_t(kFrameSamples) * 2) return 0;
        for (int i = 0; i < kFrameSamples; i++) {
            out[2 * i] = uint8_t(uint16_t(pcm[i]));
            out[2 * i + 1] = uint8_t(uint16_t(pcm[i]) >> 8);
        }
        return size_t(kFrameSamples) * 2;
    }
};

class PcmDecoder : public AudioDecoder {
public:
    Codec codec() const override { return Codec::Pcm16; }
    bool decode(const uint8_t* d, size_t len, int16_t* out) override {
        if (len != size_t(kFrameSamples) * 2) return false;
        for (int i = 0; i < kFrameSamples; i++) out[i] = int16_t(uint16_t(d[2 * i] | (d[2 * i + 1] << 8)));
        std::memcpy(last_, out, sizeof(last_));
        return true;
    }
    void conceal(int16_t* out) override {  // repeat the last frame, 6 dB quieter each time
        for (int i = 0; i < kFrameSamples; i++) last_[i] = int16_t(last_[i] / 2);
        std::memcpy(out, last_, sizeof(last_));
    }

private:
    int16_t last_[kFrameSamples] = {};
};

#ifdef IC_HAVE_OPUS
// ---- Opus: voice mode, 20 ms frames -----------------------------------------------------
class OpusEnc : public AudioEncoder {
public:
    OpusEnc() {
        int err = 0;
        enc_ = opus_encoder_create(kSampleRate, 1, OPUS_APPLICATION_VOIP, &err);
        if (err != OPUS_OK) enc_ = nullptr;
        if (!enc_) return;
        opus_encoder_ctl(enc_, OPUS_SET_BITRATE(24000));
        opus_encoder_ctl(enc_, OPUS_SET_VBR(1));
        opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(5));  // to tune on the S3 against ESP-SR's CPU use
        opus_encoder_ctl(enc_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(enc_, OPUS_SET_INBAND_FEC(0));
    }
    ~OpusEnc() override {
        if (enc_) opus_encoder_destroy(enc_);
    }
    bool ok() const { return enc_ != nullptr; }
    Codec codec() const override { return Codec::Opus; }
    size_t encode(const int16_t* pcm, uint8_t* out, size_t cap) override {
        if (!enc_) return 0;
        opus_int32 n = opus_encode(enc_, pcm, kFrameSamples, out, opus_int32(cap));
        return n > 0 ? size_t(n) : 0;
    }
    void setBitrate(int bps) override {
        if (enc_) opus_encoder_ctl(enc_, OPUS_SET_BITRATE(bps));
    }
    void setFec(bool on, int lossPct) override {
        if (!enc_) return;
        opus_encoder_ctl(enc_, OPUS_SET_INBAND_FEC(on ? 1 : 0));
        opus_encoder_ctl(enc_, OPUS_SET_PACKET_LOSS_PERC(on ? lossPct : 0));
    }

private:
    OpusEncoder* enc_ = nullptr;
};

class OpusDec : public AudioDecoder {
public:
    OpusDec() {
        int err = 0;
        dec_ = opus_decoder_create(kSampleRate, 1, &err);
        if (err != OPUS_OK) dec_ = nullptr;
    }
    ~OpusDec() override {
        if (dec_) opus_decoder_destroy(dec_);
    }
    bool ok() const { return dec_ != nullptr; }
    Codec codec() const override { return Codec::Opus; }
    bool decode(const uint8_t* d, size_t len, int16_t* out) override {
        if (!dec_) return false;
        return opus_decode(dec_, d, opus_int32(len), out, kFrameSamples, 0) == kFrameSamples;
    }
    void conceal(int16_t* out) override {  // Opus packet-loss concealment
        if (!dec_ || opus_decode(dec_, nullptr, 0, out, kFrameSamples, 0) != kFrameSamples)
            std::memset(out, 0, sizeof(int16_t) * kFrameSamples);
    }

private:
    OpusDecoder* dec_ = nullptr;
};
#endif

}  // namespace

bool codecAvailable(Codec c) {
    if (c == Codec::Pcm16) return true;
#ifdef IC_HAVE_OPUS
    if (c == Codec::Opus) return true;
#endif
    return false;
}

std::unique_ptr<AudioEncoder> makeEncoder(Codec c) {
    if (c == Codec::Pcm16) return std::unique_ptr<AudioEncoder>(new PcmEncoder());
#ifdef IC_HAVE_OPUS
    if (c == Codec::Opus) {
        std::unique_ptr<OpusEnc> e(new OpusEnc());
        if (e->ok()) return std::unique_ptr<AudioEncoder>(e.release());
    }
#endif
    return nullptr;
}

std::unique_ptr<AudioDecoder> makeDecoder(Codec c) {
    if (c == Codec::Pcm16) return std::unique_ptr<AudioDecoder>(new PcmDecoder());
#ifdef IC_HAVE_OPUS
    if (c == Codec::Opus) {
        std::unique_ptr<OpusDec> d(new OpusDec());
        if (d->ok()) return std::unique_ptr<AudioDecoder>(d.release());
    }
#endif
    return nullptr;
}

const char* codecName(Codec c) { return c == Codec::Opus ? "Opus" : "PCM"; }

}  // namespace ic
