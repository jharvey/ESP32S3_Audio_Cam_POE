// Simulated acoustic echo: what a unit's mic hears of its own speaker.
// Direct coupling through the case + early reflections + a feedback-delay-network reverb
// tail with a chosen RT60 (classroom ~0.6 s, gym ~2 s).
#pragma once
#include <cstdint>
#include <vector>

namespace sim {

enum class RoomPreset { None = 0, Classroom = 1, Gym = 2 };
const char* presetName(RoomPreset p);

class RoomEcho {
public:
    RoomEcho();
    void setPreset(RoomPreset p);
    RoomPreset preset() const { return preset_; }
    // Speaker frame in (n samples), echo frame out (to add to the mic signal).
    void process(const int16_t* speaker, float* echo, int n);

private:
    struct Tap { int delay; float gain; };
    RoomPreset preset_ = RoomPreset::None;
    std::vector<Tap> taps_;
    float revIn_ = 0;
    std::vector<float> hist_;  // speaker history for direct + early taps
    int histPos_ = 0;
    static constexpr int kLines = 4;
    std::vector<float> line_[kLines];
    int linePos_[kLines] = {};
    float fb_[kLines] = {};
    float lp_[kLines] = {};
    int preDelay_ = 0;
};

}  // namespace sim
