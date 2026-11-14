// TM1637 3-digit display + key scan, bit-banged through any two open-drain lines.
//
// On this board CLK and DIO are TCA9555 P00/P01 with 10k pull-ups (R38/R39): "release" =
// make the expander pin an input (pulled high), "low" = output driving 0. Every edge is an
// I2C write, so a display update takes ~12 ms and a key read ~5 ms; run it in its own task.
//
// Plain C++ (no Arduino), so the PC selftest checks the bit stream against a TM1637 model.
//
// Keys: the TM1637 reports ONE key at a time (the lowest code wins), so two buttons held
// together can't be detected. The panel's combos are designed around that.
#pragma once
#include <cstdint>

namespace board {

class Tm1637Lines {
public:
    virtual ~Tm1637Lines() = default;
    virtual void clk(bool high) = 0;  // high = release (pull-up), low = drive 0
    virtual void dio(bool high) = 0;
    virtual bool readDio() = 0;
};

class Tm1637 {
public:
    explicit Tm1637(Tm1637Lines& lines) : l_(lines) {}

    // text: 3 characters ('0'-'9', 'a'-'f', 'L', 'r', 'n', 'o', 'P', 'u', '-', ' ');
    // dots: bit n lights the decimal point after digit n. brightness 0..7.
    // Returns false if the chip didn't acknowledge (not fitted, wiring fault).
    bool show(const char text[3], uint8_t dots, int brightness = 4);
    // Raw key code from the chip: 0xFF = no key. See keyIndex().
    bool readKeys(uint8_t* code);

    // Segment pattern for one character (bit 0 = a ... bit 6 = g, bit 7 = dot).
    static uint8_t segments(char c);
    // Key code -> (k, seg): k = 1 or 2 (K1/K2), seg = 1..8. False for "no key" / invalid.
    static bool keyIndex(uint8_t code, int* k, int* seg);

private:
    void start();
    void stop();
    bool writeByte(uint8_t b);
    uint8_t readByte();

    Tm1637Lines& l_;
};

}  // namespace board
