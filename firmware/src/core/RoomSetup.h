// Setting the room number on the unit itself (the board has no DIP switches).
//
//   Hold MODE 3 s        enter setup; the first digit blinks
//   UP / DOWN            change the blinking digit
//   MODE (short)         next digit; after the 3 digits comes the sub-unit letter
//                        ("--" plus '-', a, b, c, d), for several units in one room
//   MODE on the letter   save; the unit takes its new IP address
//   20 s without input   cancel, nothing changes
#pragma once
#include <cstdint>

#include "Packet.h"

namespace ic {

class RoomSetup {
public:
    // Call once per frame with the held-button bitmask. Returns true when a new unit id
    // was saved into *id.
    bool update(uint8_t held, uint32_t frame, UnitId* id);
    bool active() const { return active_; }
    // Display text while active (blinking digit handled here).
    void render(uint32_t frame, char text[3], uint8_t* dots) const;

private:
    bool active_ = false;
    int field_ = 0;          // 0..2 digits, 3 = sub letter
    int digits_[3] = {};
    int sub_ = 0;
    uint8_t prev_ = 0;
    uint32_t modeDownAt_ = 0;
    bool modeLong_ = false;
    uint32_t lastInput_ = 0;
};

}  // namespace ic
