// Web interface password: stored in flash as a salted SHA-256 hash, never as text.
// It can only be set or cleared from the USB console ("webpass"); until it is set the web
// interface stays off.
#pragma once
#include <cstdint>
#include <string>

#include "Hal.h"
#include "Packet.h"

namespace ic {

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
std::string sha256Hex(const std::string& data);
std::string hmacSha256Hex(const std::string& key, const std::string& msg);
bool base64Decode(const std::string& in, std::string* out);

namespace auth {
constexpr int kMinPasswordLen = 8;
bool hasPassword(IStore& store);
std::string stored(IStore& store);  // the stored salt$hash (changes when the password does)
// seed: any changing value (frame counter, unit id) mixed into the salt.
void setPassword(IStore& store, const std::string& password, uint32_t seed);
void clearPassword(IStore& store);
bool checkPassword(IStore& store, const std::string& password);
}  // namespace auth

// Network commands are signed with a shared key ("netkey" on USB, the same key in the office
// console). Wire form of the command text: "#<nonce> <mac> <command>", where mac = the first
// 16 bytes (hex) of HMAC-SHA256(key, "nonce|targetRoom|targetSub|fromRoom|fromSub|command").
// The nonce (sender's clock in ms) must be higher than the last one the unit accepted, so a
// captured command can't be replayed. Not encrypted: anyone on the VLAN can read commands.
namespace netauth {
constexpr int kMinKeyLen = 16;
enum class Check { Ok, NoKey, Unsigned, BadMac, Replay };
std::string sign(const std::string& key, uint64_t nonce, const TextPacket& t);
Check verify(const std::string& key, uint64_t lastNonce, const TextPacket& t, std::string* cmd, uint64_t* nonce);
const char* describe(Check c);
std::string loadKey(IStore& store);
void saveKey(IStore& store, const std::string& key);
uint64_t loadNonce(IStore& store);
void saveNonce(IStore& store, uint64_t n);
}  // namespace netauth

}  // namespace ic
