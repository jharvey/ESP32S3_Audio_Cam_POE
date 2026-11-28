// LoRa side channel (DRAFT, not run on a radio yet).
//
// A slow, long-range radio link (RFM95 / SX1276, 915 MHz) beside the network. It tells a person outside
// (parking lot, playground) about alarms when the network or the building's power is in trouble.
// Only the acting office unit sends the broadcast messages, so 35 units do not talk over each other.
//
// Nothing that arrives by radio can change a setting, and nothing that arrives by radio can start, change
// or clear an emergency on a unit that has a network link. Radio input is shown, logged and acknowledged.
// (One narrow case is decided outside this file, in IntercomLora.cpp: a unit with NO network link shows a
// signed alarm from an office unit.)
//
// Message, all multi-byte fields little-endian:
//   0     magic 0xC5
//   1     bits 0-3 type, bits 4-6 format version (0), bit 7 = signed
//   2-3   sender room u16
//   4-7   counter u32 (goes up by one for each message of this sender; an old one is refused)
//   8..   payload, see below
//   last  8 bytes: the first 8 bytes of HMAC-SHA256(network key, "lora1|" + every byte before), signed only
//
// Payloads:
//   EMERGENCY 1: kind u8 (0 all clear, 1 lockdown, 2 evacuate, 3 shelter) | room whose red button started
//                it u16 (0 = the office) | seconds since it started u16 (65535 = longer)
//   HELP      2: count u8 (0 = no help call waiting any more), then count x
//                (room u16 | flags u8: bit 0 help needed, bit 1 silent | seconds waiting u16), up to 8
//   PEOPLE    3: u8: high half = part number (from 0), low half = number of parts, then 2 bytes per room:
//                bits 0-9 room, bits 10-12 count code (0 empty, 1, 2, 3 = three or more, 4 = someone,
//                cannot count), bits 13-15 how long empty (0 under 1 min, 1 = 1 min, 2 = 5 min,
//                3 = 15 min, 4 = 1 hour, 5 = over 4 hours, 7 = nobody since the sensor started); up to 20
//   STATUS    4: units online u8 | units offline u8 | emergency kind u8 | power u8 (0 PoE, 1 battery)
//                | battery percent u8 (255 = no battery) | help calls waiting u8 | minutes running u16
//   ACK       5: "I got it": sender room of the message u16 | its counter u32 | its type u8 | signal i8 (dBm)
//   QUERY     6: what u8 (1 = send the people list now)
//   TEST      7: seconds running u32
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Hal.h"

namespace ic {
namespace lora {

constexpr uint8_t kMagic = 0xC5;
constexpr size_t kHeaderBytes = 8, kSigBytes = 8, kMaxBytes = 60;
constexpr uint8_t kFlagSigned = 0x80;
constexpr size_t kMaxHelp = 8, kMaxPeople = 20;

enum class Type : uint8_t { Emergency = 1, Help = 2, People = 3, Status = 4, Ack = 5, Query = 6, Test = 7 };

struct HelpEntry {
    uint16_t room = 0;
    bool urgent = false, silent = false;
    uint16_t waitS = 0;
};
// People count codes.
constexpr uint8_t kPeopleEmpty = 0, kPeopleThreePlus = 3, kPeopleSomeone = 4;
struct PeopleEntry {
    uint16_t room = 0;
    uint8_t code = 0;   // 0 empty, 1, 2, 3 = three or more, 4 = someone (the sensor cannot count)
    uint8_t quiet = 0;  // how long empty, see the table above
};
uint8_t quietCode(uint8_t minutes);  // from StatusPacket::radarQuietMin
std::string peopleText(const PeopleEntry& e);

struct Msg {
    Type type = Type::Test;
    uint16_t from = 0;
    uint32_t counter = 0;
    bool isSigned = false;         // set by decode
    uint8_t kind = 0;              // Emergency, Status: 0 none / all clear, 1 lockdown, 2 evacuate, 3 shelter
    uint16_t origin = 0;           // Emergency: the room whose red button started it, 0 = the office
    uint16_t ageS = 0;             // Emergency: seconds since it started
    std::vector<HelpEntry> help;   // Help
    uint8_t part = 0, parts = 1;   // People
    std::vector<PeopleEntry> people;
    uint8_t online = 0, offline = 0, power = 0, battery = 255, helpWaiting = 0;  // Status
    uint16_t uptimeMin = 0;
    uint16_t ackRoom = 0;          // Ack
    uint32_t ackCounter = 0;
    uint8_t ackType = 0;
    int8_t ackRssi = 0;
    uint8_t what = 0;              // Query: 1 = the people list
    uint32_t uptimeS = 0;          // Test
};

// Writes the message into out (kMaxBytes); signed when key is not empty. Returns its size.
size_t encode(const Msg& m, const std::string& key, uint8_t* out);
enum class Check : uint8_t { Ok = 0, NotSigned, NoKey, BadSignature, Malformed };
// Ok = signed with our key. NotSigned = the sender has no key. NoKey = signed, but this unit has no key to check it.
Check decode(const uint8_t* buf, size_t len, const std::string& key, Msg* m);
std::string describe(const Msg& m);  // one line of plain text
// Time on the air in ms (explicit header, CRC on, 8 symbols of preamble).
uint32_t airtimeMs(size_t bytes, const LoraConfig& c);

}  // namespace lora

class LoraLink {
public:
    // What the unit knows right now. The unit fills this in for every tick.
    struct State {
        uint32_t frame = 0;
        uint16_t room = 0;
        bool sender = false;   // this unit may send the broadcast messages (it is the acting office)
        bool linkUp = true;    // it has a network link
        uint8_t emergency = 0;
        uint16_t origin = 0;
        uint32_t emergencyS = 0;
        std::vector<lora::HelpEntry> help;  // rooms waiting for help (help needed or silent)
        uint8_t online = 0, offline = 0, power = 0, battery = 255;
        uint32_t uptimeS = 0;
    };
    // Something for the unit to log or show.
    struct Note {
        bool fault = false;    // an alarm (shown as a fault in the log), not just information
        uint16_t room = 0;
        std::string text;
        int alarm = -1;        // a signed, new EMERGENCY message: its kind (0 = all clear). -1 = none
        uint16_t from = 0;     // ... and who sent it
        bool chime = false;    // a new call for help heard by a unit without a network link
    };
    using PeopleFn = std::function<std::vector<lora::PeopleEntry>()>;

    // seed: any number that differs between units (for the random send delays).
    void begin(IStore* store, LoraRadio* radio, uint32_t seed, PeopleFn people);
    bool on() const { return on_; }
    bool fitted() const { return fitted_; }
    const LoraConfig& config() const { return cfg_; }
    void tick(const State& st, std::vector<Note>* notes);
    // The "lora" console command; a[0] is "lora".
    std::string command(const std::vector<std::string>& a, const State& st);

    uint32_t sent() const { return sent_; }
    uint32_t received() const { return received_; }
    uint32_t refused() const { return badSig_ + replayed_ + malformed_; }
    uint32_t replayed() const { return replayed_; }
    uint32_t badSignature() const { return badSig_; }
    // The last people list heard by radio (complete once every part has arrived).
    const std::vector<lora::PeopleEntry>& heardPeople() const { return heard_; }
    bool heardPeopleComplete() const { return heardParts_ && heardMask_ == (1u << heardParts_) - 1; }

private:
    void load();
    void start();
    uint32_t nextCounter();
    uint32_t rnd(uint32_t n);
    void queue(const lora::Msg& m, bool front = false);
    void queuePeople();
    void schedule(const State& st);
    void transmit(const State& st);
    void receiveOne(const State& st, std::vector<Note>* notes);
    bool fresh(uint16_t from, uint32_t counter);
    std::string status(const State& st) const;

    IStore* store_ = nullptr;
    LoraRadio* radio_ = nullptr;
    PeopleFn people_;
    bool on_ = false, fitted_ = false, sending_ = false;
    LoraConfig cfg_;
    uint32_t rng_ = 1;
    uint32_t frame_ = 0;

    uint32_t counter_ = 0, counterSaved_ = 0;         // counterSaved_: counters up to here are used up after a restart
    std::map<uint16_t, uint32_t> seen_, seenSaved_;   // last counter taken from each sender
    std::deque<lora::Msg> txq_;
    uint32_t txAt_ = 0;                               // no send before this frame
    int32_t budgetMs_ = 10000;                        // airtime left; 10 ms come back every second (1 %)
    uint32_t budgetAt_ = 0;

    // The acting office's sending plan.
    bool wasSender_ = false;
    uint8_t emKind_ = 0;
    int emSends_ = 0, clearLeft_ = 0;
    uint32_t emNext_ = 0;
    std::vector<uint16_t> helpRooms_;
    int helpSends_ = 0, helpOverLeft_ = 0;
    uint32_t helpNext_ = 0, statusNext_ = 0, queryAt_ = 0;

    // A receiver's answers.
    struct AckDone {
        uint8_t type, kind;
        uint32_t at;
    };
    std::map<uint16_t, AckDone> acked_;
    std::map<uint32_t, uint32_t> logged_;             // (sender, type) -> what was last written to the log
    std::vector<lora::PeopleEntry> heard_;
    uint8_t heardParts_ = 0;
    uint32_t heardMask_ = 0;
    uint16_t heardFrom_ = 0;
    bool heardSigned_ = false;

    uint32_t sent_ = 0, received_ = 0, badSig_ = 0, replayed_ = 0, malformed_ = 0, overBudget_ = 0, sendFailed_ = 0;
    std::string lastTx_, lastRx_;
    uint32_t lastTxAt_ = 0, lastRxAt_ = 0, untrustedAt_ = 0;
};

}  // namespace ic
