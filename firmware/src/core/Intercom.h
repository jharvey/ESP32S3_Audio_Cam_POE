// The intercom application: push-to-talk all-call with multi-talker mixing, echo
// cancellation with auto-tune and manual override, Ethernet/Wi-Fi link selection,
// room-number setup, status beacons and a command console (USB serial or network).
//
// The platform calls processFrame() once per 20 ms audio frame with the mic samples
// and sends the returned speaker samples to the amplifier.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "Calibrator.h"
#include "EchoCanceller.h"
#include "Hal.h"
#include "JitterBuffer.h"
#include "Mixer.h"
#include "RoomSetup.h"
#include "Tuning.h"

namespace ic {

struct IntercomOptions {
    int maxTaps = 2048;  // longest echo filter this platform can run in real time
};

struct Peer {
    StatusPacket status;
    uint32_t lastFrame = 0;
};

class Intercom {
public:
    Intercom(const Hal& hal, EchoCanceller& aec, IntercomOptions opt = IntercomOptions());

    void begin();
    void processFrame(const int16_t* mic, int16_t* speaker);

    // Console: one command line in, reply text out. Same commands over USB serial and the
    // network ("help" lists them). Call from the audio thread (or between frames).
    std::string command(const std::string& line);

    const TuneParams& params() const { return params_; }
    EchoCanceller& aec() { return aec_; }
    const UnitId& unit() const { return unit_; }
    LinkType link() const { return link_; }
    bool transmitting() const { return transmitting_; }
    bool inSetup() const { return setup_.active(); }
    bool calibrating() const { return cal_.active(); }
    int calProgress() const { return cal_.progressPct(); }
    const CalResult* lastCal() const { return haveCal_ ? &cal_.result() : nullptr; }
    int talkersPlaying() const { return playing_; }
    const std::vector<Peer>& peers() const { return peers_; }
    uint8_t micLevel() const { return micLevel_; }
    uint8_t sentLevel() const { return sentLevel_; }
    uint8_t speakerLevel() const { return spkLevel_; }
    uint32_t frame() const { return frame_; }
    // Talkers heard since start, for tests: (unit key, frames played).
    const std::vector<std::pair<uint32_t, uint32_t>>& heard() const { return heard_; }

private:
    struct Talker {
        UnitId id;
        JitterBuffer jb;
        uint32_t lastFrame = 0;
    };

    void logf(const char* fmt, ...);
    void applyParams();
    void startCal(const char* why);
    void finishCal();
    void updateLink();
    void receive();
    void sendStatus();
    void sendReply(const TextPacket& to, const std::string& text);
    void updatePanel();
    Talker* talkerFor(const UnitId& id);

    Hal hal_;
    EchoCanceller& aec_;
    IntercomOptions opt_;
    TuneParams params_;
    Calibrator cal_;
    bool haveCal_ = false;
    uint32_t showResultUntil_ = 0;
    uint32_t comboSince_ = 0;
    Mixer mixer_;
    RoomSetup setup_;
    UnitId unit_;
    LinkType link_ = LinkType::None;
    uint32_t frame_ = 0;
    int ethStable_ = 0;
    uint32_t linkLostAt_ = 0;
    bool everLinked_ = false;
    bool transmitting_ = false;
    uint16_t seq_ = 0;
    int playing_ = 0;
    uint8_t micLevel_ = 0, sentLevel_ = 0, spkLevel_ = 0;
    char shown_[3] = {0, 0, 0};
    uint8_t shownDots_ = 0xff;
    std::vector<std::unique_ptr<Talker>> talkers_;
    std::vector<Peer> peers_;
    std::vector<std::pair<uint32_t, uint32_t>> heard_;
};

}  // namespace ic
