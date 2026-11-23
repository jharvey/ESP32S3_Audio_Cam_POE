#include "RoomSetup.h"

#include "Hal.h"

namespace ic {

namespace {
constexpr uint32_t kLongPressFrames = 150;  // 3 s
constexpr uint32_t kTimeoutFrames = 1000;   // 20 s
}  // namespace

bool RoomSetup::update(uint8_t held, uint32_t frame, UnitId* id) {
    uint8_t pressed = held & ~prev_;
    uint8_t released = prev_ & ~held;
    prev_ = held;
    const uint8_t mode = 1u << kBtnMode;
    const uint8_t setupKey = 1u << kBtnSetup;

    if (pressed & mode) modeLong_ = false;
    if (pressed & setupKey) modeDownAt_ = frame;  // only the hidden SETUP button starts setup
    if (!active_) {
        if ((held & setupKey) && frame - modeDownAt_ >= kLongPressFrames) {
            modeLong_ = !!(held & mode);  // a MODE press already down must not advance a field
            active_ = true;
            field_ = 0;
            digits_[0] = (id->room / 100) % 10;
            digits_[1] = (id->room / 10) % 10;
            digits_[2] = id->room % 10;
            lastInput_ = frame;
        }
        return false;
    }

    if (pressed) lastInput_ = frame;
    if (frame - lastInput_ > kTimeoutFrames) {
        active_ = false;
        return false;
    }
    int step = 0;
    if (pressed & (1u << kBtnUp)) step = 1;
    if (pressed & (1u << kBtnDown)) step = -1;
    if (step) digits_[field_] = (digits_[field_] + step + 10) % 10;
    if ((released & mode) && !modeLong_) {
        if (field_ < 2) {
            field_++;
        } else {
            active_ = false;
            id->room = uint16_t(digits_[0] * 100 + digits_[1] * 10 + digits_[2]);
            id->sub = 0;
            return true;
        }
    }
    return false;
}

void RoomSetup::render(uint32_t frame, char text[3], uint8_t* dots) const {
    bool blinkOff = (frame / 15) % 2;  // ~1.7 Hz
    *dots = 0;
    for (int i = 0; i < 3; i++) text[i] = char('0' + digits_[i]);
    if (blinkOff) text[field_] = ' ';
    *dots = 0x04;  // dot on the last digit = "setup"
}

}  // namespace ic
