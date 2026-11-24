// Front-panel buttons: which input of the second TCA9555 (U22, I2C 0x21) is which button.
//
// Board 75d75fb: every button switches its own U22 input to ground (TCA9555 internal pull-ups), so
// several buttons can be pressed at once (the TM1637 key scan they used before saw only one).
//   P00 PB1 TALK   P01 PB2 MODE   P02 PB4 UP   P03 PB5 DOWN   P04 PB6 hidden SETUP (under the cover)
//   P05 PB7  P06 PB8  P07 PB9  P10 PB10  P11 PB11  P12 PB12: more TALK buttons, any one talks
//   P13-P17 free.  (PB3 is the AXP2101 power button, not on U22.)
#pragma once
#include <cstdint>

#include "Hal.h"

namespace board {

constexpr uint8_t kButtonExpanderAddr = 0x21;

struct PinMap {
    int bit;  // 0..15 = P00..P17
    ic::Button button;
};
constexpr PinMap kPanelPins[] = {
    {0, ic::kBtnTalk},   // PB1
    {1, ic::kBtnMode},   // PB2
    {2, ic::kBtnUp},     // PB4
    {3, ic::kBtnDown},   // PB5
    {4, ic::kBtnSetup},  // PB6 (hidden)
    {5, ic::kBtnOffice}, // PB7 (front: OFFICE)
    {6, ic::kBtnTalk},   // PB8
    {7, ic::kBtnTalk},   // PB9
    {8, ic::kBtnTalk},   // PB10
    {9, ic::kBtnTalk},   // PB11
    {10, ic::kBtnTalk},  // PB12
};

// U22 input ports (bit n = Pn, P00..P17; a pressed button reads 0) -> the core's button bits.
inline uint8_t buttonsFromInputs(uint16_t inputs) {
    uint8_t held = 0;
    for (const PinMap& m : kPanelPins)
        if (!(inputs & (1u << m.bit))) held |= uint8_t(1u << m.button);
    return held;
}

}  // namespace board
