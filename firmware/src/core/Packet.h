// Wire format. All multi-byte fields little-endian, written byte by byte (no packed structs).
//
// Audio  (16-byte header + payload):
//   'I' 'C' ver type | room u16 | sub u8 | flags u8 | target u16 | seq u16 | level u8 | codec u8 | nsamples u16
// Status (16 bytes):
//   'I' 'C' ver type | room u16 | sub u8 | link u8 | power u8 | battery u8 | flags u8 | rsvd u8 | uptime_s u32
#pragma once
#include <cstddef>
#include <cstdint>

#include "Config.h"

namespace ic {

struct UnitId {
    uint16_t room = 0;  // 0..999
    uint8_t sub = 0;    // 0 = none, 1 = 'a', 2 = 'b' ...
    uint32_t key() const { return (uint32_t(room) << 8) | sub; }
    bool operator==(const UnitId& o) const { return room == o.room && sub == o.sub; }
    bool operator!=(const UnitId& o) const { return !(*this == o); }
};

// "214", "214a"; buf must hold 8 bytes.
void formatUnit(const UnitId& id, char* buf);
// "10.42.21.14"; buf must hold 16 bytes.
void formatIp(const UnitId& id, char* buf);
void ipForUnit(const UnitId& id, uint8_t ip[4]);

enum class PacketType : uint8_t { Audio = 1, Status = 2 };
enum class Codec : uint8_t { Pcm16 = 0, Opus = 1 };
enum class LinkType : uint8_t { None = 0, Ethernet = 1, WiFi = 2 };
enum class PowerSource : uint8_t { PoE = 0, Battery = 1 };

constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderBytes = 16;
constexpr size_t kMaxPacketBytes = kHeaderBytes + kFrameSamples * 2;

enum AudioFlags : uint8_t { kAudioAllCall = 0x01, kAudioFromConsole = 0x02 };
enum StatusFlags : uint8_t { kStatusTalking = 0x01, kStatusEmergency = 0x02, kStatusCameraOk = 0x04 };

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
};

size_t encodeAudio(const AudioHeader& h, const int16_t* pcm, uint8_t* out);
size_t encodeStatus(const StatusPacket& s, uint8_t* out);

PacketType peekType(const uint8_t* buf, size_t len, bool* ok);
// pcm receives h.nsamples samples (<= kFrameSamples).
bool decodeAudio(const uint8_t* buf, size_t len, AudioHeader* h, int16_t* pcm);
bool decodeStatus(const uint8_t* buf, size_t len, StatusPacket* s);

// Loudness of a frame for mixer ranking: dBFS + 100, clamped to 0..100.
uint8_t frameLevel(const int16_t* pcm, int n);

}  // namespace ic
