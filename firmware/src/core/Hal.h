// Hardware interfaces the intercom core needs. The ESP32-S3 board code and the PC
// simulator each implement these; the core never touches hardware directly.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "Packet.h"

namespace ic {

// Datagram transport to the intercom multicast group, over whichever link is active.
class INet {
public:
    virtual ~INet() = default;
    virtual void send(const uint8_t* data, size_t len) = 0;
    // Returns the packet length, or 0 when nothing is waiting.
    virtual size_t recv(uint8_t* buf, size_t cap) = 0;
};

// Physical link state, and which link the core wants traffic on.
class ILinks {
public:
    virtual ~ILinks() = default;
    virtual bool ethernetUp() = 0;  // W5500 PHY link (PoE cable plugged in)
    virtual bool wifiUp() = 0;      // associated to the intercom SSID
    virtual void useLink(LinkType link) = 0;
    // The room number changed: move both links to the unit's new IP address.
    virtual void setAddress(const UnitId& id) { (void)id; }
    // Wi-Fi network for portable units and Ethernet failover (empty ssid = Wi-Fi off).
    virtual void setWifi(const std::string& ssid, const std::string& pass) { (void)ssid; (void)pass; }
};

struct PowerState {
    PowerSource source = PowerSource::PoE;
    bool hasBattery = false;
    uint8_t batteryPct = 0;
};

class IPower {
public:
    virtual ~IPower() = default;
    virtual PowerState read() = 0;
};

// Front panel: buttons (U22, a TCA9555 input each) and the 3-digit display (TM1637). SETUP is the hidden
// button under the front cover: holding it 3 s starts room-number setup.
enum Button : uint8_t { kBtnTalk = 0, kBtnMode = 1, kBtnUp = 2, kBtnDown = 3, kBtnSetup = 4, kButtonCount = 5 };

class IPanel {
public:
    virtual ~IPanel() = default;
    virtual uint8_t buttons() = 0;  // bit n = Button n held
    // 3 characters: '0'-'9', 'a'-'f', 'E', 'L', 'r', 'n', 'o', 'P', 'u', 'U', '-', ' '. dots: bit n lights the decimal point after digit n.
    // A character with bit 7 set is a raw segment pattern instead: bit 0 = a ... bit 6 = g (power-up light test).
    virtual void show(const char text[3], uint8_t dots) = 0;
};

class IStore {
public:
    virtual ~IStore() = default;
    virtual UnitId loadUnit() = 0;
    virtual void saveUnit(const UnitId& id) = 0;
    // Small text values in flash (tuning parameters). Empty string = not set.
    virtual std::string loadText(const char* key) = 0;
    virtual void saveText(const char* key, const std::string& value) = 0;
};

// Optional board checks for the "selftest" command: which chips answer, link state, etc.
// One line per check, starting with "ok " or "FAIL ".
class IDiag {
public:
    virtual ~IDiag() = default;
    virtual std::string boardTest() = 0;
};

class ILog {
public:
    virtual ~ILog() = default;
    virtual void log(const char* msg) = 0;
};

// Lamp outputs (P-FET high-side switches on the board): J13 white, J12 IR illuminator, J11 other
// (the call / alarm light). Level 0-100 %; the board treats anything above 0 as on (its PWM runs at
// 50 Hz for the servos, which would flicker).
enum class Lamp : uint8_t { White = 0, Ir = 1, Other = 2, Count = 3 };

class ILamps {
public:
    virtual ~ILamps() = default;
    virtual void set(Lamp lamp, uint8_t pct) = 0;
    // The RGB status LED (D9, WS2812B). 0-255 per colour.
    virtual void setRgb(uint8_t r, uint8_t g, uint8_t b) { (void)r; (void)g; (void)b; }
};

struct Hal {
    INet* net;
    ILinks* links;
    IPower* power;
    IPanel* panel;
    IStore* store;
    ILog* log;
    IDiag* diag = nullptr;  // optional
    ILamps* lamps = nullptr;  // optional
};

}  // namespace ic
