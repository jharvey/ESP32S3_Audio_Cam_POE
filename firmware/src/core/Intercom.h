// The intercom application: push-to-talk all-call with multi-talker mixing, echo
// cancellation with auto-tune and manual override, Ethernet/Wi-Fi link selection,
// room-number setup, status beacons and a command console (USB serial or network).
//
// The platform calls processFrame() once per 20 ms audio frame with the mic samples
// and sends the returned speaker samples to the amplifier.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "AudioCodec.h"
#include "Calibrator.h"
#include "Coupling.h"
#include "EchoCanceller.h"
#include "Hal.h"
#include "JitterBuffer.h"
#include "Mixer.h"
#include "RoomSetup.h"
#include "Schedule.h"
#include "Sounds.h"
#include "Tuning.h"

namespace ic {

struct IntercomOptions {
    int maxTaps = 2048;  // longest echo filter this platform can run in real time
    // Runs auto-tune's analysis off the audio thread (seconds on the S3). Empty = run it inline.
    std::function<void(std::function<void()>)> runBackground;
    uint32_t unitUid = 0;  // unique per board (the ESP32's MAC); finds two units set to the same room
    // Wall clock, UTC seconds (NTP on the board, the PC clock in the simulator); 0 = not known yet.
    // Without it the office console's "time" command sets the clock.
    std::function<int64_t()> utcNow;
    // Restarts the unit ("reboot"); download = into the ROM's USB flashing mode. Empty = not available.
    std::function<void(bool download)> restart;
};

struct AudioStats {
    uint32_t framesPlayed = 0;   // received frames played out (all talkers)
    uint32_t concealed = 0;      // missing frames bridged by concealment
    uint32_t lateDrops = 0;      // frames that arrived after their playout time
};

// Where a console command came from. Some commands are refused from the network or web
// (the web password can only be set over USB).
enum class CmdSource : uint8_t { Local = 0, Network = 1, Web = 2 };

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
    std::string command(const std::string& line, CmdSource src = CmdSource::Local);

    const TuneParams& params() const { return params_; }
    EchoCanceller& aec() { return aec_; }
    const UnitId& unit() const { return unit_; }
    PowerState power() const { return power_; }
    IStore& store() const { return *hal_.store; }
    // Firmware update over the network (R3.0): off until "netupdate on" is typed on the USB console.
    IFirmware* firmware() const { return hal_.fw; }
    bool netUpdateAllowed() const { return hal_.fw && hal_.store->loadText("netupdate") == "on"; }
    bool toneActive() const { return toneFrames_ > 0; }
    LinkType link() const { return link_; }
    bool transmitting() const { return transmitting_; }
    bool selfTesting() const { return testPhase_ != 0; }
    bool inSetup() const { return setup_.active(); }
    bool calibrating() const { return cal_.active(); }
    int calProgress() const { return cal_.progressPct(); }
    const CalResult* lastCal() const { return haveCal_ ? &cal_.result() : nullptr; }
    int talkersPlaying() const { return playing_; }
    const std::vector<Peer>& peers() const { return peers_; }
    // Every unit heard since start, with when it was last heard (peers() drops silent ones after 3.5 s).
    const std::vector<Peer>& known() const { return known_; }
    // Replies to commands this unit sent ("send ..."), oldest first, numbered so the web page can poll for new ones.
    struct ReplyLine {
        uint32_t n;
        UnitId from;
        std::string text;
    };
    const std::vector<ReplyLine>& replies() const { return replies_; }
    const std::vector<CouplingResult>& couplings() const { return couplings_; }          // doors closed
    const std::vector<CouplingResult>& couplingsOpen() const { return couplingsOpen_; }  // doors open
    float couplingDb(uint16_t room) const { return coupling(room); }  // louder of the two, -99 = not heard
    bool couplingTest() const { return couple_.active(); }
    uint8_t micLevel() const { return micLevel_; }
    uint8_t sentLevel() const { return sentLevel_; }
    uint8_t speakerLevel() const { return spkLevel_; }
    bool groupTalking() const { return frame_ < groupTalkUntil_; }  // another unit in our room is talking
    uint32_t frame() const { return frame_; }
    uint16_t group() const { return group_ ? group_ : unit_.room; }
    bool duplicate() const { return frame_ < dupUntil_; }
    // What the display shows besides the room number: "dUP", "E1 ", "E2 ", "Lo ", or empty.
    const char* fault() const;
    Fault faultCode() const;
    uint8_t outputFaults() const { return outFaults_; }
    // Web console talk: 16 kHz mono PCM from the office PC's microphone (via the web page). While it
    // keeps coming, this unit transmits it instead of its own mic, marked as coming from the office.
    // Returns how many samples are waiting (the page uses it to see the unit keeps up).
    size_t webTalk(const int16_t* pcm, size_t samples);
    void setWebTarget(const TalkTarget& t) { if (webState_ == 0) webTarget_ = t; }  // fixed for one press
    bool webTalking() const { return webState_ == 2; }
    bool muted() const { return muteUntil_ != 0; }
    bool talkHeld() const { return talkSince_ && frame_ - talkSince_ >= kTalkHeldFrames; }
    // Room calls (R2.0).
    void setTalkTarget(const TalkTarget& t) { talkTarget_ = t; }
    const TalkTarget& talkTarget() const { return talkTarget_; }
    bool inCall() const;            // a call to this room is playing (display CAL)
    bool canAnswer() const;         // TALK now answers the caller only (during a call and 10 s after)
    const UnitId& caller() const { return callFrom_; }
    bool callAnswered() const { return callAnswered_; }       // TALK answered the last call
    bool testsLocked() const { return !hal_.store->loadText("testpass").empty(); }  // installer tests need a password
    bool missedCall() const { return frame_ < missedUntil_; }  // a call went unanswered: slow blink until a button
    bool callingOffice() const { return callingOffice_; }
    bool chiming() const { return soundActive_ && sound_ == Sound::Chime; }
    // Timed events (R2.1).
    int64_t utcNow() const;  // 0 = no clock yet
    const Schedule& schedule() const { return sched_; }
    bool soundPlaying() const { return soundActive_; }
    Sound currentSound() const { return sound_; }
    // Lamp outputs: what the core is driving now (0-100 %). Other = the call / alarm light.
    uint8_t lampLevel(Lamp l) const { return lamp_[int(l)]; }
    uint32_t rgb() const { return rgb_; }  // 0xRRGGBB on the RGB status LED
    bool bootTesting() const { return bootT0_ != 0; }
    Codec sendCodec() const { return encoder_ ? encoder_->codec() : Codec::Pcm16; }
    AudioStats audioStats() const;
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
    void applyCodec();
    void startCal(const char* why);
    void setUnit(const UnitId& id);
    void selfTestFrame(const int16_t* mic, int16_t* speaker);
    void finishCal();
    void updateLink();
    void receive();
    void sendStatus();
    void sendReply(const TextPacket& to, const std::string& text);
    std::string sendSigned(const std::string& target, const std::string& cmd);
    void updatePanel();
    Talker* talkerFor(const UnitId& id);
    bool sameGroup(const UnitId& from) const;
    bool isForMe(const AudioHeader& h) const;
    void setCallingOffice(bool on, const char* why);
    void startSound(Sound s);
    void checkSchedule();
    void updateLamps();
    void saveSchedule();
    std::string schedCommand(const std::vector<std::string>& a);
    void refreshPower();

    Hal hal_;
    EchoCanceller& aec_;
    IntercomOptions opt_;
    TuneParams params_;
    Calibrator cal_;
    CouplingTest couple_;
    std::vector<CouplingResult> couplings_;      // last coupling test with the doors closed (saved)
    std::vector<CouplingResult> couplingsOpen_;  // ... with the doors open (saved apart)
    bool coupleOpen_ = false;                    // the test running now is a doors-open one
    void finishCouple();
    float coupling(uint16_t room) const;     // dB vs our own speaker, -99 = not heard / unknown
    bool haveCal_ = false;
    uint32_t showResultUntil_ = 0;
    uint32_t comboSince_ = 0;
    Mixer mixer_;
    RoomSetup setup_;
    UnitId unit_;
    std::unique_ptr<AudioEncoder> encoder_;
    uint16_t group_ = 0;       // room group, 0 = this unit's own room number
    uint32_t dupUntil_ = 0;    // another unit with our room number was heard until this frame
    uint32_t restartAt_ = 0;   // "reboot": restart at this frame (0.5 s later, so the reply still goes out)
    bool restartDownload_ = false;
    uint32_t groupTalkUntil_ = 0;  // a unit in our room group was talking until this frame: our speaker stays quiet
    uint32_t dupUid_ = 0;
    uint8_t outFaults_ = 0;    // output switches shut down by a fault (IDiag::outputFaults)
    PowerState power_;         // read every second (I2C on the board), not every frame
    bool ethUp_ = false;
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
    std::vector<Peer> known_;
    std::vector<ReplyLine> replies_;  // last 200 reply lines to commands this unit sent
    uint32_t replyN_ = 0;
    uint64_t sentNonce_ = 0;          // last nonce this unit signed with (web console commands)  // up to 200 units, never pruned (the school status page shows them offline)
    std::vector<std::pair<uint32_t, uint32_t>> heard_;
    uint32_t toneFrames_ = 0;  // test tone frames left ("tone" command)
    static constexpr uint32_t kTalkHeldFrames = 120000 / kFrameMs;  // 2 minutes
    uint32_t muteUntil_ = 0;  // frame the office mute ends; 0 = not muted
    uint32_t talkSince_ = 0;  // frame TALK went down; 0 = not talking
    TalkTarget talkTarget_;
    uint16_t txTarget_ = 0;       // target and flags of the transmission in progress
    uint8_t txFlags_ = kAudioAllCall;
    UnitId callFrom_;             // last unit that called this room
    uint32_t callLast_ = 0;       // frame of its last packet; 0 = no call yet
    bool callAnswered_ = true;    // TALK answered it (until then the call LEDs blink to get attention)
    bool testsOk_ = false;        // running a command inside "tests <password> ..." (password checked)
    uint32_t missedUntil_ = 0;    // missed-call blink runs until this frame (or until a button is pressed)
    uint32_t ringUntil_ = 0;      // "ring": chime every 2 s until this frame or until TALK answers
    bool officeDown_ = false;     // OFFICE was held last frame (a press = one call)
    uint32_t allCallUntil_ = 0;   // an office all-call is playing: room calls wait
    uint8_t lamp_[int(Lamp::Count)] = {0, 0, 0};
    uint32_t rgb_ = 0;
    uint32_t bootT0_ = 0;  // power-up light test running since this frame; 0 = not running
    int bootTestStep(char text[3], uint8_t* dots, uint8_t lamps[], uint32_t* rgb) const;
    uint8_t lampManual_[int(Lamp::Count)] = {255, 255, 255};  // 255 = automatic
    bool soundActive_ = false;      // a chime / timed sound is playing
    Sound sound_ = Sound::Chime;
    uint32_t soundPos_ = 0;
    bool pendingSound_ = false;     // a timed sound waiting for a call to end
    Sound pending_ = Sound::Bell;
    uint32_t pendingSince_ = 0;
    Schedule sched_;
    int64_t setEpoch_ = 0;          // clock set by the "time" command, at frame setFrame_
    uint32_t setFrame_ = 0;
    int64_t lastSchedMinute_ = 0;   // the last minute the schedule was checked for
    bool callingOffice_ = false;
    uint32_t callingSince_ = 0, downSince_ = 0;
    uint32_t talkFrames_ = 0;  // "talk" command: transmit as if TALK were held
    std::vector<int16_t> webBuf_;  // web console talk audio waiting to be sent (up to 1 s)
    TalkTarget webTarget_;         // who web talk goes to (R2: everyone, a room, a floor or a group)
    int webState_ = 0;             // 0 idle, 1 buffering (waits for ~120 ms of audio), 2 sending
    uint32_t webLast_ = 0;         // frame the last web audio arrived
    uint64_t lastNonce_ = 0;   // last accepted network command (replay protection)
    int testPhase_ = 0;        // selftest: 0 idle, 1 quiet, 2 tone
    int testCount_ = 0;
    double testQuiet_ = 0, testLoud_ = 0;
    std::string testBoard_, testResult_;
    uint32_t tonePhase_ = 0;
    AudioStats retired_;  // counters of talkers already forgotten
};

}  // namespace ic
