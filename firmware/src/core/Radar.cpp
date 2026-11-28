#include "Radar.h"

#include <cstring>

namespace ic {

namespace {
const uint8_t kHead2450[4] = {0xAA, 0xFF, 0x03, 0x00};
const uint8_t kHead2410[4] = {0xF4, 0xF3, 0xF2, 0xF1};
const size_t kLen2450 = 30;

// The LD2450 sends a coordinate as 15 bits of size with the top bit set for "positive".
int16_t coord(const uint8_t* p) {
    const uint16_t v = uint16_t(p[0] | (p[1] << 8));
    const int m = int(v & 0x7fff);
    return int16_t((v & 0x8000) ? m : -m);
}
}  // namespace

void RadarParser::feed(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (n_ == sizeof(buf_)) {  // no frame in a full buffer: keep the tail and go on looking
            std::memmove(buf_, buf_ + sizeof(buf_) / 2, sizeof(buf_) / 2);
            n_ = sizeof(buf_) / 2;
        }
        buf_[n_++] = data[i];
        parse();
    }
}

void RadarParser::parse() {
    for (;;) {
        size_t at = 0;  // drop everything before the first possible frame start
        while (at < n_ && buf_[at] != kHead2450[0] && buf_[at] != kHead2410[0]) at++;
        if (at) {
            std::memmove(buf_, buf_ + at, n_ - at);
            n_ -= at;
        }
        if (n_ < 4) return;
        if (std::memcmp(buf_, kHead2450, 4) == 0) {
            if (n_ < kLen2450) return;
            if (buf_[28] == 0x55 && buf_[29] == 0xCC) {
                RadarState s;
                s.kind = kRadarTracking;
                for (int k = 0; k < 3; k++) {
                    const uint8_t* t = buf_ + 4 + 8 * k;
                    const int16_t x = coord(t), y = coord(t + 2);
                    if (!x && !y) continue;
                    s.x[s.count] = x;
                    s.y[s.count] = y;
                    s.count++;
                }
                last_ = s;
                fresh_ = true;
                std::memmove(buf_, buf_ + kLen2450, n_ - kLen2450);
                n_ -= kLen2450;
                continue;
            }
        } else if (std::memcmp(buf_, kHead2410, 4) == 0) {
            if (n_ < 6) return;
            const size_t len = size_t(buf_[4] | (buf_[5] << 8));
            if (len >= 9 && len <= 64) {
                const size_t total = 6 + len + 4;
                if (n_ < total) return;
                const uint8_t* d = buf_ + 6;  // type, 0xAA, state, moving cm (2), energy, still cm (2), energy, ...
                if (d[1] == 0xAA && buf_[total - 4] == 0xF8 && buf_[total - 3] == 0xF7 && buf_[total - 2] == 0xF6 && buf_[total - 1] == 0xF5) {
                    RadarState s;
                    s.kind = kRadarPresence;
                    const uint8_t state = d[2];
                    if (state & 3) {
                        const unsigned cm = (state & 1) ? unsigned(d[3] | (d[4] << 8)) : unsigned(d[6] | (d[7] << 8));
                        s.count = 1;
                        s.y[0] = int16_t(cm > 3000 ? 30000 : cm * 10);
                    }
                    last_ = s;
                    fresh_ = true;
                    std::memmove(buf_, buf_ + total, n_ - total);
                    n_ -= total;
                    continue;
                }
            }
        }
        // not a frame after all: skip this byte and look again
        std::memmove(buf_, buf_ + 1, n_ - 1);
        n_--;
    }
}

bool RadarParser::take(RadarState* out) {
    if (!fresh_) return false;
    fresh_ = false;
    *out = last_;
    return true;
}

}  // namespace ic
