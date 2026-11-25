// Firmware update over the network (R3.0): the web page or tools/net_update.py sends a new image in
// small pieces; this checks them and hands them to the board's spare firmware slot (IFirmware).
//
//   begin(size, sha)   start: size in bytes, optional SHA-256 of the whole image (hex)
//   data(offset, ...)  the next piece; offset must be exactly what has arrived so far
//   end()              all there, the hash matches, the image is ours -> the board switches slots
//
// What makes a bad update harmless: the running firmware is not touched until end() succeeds; pieces
// must arrive in order; the image must be the declared size, start with the ESP32 image byte, carry
// this firmware's tag (kFwTag: no other project's image gets in) and match the SHA-256 when one was
// given. A transfer that stalls for 30 s is dropped.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "Auth.h"
#include "Hal.h"

namespace ic {

constexpr size_t kFwMinBytes = 64 * 1024;
constexpr size_t kFwMaxBytes = 0x640000;  // one firmware slot of default_16MB.csv
constexpr size_t kFwMaxChunk = 8192;
constexpr uint8_t kFwMagic = 0xE9;        // first byte of every ESP32 application image
extern const char* const kFwTag;          // text every image of this firmware contains
constexpr uint32_t kFwStallFrames = 30 * 50;

class FwUpdate {
public:
    enum class State : uint8_t { Idle, Receiving, Done, Failed };

    explicit FwUpdate(IFirmware* fw) : fw_(fw) {}
    bool available() const { return fw_ != nullptr; }
    State state() const { return state_; }
    size_t size() const { return size_; }
    size_t received() const { return received_; }
    const std::string& error() const { return error_; }
    const std::string& sha() const { return gotSha_; }  // of the accepted image, after end()

    // Each returns "" when it worked, else what was wrong. frame = the unit's frame counter.
    std::string begin(size_t size, const std::string& shaHex, uint32_t frame);
    std::string data(size_t offset, const uint8_t* bytes, size_t len, uint32_t frame);
    std::string end();
    void abort(const std::string& why);
    void tick(uint32_t frame);  // drops a transfer that stalled

private:
    std::string failed(const std::string& why);

    IFirmware* fw_;
    State state_ = State::Idle;
    size_t size_ = 0, received_ = 0;
    uint32_t lastFrame_ = 0;
    std::string wantSha_, gotSha_, error_;
    Sha256 hash_;
    bool tagSeen_ = false;
    std::string carry_;  // the last bytes of the previous piece: the tag may straddle two pieces
};

}  // namespace ic
