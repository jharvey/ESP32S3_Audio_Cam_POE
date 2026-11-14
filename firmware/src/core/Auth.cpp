#include "Auth.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ic {

namespace {
const char* kPassKey = "web_pass";

const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void block(uint32_t h[8], const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
        uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

// Constant-time compare, so response time doesn't leak how much of a hash matched.
bool sameText(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); i++) diff |= uint8_t(a[i] ^ b[i]);
    return diff == 0;
}

// Many rounds slow down guessing if someone copies the stored hash out of flash.
constexpr int kRounds = 2000;
std::string hashWithSalt(const std::string& salt, const std::string& password) {
    std::string h = sha256Hex(salt + ":" + password);
    for (int i = 1; i < kRounds; i++) h = sha256Hex(h + password);
    return h;
}
}  // namespace

void sha256(const uint8_t* p, size_t n, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t i = 0;
    for (; i + 64 <= n; i += 64) block(h, p + i);
    uint8_t tail[128] = {};
    size_t rest = n - i;
    std::memcpy(tail, p + i, rest);
    tail[rest] = 0x80;
    size_t len = rest + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = uint64_t(n) * 8;
    for (int j = 0; j < 8; j++) tail[len - 1 - j] = uint8_t(bits >> (8 * j));
    block(h, tail);
    if (len == 128) block(h, tail + 64);
    for (int j = 0; j < 8; j++) {
        out[4 * j] = uint8_t(h[j] >> 24);
        out[4 * j + 1] = uint8_t(h[j] >> 16);
        out[4 * j + 2] = uint8_t(h[j] >> 8);
        out[4 * j + 3] = uint8_t(h[j]);
    }
}

static std::string hex(const uint8_t* d, size_t n) {
    static const char* k = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; i++) {
        out += k[d[i] >> 4];
        out += k[d[i] & 15];
    }
    return out;
}

std::string sha256Hex(const std::string& data) {
    uint8_t d[32];
    sha256(reinterpret_cast<const uint8_t*>(data.data()), data.size(), d);
    return hex(d, 32);
}

std::string hmacSha256Hex(const std::string& key, const std::string& msg) {
    uint8_t k[64] = {};
    if (key.size() > 64) sha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(), k);
    else std::memcpy(k, key.data(), key.size());
    std::string inner(64, char(0)), outer(64, char(0));
    for (int i = 0; i < 64; i++) {
        inner[i] = char(k[i] ^ 0x36);
        outer[i] = char(k[i] ^ 0x5c);
    }
    uint8_t d[32];
    inner += msg;
    sha256(reinterpret_cast<const uint8_t*>(inner.data()), inner.size(), d);
    outer.append(reinterpret_cast<const char*>(d), 32);
    sha256(reinterpret_cast<const uint8_t*>(outer.data()), outer.size(), d);
    return hex(d, 32);
}

bool base64Decode(const std::string& in, std::string* out) {
    out->clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : in) {
        int v;
        if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
        else if (ch == '+') v = 62;
        else if (ch == '/') v = 63;
        else if (ch == '=') break;
        else return false;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back(char((acc >> bits) & 0xff));
        }
    }
    return true;
}

namespace auth {

bool hasPassword(IStore& store) { return !store.loadText(kPassKey).empty(); }

std::string stored(IStore& store) { return store.loadText(kPassKey); }

void setPassword(IStore& store, const std::string& password, uint32_t seed) {
    char seedText[64];
    snprintf(seedText, sizeof(seedText), "%u:%p:%zu", unsigned(seed), static_cast<void*>(&store), password.size());
    std::string salt = sha256Hex(seedText + store.loadText(kPassKey)).substr(0, 16);
    store.saveText(kPassKey, salt + "$" + hashWithSalt(salt, password));
}

void clearPassword(IStore& store) { store.saveText(kPassKey, ""); }

bool checkPassword(IStore& store, const std::string& password) {
    std::string s = store.loadText(kPassKey);
    size_t d = s.find('$');
    if (s.empty() || d == std::string::npos) return false;
    return sameText(hashWithSalt(s.substr(0, d), password), s.substr(d + 1));
}

}  // namespace auth

namespace netauth {
namespace {
const char* kKeyKey = "net_key";
const char* kNonceKey = "net_nonce";
constexpr size_t kMacHex = 32;  // first 16 bytes of the HMAC

std::string macFor(const std::string& key, uint64_t nonce, const TextPacket& t, const std::string& cmd) {
    char head[96];
    snprintf(head, sizeof(head), "%llu|%u|%u|%u|%u|", static_cast<unsigned long long>(nonce), unsigned(t.targetRoom),
             unsigned(t.targetSub), unsigned(t.from.room), unsigned(t.from.sub));
    return hmacSha256Hex(key, head + cmd).substr(0, kMacHex);
}
}  // namespace

std::string sign(const std::string& key, uint64_t nonce, const TextPacket& t) {
    char n[24];
    snprintf(n, sizeof(n), "%llu", static_cast<unsigned long long>(nonce));
    return std::string("#") + n + " " + macFor(key, nonce, t, t.text) + " " + t.text;
}

Check verify(const std::string& key, uint64_t lastNonce, const TextPacket& t, std::string* cmd, uint64_t* nonce) {
    if (key.empty()) return Check::NoKey;
    const std::string& s = t.text;
    if (s.empty() || s[0] != '#') return Check::Unsigned;
    size_t sp1 = s.find(' ');
    size_t sp2 = sp1 == std::string::npos ? sp1 : s.find(' ', sp1 + 1);
    if (sp2 == std::string::npos || sp2 - sp1 - 1 != kMacHex) return Check::Unsigned;
    char* end = nullptr;
    std::string nText = s.substr(1, sp1 - 1);
    unsigned long long n = std::strtoull(nText.c_str(), &end, 10);
    if (nText.empty() || *end) return Check::Unsigned;
    std::string body = s.substr(sp2 + 1);
    if (!sameText(macFor(key, n, t, body), s.substr(sp1 + 1, kMacHex))) return Check::BadMac;
    if (n <= lastNonce) return Check::Replay;
    *cmd = body;
    *nonce = n;
    return Check::Ok;
}

const char* describe(Check c) {
    switch (c) {
        case Check::Ok: return "ok";
        case Check::NoKey: return "network commands are off on this unit (set a key on USB: netkey <key>)";
        case Check::Unsigned: return "refused: command not signed with the network key";
        case Check::BadMac: return "refused: wrong network key";
        case Check::Replay: return "refused: old or repeated command (check this PC's clock)";
    }
    return "refused";
}

std::string loadKey(IStore& store) { return store.loadText(kKeyKey); }
void saveKey(IStore& store, const std::string& key) { store.saveText(kKeyKey, key); }
uint64_t loadNonce(IStore& store) { return std::strtoull(store.loadText(kNonceKey).c_str(), nullptr, 10); }
void saveNonce(IStore& store, uint64_t n) {
    char b[24];
    snprintf(b, sizeof(b), "%llu", static_cast<unsigned long long>(n));
    store.saveText(kNonceKey, b);
}

}  // namespace netauth
}  // namespace ic
