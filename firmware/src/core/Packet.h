// Wire format. All multi-byte fields little-endian, written byte by byte (no packed structs).
//
// Audio  (16-byte header + payload):
//   'I' 'C' ver type | room u16 | sub u8 | flags u8 | target u16 | seq u16 | level u8 | codec u8 | nsamples u16
//   codec 0 (PCM): nsamples = samples, payload = 16-bit samples; codec 1 (Opus): nsamples = payload bytes
// Command / Reply (16-byte header + text, console over the network):
//   'I' 'C' ver type | room u16 | sub u8 | target sub u8 (0xff any) | target room u16 (0xffff all) | 6 rsvd | text
// Status (24 bytes):
//   'I' 'C' ver type | room u16 | sub u8 | link u8 | power u8 | battery u8 | flags u8 | fault u8 | uptime_s u32
//   | unit_id u32 (from the MAC: tells two units with the same room number apart) | group u16 | rsvd u16
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "Config.h"

namespace ic {

struct UnitId {
    uint16_t room = 0;  // 0..999
    uint8_t sub = 0;    // unused since protocol 2 (was 'a', 'b' ... for several units in one room)
    uint32_t key() const { return (uint32_t(room) << 8) | sub; }
    bool operator==(const UnitId& o) const { return room == o.room && sub == o.sub; }
    bool operator!=(const UnitId& o) const { return !(*this == o); }
};

// "214", "214a"; buf must hold 8 bytes.
void formatUnit(const UnitId& id, char* buf);
// "10.42.2.14"; buf must hold 16 bytes.
void formatIp(const UnitId& id, char* buf);
void ipForUnit(const UnitId& id, uint8_t ip[4]);

enum class PacketType : uint8_t { Audio = 1, Status = 2, Command = 3, Reply = 4 };
enum class Codec : uint8_t { Pcm16 = 0, Opus = 1 };
enum class LinkType : uint8_t { None = 0, Ethernet = 1, WiFi = 2 };
enum class PowerSource : uint8_t { PoE = 0, Battery = 1 };

constexpr uint8_t kVersion = 2;  // 2: status carries the unit id and room group; IP 10.42.<floor>.<room>
constexpr size_t kHeaderBytes = 16;
constexpr size_t kMaxPacketBytes = kHeaderBytes + kFrameSamples * 2;

enum AudioFlags : uint8_t { kAudioAllCall = 0x01, kAudioFromConsole = 0x02 };
enum StatusFlags : uint8_t {
    kStatusTalking = 0x01,
    kStatusEmergency = 0x02,
    kStatusCameraOk = 0x04,
    kStatusMuted = 0x08,     // the office muted this unit's TALK ("mute" command)
    kStatusTalkHeld = 0x10,  // TALK held for over 2 minutes: stuck button or something on it
};
// Status fault byte: what the unit's display is flashing.
enum class Fault : uint8_t { None = 0, NoEthernet = 1, NoNetwork = 2, LowBattery = 3, Duplicate = 4 };

struct AudioHeader {
    UnitId from;
    uint8_t flags = kAudioAllCall;
    uint16_t target = 0;  // 0 = everyone, else a room number
    uint16_t seq = 0;
    uint8_t level = 0;    // loudness 0..100 (dBFS + 100)
    Codec codec = Codec::Pcm16;
    uint16_t nsamples = kFrameSamples;
};

struct StatusPacket {
    UnitId from;
    LinkType link = LinkType::None;
    PowerSource power = PowerSource::PoE;
    uint8_t battery = 0;  // percent, 0 if no battery
    uint8_t flags = 0;
    uint32_t uptimeS = 0;
    uint32_t unitUid = 0;  // unique per board (MAC); 0 = unknown
    uint16_t group = 0;    // room group (0 = its own room number)
    Fault fault = Fault::None;
};

constexpr uint16_t kTargetAllRooms = 0xffff;
constexpr uint8_t kTargetAnySub = 0xff;
constexpr size_t kMaxTextBytes = kMaxPacketBytes - kHeaderBytes;  // 640

struct TextPacket {
    PacketType type = PacketType::Command;
    UnitId from;
    uint16_t targetRoom = kTargetAllRooms;
    uint8_t targetSub = kTargetAnySub;
    std::string text;
    bool addressedTo(const UnitId& u) const {
        return targetRoom == kTargetAllRooms || (targetRoom == u.room && (targetSub == kTargetAnySub || targetSub == u.sub));
    }
};
size_t encodeText(const TextPacket& t, uint8_t* out);  // text truncated to kMaxTextBytes
bool decodeText(const uint8_t* buf, size_t len, TextPacket* t);
// "all", "214" (any unit in room 214), "214a"
bool parseTarget(const std::string& s, uint16_t* room, uint8_t* sub);

size_t encodeAudio(const AudioHeader& h, const int16_t* pcm, uint8_t* out);
// Audio with an already encoded payload (h.codec says which); returns the packet size.
size_t encodeAudioData(const AudioHeader& h, const uint8_t* data, size_t len, uint8_t* out);
size_t encodeStatus(const StatusPacket& s, uint8_t* out);

PacketType peekType(const uint8_t* buf, size_t len, bool* ok);
// pcm receives h.nsamples samples (<= kFrameSamples).
bool decodeAudio(const uint8_t* buf, size_t len, AudioHeader* h, int16_t* pcm);
// Any codec: *data points into buf at the payload, *n is its size in bytes.
bool decodeAudioData(const uint8_t* buf, size_t len, AudioHeader* h, const uint8_t** data, size_t* n);
bool decodeStatus(const uint8_t* buf, size_t len, StatusPacket* s);

// Loudness of a frame for mixer ranking: dBFS + 100, clamped to 0..100.
uint8_t frameLevel(const int16_t* pcm, int n);

}  // namespace ic
