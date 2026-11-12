#include "Packet.h"

#include <cmath>
#include <cstdio>

namespace ic {

namespace {
void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); }
uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

void putPrefix(uint8_t* out, PacketType t, const UnitId& id) {
    out[0] = 'I'; out[1] = 'C'; out[2] = kVersion; out[3] = uint8_t(t);
    put16(out + 4, id.room);
    out[6] = id.sub;
}
}  // namespace

void formatUnit(const UnitId& id, char* buf) {
    if (id.sub) snprintf(buf, 8, "%u%c", unsigned(id.room), char('a' + id.sub - 1));
    else snprintf(buf, 8, "%u", unsigned(id.room));
}

void ipForUnit(const UnitId& id, uint8_t ip[4]) {
    ip[0] = kIpPrefix0;
    ip[1] = kIpPrefix1;
    ip[2] = uint8_t((id.room / 100) * 10 + (id.sub % 10));
    ip[3] = uint8_t(id.room % 100);
    if (ip[3] == 0) ip[3] = 100;  // .0 is not a usable host address: room 100 -> x.x.10.100
}

void formatIp(const UnitId& id, char* buf) {
    uint8_t ip[4];
    ipForUnit(id, ip);
    snprintf(buf, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

size_t encodeAudio(const AudioHeader& h, const int16_t* pcm, uint8_t* out) {
    putPrefix(out, PacketType::Audio, h.from);
    out[7] = h.flags;
    put16(out + 8, h.target);
    put16(out + 10, h.seq);
    out[12] = h.level;
    out[13] = uint8_t(h.codec);
    put16(out + 14, h.nsamples);
    for (int i = 0; i < h.nsamples; i++) put16(out + kHeaderBytes + 2 * i, uint16_t(pcm[i]));
    return kHeaderBytes + 2u * h.nsamples;
}

size_t encodeStatus(const StatusPacket& s, uint8_t* out) {
    putPrefix(out, PacketType::Status, s.from);
    out[7] = uint8_t(s.link);
    out[8] = uint8_t(s.power);
    out[9] = s.battery;
    out[10] = s.flags;
    out[11] = 0;
    put32(out + 12, s.uptimeS);
    return 16;
}

PacketType peekType(const uint8_t* buf, size_t len, bool* ok) {
    *ok = len >= kHeaderBytes && buf[0] == 'I' && buf[1] == 'C' && buf[2] == kVersion;
    return *ok ? PacketType(buf[3]) : PacketType::Audio;
}

bool decodeAudio(const uint8_t* buf, size_t len, AudioHeader* h, int16_t* pcm) {
    bool ok;
    if (peekType(buf, len, &ok) != PacketType::Audio || !ok) return false;
    h->from.room = get16(buf + 4);
    h->from.sub = buf[6];
    h->flags = buf[7];
    h->target = get16(buf + 8);
    h->seq = get16(buf + 10);
    h->level = buf[12];
    h->codec = Codec(buf[13]);
    h->nsamples = get16(buf + 14);
    if (h->codec != Codec::Pcm16 || h->nsamples > kFrameSamples) return false;
    if (len < kHeaderBytes + 2u * h->nsamples) return false;
    for (int i = 0; i < h->nsamples; i++) pcm[i] = int16_t(get16(buf + kHeaderBytes + 2 * i));
    return true;
}

bool decodeStatus(const uint8_t* buf, size_t len, StatusPacket* s) {
    bool ok;
    if (peekType(buf, len, &ok) != PacketType::Status || !ok) return false;
    s->from.room = get16(buf + 4);
    s->from.sub = buf[6];
    s->link = LinkType(buf[7]);
    s->power = PowerSource(buf[8]);
    s->battery = buf[9];
    s->flags = buf[10];
    s->uptimeS = get32(buf + 12);
    return true;
}

uint8_t frameLevel(const int16_t* pcm, int n) {
    double e = 0;
    for (int i = 0; i < n; i++) e += double(pcm[i]) * pcm[i];
    double rms = std::sqrt(e / (n ? n : 1)) / 32768.0;
    double db = rms > 1e-5 ? 20.0 * std::log10(rms) : -100.0;
    int lvl = int(db + 100.0 + 0.5);
    return uint8_t(lvl < 0 ? 0 : lvl > 100 ? 100 : lvl);
}

}  // namespace ic
