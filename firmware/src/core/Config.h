// Shared constants for the intercom core. Plain C++17: no Arduino or ESP-IDF headers,
// so the same code builds for the ESP32-S3 and for the PC simulator.
#pragma once
#include <cstdint>

namespace ic {

constexpr int kSampleRate = 16000;                          // Hz, mono
constexpr int kFrameMs = 20;
constexpr int kFrameSamples = kSampleRate * kFrameMs / 1000;  // 320

constexpr uint16_t kAudioPort = 5004;
constexpr const char* kMulticastGroup = "239.255.42.1";     // intercom VLAN, TTL 1

constexpr int kMaxMixTalkers = 4;        // loudest N talkers are mixed
constexpr int kStatusEveryFrames = 50;   // status packet once a second
constexpr int kPeerTimeoutFrames = 175;  // peer dropped after 3.5 s without status

// IP plan: 10.42.<floor*10 + sub>.<room % 100>
//   room 214  -> 10.42.20.14     room 214a -> 10.42.21.14     room 7 -> 10.42.0.7
constexpr uint8_t kIpPrefix0 = 10;
constexpr uint8_t kIpPrefix1 = 42;

}  // namespace ic
