// Front-panel buttons: which TM1637 key-scan position is which button.
//
// PCB wiring (from the board file):  PB1 = K1 + SEG1   PB2 = K2 + SEG1
//                                    PB4 = K1 + SEG3   PB5 = K2 + SEG2   (PB3 is not on the scan)
// Function of each button: ASSUMED (PB1 TALK, PB2 MODE, PB4 UP, PB5 DOWN). Change the table
// below if the case labels end up different.
#pragma once
#include <cstdint>

#include "Hal.h"
#include "Tm1637.h"

namespace board {

struct KeyMap {
    int k, seg;
    ic::Button button;
};
constexpr KeyMap kPanelKeys[] = {
    {1, 1, ic::kBtnTalk},  // PB1
    {2, 1, ic::kBtnMode},  // PB2
    {1, 3, ic::kBtnUp},    // PB4
    {2, 2, ic::kBtnDown},  // PB5
};

// TM1637 key code -> the core's button bits. Only one key at a time (chip limitation).
inline uint8_t buttonsFromKey(uint8_t code) {
    int k, seg;
    if (!Tm1637::keyIndex(code, &k, &seg)) return 0;
    for (const KeyMap& m : kPanelKeys)
        if (m.k == k && m.seg == seg) return uint8_t(1u << m.button);
    return 0;
}

}  // namespace board
