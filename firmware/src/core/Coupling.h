// Room coupling test (roadmap 1.6e): which rooms hear each other through walls and open doors.
//
// The office sends "couple run ..." to every unit at once. Each unit then follows the same schedule:
//   0.5 s quiet (noise floor), then one slot per unit in the given order; in its own slot a unit plays a
//   0.8 s sweep (300-3400 Hz, -12 dBFS) while every unit records how loud its microphone gets.
// Result at each unit, per other room: how loud that room's sweep arrived compared with this unit's own
// sweep (dB). About 0 dB = the same room (gym units); -20 dB through a wall; "not heard" below the noise.
// The intercom uses it to group units in one room and to play coupled rooms a little quieter.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ic {

struct CouplingResult {
    uint16_t room = 0;
    float db = -99;      // relative to this unit's own sweep; -99 = not heard
    bool heard = false;
};

class CouplingTest {
public:
    static constexpr uint32_t kLeadFrames = 25;  // 0.5 s noise floor
    static constexpr float kSameRoomDb = -8;     // within 8 dB of our own speaker: same room
    static constexpr float kCoupledDb = -40;     // louder than this: heard through a wall / door (uninsulated stud walls)

    // order: every unit's room in sweep order (must include me). slotFrames: 50 = 1 s per unit.
    // Returns an error text, or "" when started.
    std::string start(const std::vector<uint16_t>& order, uint16_t me, uint32_t now, uint32_t slotFrames, bool autoGroup);
    bool active() const { return active_; }
    // Once per frame while active: measures the raw mic and adds our sweep to the speaker in our slot.
    // Returns true on the frame the test finishes (results ready).
    bool frame(uint32_t now, const int16_t* mic, int16_t* speaker, int n);

    const std::vector<CouplingResult>& results() const { return results_; }
    bool ok() const { return ok_; }          // our own sweep was heard (speaker and mic work)
    bool autoGroup() const { return autoGroup_; }
    std::string summary() const;
    uint32_t totalFrames() const { return kLeadFrames + uint32_t(order_.size()) * slot_ + 10; }
    // Saved form "101:-21.5,214:-99" and back.
    static std::string toString(const std::vector<CouplingResult>& r);
    static std::vector<CouplingResult> fromString(const std::string& s);

private:
    bool active_ = false, ok_ = false, autoGroup_ = false;
    std::vector<uint16_t> order_;
    uint16_t me_ = 0;
    int mySlot_ = -1;
    uint32_t t0_ = 0, slot_ = 50;
    double noise_ = 0;
    int noiseN_ = 0;
    std::vector<double> acc_;
    std::vector<int> accN_;
    uint32_t sweepPos_ = 0;
    double phase_ = 0;
    std::vector<CouplingResult> results_;
    float ownDb_ = -99;
};

}  // namespace ic
