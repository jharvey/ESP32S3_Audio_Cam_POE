#include "Tm1637.h"

namespace board {

namespace {
constexpr uint8_t kCmdWriteAuto = 0x40;  // data command: write display, auto-increment address
constexpr uint8_t kCmdReadKeys = 0x42;   // data command: read key scan
constexpr uint8_t kCmdAddr0 = 0xC0;      // address command: GRID1
constexpr uint8_t kCmdDisplayOn = 0x88;  // display control: on, | brightness 0..7
}  // namespace

uint8_t Tm1637::segments(char c) {
    //                 gfedcba
    switch (c) {
        case '0': return 0x3F;
        case '1': return 0x06;
        case '2': return 0x5B;
        case '3': return 0x4F;
        case '4': return 0x66;
        case '5': return 0x6D;
        case '6': return 0x7D;
        case '7': return 0x07;
        case '8': return 0x7F;
        case '9': return 0x6F;
        case 'a': case 'A': return 0x77;
        case 'b': case 'B': return 0x7C;
        case 'c': return 0x58;
        case 'C': return 0x39;
        case 'd': case 'D': return 0x5E;
        case 'e': case 'E': return 0x79;
        case 'f': case 'F': return 0x71;
        case 'L': return 0x38;
        case 'r': return 0x50;
        case 'n': return 0x54;
        case 'o': return 0x5C;
        case 'P': return 0x73;
        case 'u': return 0x1C;
        case '-': return 0x40;
        default: return 0x00;
    }
}

bool Tm1637::keyIndex(uint8_t code, int* k, int* seg) {
    // Datasheet: K1 + SG1..SG8 = 0xF7..0xF0, K2 + SG1..SG8 = 0xEF..0xE8, no key = 0xFF.
    if (code >= 0xF0 && code <= 0xF7) {
        *k = 1;
        *seg = 0xF7 - code + 1;
        return true;
    }
    if (code >= 0xE8 && code <= 0xEF) {
        *k = 2;
        *seg = 0xEF - code + 1;
        return true;
    }
    return false;
}

void Tm1637::start() {
    l_.clk(true);
    l_.dio(true);
    l_.dio(false);  // DIO falls while CLK is high
}

void Tm1637::stop() {
    l_.clk(false);
    l_.dio(false);
    l_.clk(true);
    l_.dio(true);  // DIO rises while CLK is high
}

bool Tm1637::writeByte(uint8_t b) {
    for (int i = 0; i < 8; i++) {  // LSB first, data changes while CLK is low
        l_.clk(false);
        l_.dio((b >> i) & 1);
        l_.clk(true);
    }
    l_.clk(false);
    l_.dio(true);  // release for the chip's ACK
    l_.clk(true);
    bool ack = !l_.readDio();
    l_.clk(false);
    return ack;
}

uint8_t Tm1637::readByte() {
    uint8_t b = 0;
    l_.dio(true);
    for (int i = 0; i < 8; i++) {
        l_.clk(false);
        l_.clk(true);
        if (l_.readDio()) b |= uint8_t(1u << i);
    }
    l_.clk(false);  // ACK clock
    l_.clk(true);
    l_.clk(false);
    return b;
}

bool Tm1637::show(const char text[3], uint8_t dots, int brightness) {
    if (brightness < 0) brightness = 0;
    if (brightness > 7) brightness = 7;
    bool ok = true;
    start();
    ok &= writeByte(kCmdWriteAuto);
    stop();
    start();
    ok &= writeByte(kCmdAddr0);
    for (int i = 0; i < 3; i++) ok &= writeByte(uint8_t(segments(text[i]) | ((dots >> i) & 1 ? 0x80 : 0)));
    stop();
    start();
    ok &= writeByte(uint8_t(kCmdDisplayOn | brightness));
    stop();
    return ok;
}

bool Tm1637::readKeys(uint8_t* code) {
    start();
    bool ok = writeByte(kCmdReadKeys);
    *code = ok ? readByte() : 0xFF;
    stop();
    return ok;
}

}  // namespace board
