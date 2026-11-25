#include "FwUpdate.h"

#include <cstring>

namespace ic {

const char* const kFwTag = "ic-fw:intercom-esp32s3";

namespace {
bool isHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
}  // namespace

std::string FwUpdate::failed(const std::string& why) {
    if (state_ == State::Receiving && fw_) fw_->finish(false);
    state_ = State::Failed;
    error_ = why;
    return why;
}

void FwUpdate::abort(const std::string& why) {
    if (state_ == State::Receiving) failed(why);
}

void FwUpdate::tick(uint32_t frame) {
    if (state_ == State::Receiving && frame - lastFrame_ > kFwStallFrames) failed("the transfer stalled for 30 s");
}

std::string FwUpdate::begin(size_t size, const std::string& shaHex, uint32_t frame) {
    if (!fw_) return "firmware updates are not available on this unit";
    if (state_ == State::Done) return "an update is already written; the unit is restarting";
    if (size < kFwMinBytes || size > kFwMaxBytes) return "size must be 65536 to 6553600 bytes";
    if (!shaHex.empty() && !isHex64(shaHex)) return "sha must be 64 lower-case hex digits";
    if (state_ == State::Receiving) fw_->finish(false);  // a new start replaces an unfinished transfer
    state_ = State::Idle;
    error_.clear();
    gotSha_.clear();
    if (!fw_->begin(size)) {
        state_ = State::Failed;
        error_ = "the unit could not start the update: " + fw_->error();
        return error_;
    }
    state_ = State::Receiving;
    size_ = size;
    received_ = 0;
    wantSha_ = shaHex;
    hash_.reset();
    tagSeen_ = false;
    carry_.clear();
    lastFrame_ = frame;
    return "";
}

std::string FwUpdate::data(size_t offset, const uint8_t* bytes, size_t len, uint32_t frame) {
    if (state_ != State::Receiving) return "no update in progress";
    if (len == 0 || len > kFwMaxChunk) return "a piece must be 1 to 8192 bytes";
    // A repeated piece (the sender did not see our answer) is not an error: say where we are.
    if (offset != received_) return "expected offset " + std::to_string(received_);
    if (len > size_ - received_) return failed("more data than the declared size");
    if (received_ == 0 && bytes[0] != kFwMagic) return failed("not an ESP32 firmware image");
    if (!fw_->write(bytes, len)) return failed("writing to flash failed: " + fw_->error());
    hash_.add(bytes, len);
    if (!tagSeen_) {
        const size_t tagLen = std::strlen(kFwTag);
        carry_.append(reinterpret_cast<const char*>(bytes), len);
        tagSeen_ = carry_.find(kFwTag) != std::string::npos;
        if (carry_.size() >= tagLen) carry_.erase(0, carry_.size() - (tagLen - 1));
        if (tagSeen_) carry_.clear();
    }
    received_ += len;
    lastFrame_ = frame;
    return "";
}

std::string FwUpdate::end() {
    if (state_ != State::Receiving) return "no update in progress";
    if (received_ != size_) return "only " + std::to_string(received_) + " of " + std::to_string(size_) + " bytes have arrived";
    std::string sha = hash_.hex();
    if (!tagSeen_) return failed("this is not intercom firmware (its tag is missing)");
    if (!wantSha_.empty() && sha != wantSha_) return failed("SHA-256 does not match: the image was damaged on the way");
    if (!fw_->finish(true)) {
        state_ = State::Failed;  // finish() already dropped it
        error_ = "the unit rejected the image: " + fw_->error();
        return error_;
    }
    state_ = State::Done;
    gotSha_ = sha;
    return "";
}

}  // namespace ic
