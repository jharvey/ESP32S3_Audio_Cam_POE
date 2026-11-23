// Front-panel buttons: which TM1637 key-scan position is which button.
//
// PCB wiring (from the board file):  PB1 = K1 + SEG1   PB2 = K2 + SEG1   PB4 = K1 + SEG3   PB5 = K2 + SEG2
//                                    PB6 = K1 + SEG2   PB7 = K1 + SEG4   PB8 = K2 + SEG3   PB9 = K2 + SEG4
//                                    (PB3 is the AXP2101 power button, not on the scan)
// Function of each button: ASSUMED (PB1 TALK, PB2 MODE, PB4 UP, PB5 DOWN; PB6 hidden SETUP under the
// front cover; PB7-PB9 more TALK buttons, any one talks). Change the table if the case differs.
// Free key positions for later buttons: K1 + SEG5-8, K2 + SEG5-8.
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
    {1, 2, ic::kBtnSetup}, // PB6 (hidden)
    {1, 4, ic::kBtnTalk},  // PB7
    {2, 3, ic::kBtnTalk},  // PB8
    {2, 4, ic::kBtnTalk},  // PB9
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
