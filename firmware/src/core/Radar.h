// mmWave people sensor on a serial line (Hi-Link, 256000 baud, the module just talks; nothing is sent to it).
//   HLK-LD2450: up to three people, each with a position (x across, y out from the sensor), ten times a second.
//   HLK-LD2410: someone is there or not, and how far away (no direction, no count).
// The parser finds either kind of frame in the byte stream, so the same firmware serves both modules.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ic {

enum : uint8_t { kRadarNone = 0, kRadarPresence = 1, kRadarTracking = 2 };

struct RadarState {
    uint8_t kind = kRadarNone;
    uint8_t count = 0;        // people seen (0-3); a presence sensor reports 0 or 1
    int16_t x[3] = {0, 0, 0};  // mm; + = to the right as the sensor looks into the room (0 for a presence sensor)
    int16_t y[3] = {0, 0, 0};  // mm out from the sensor
    uint8_t moving = 0;            // bit k set: person k is moving (the others sit or stand still)
    int16_t speed[3] = {0, 0, 0};  // LD2450 only: cm/s, + = away from the sensor
    uint8_t energy = 0;            // LD2410 only: how strong the echo is, 0-100
};

class RadarParser {
public:
    void feed(const uint8_t* data, size_t n);
    // true once for each complete frame; slots without a person have x = y = 0 and are not counted
    bool take(RadarState* out);

private:
    void parse();
    uint8_t buf_[96];
    size_t n_ = 0;
    RadarState last_;
    bool fresh_ = false;
};

}  // namespace ic
