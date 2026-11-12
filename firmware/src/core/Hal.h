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

// Front panel: 4 buttons (via the TM1637 key scan) and the 3-digit display.
enum Button : uint8_t { kBtnTalk = 0, kBtnMode = 1, kBtnUp = 2, kBtnDown = 3, kButtonCount = 4 };

class IPanel {
public:
    virtual ~IPanel() = default;
    virtual uint8_t buttons() = 0;  // bit n = Button n held
    // 3 characters: '0'-'9', 'a'-'f', 'L', 'r', 'n', 'o', 'P', 'u', '-', ' '. dots: bit n lights the decimal point after digit n.
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

class ILog {
public:
    virtual ~ILog() = default;
    virtual void log(const char* msg) = 0;
};

struct Hal {
    INet* net;
    ILinks* links;
    IPower* power;
    IPanel* panel;
    IStore* store;
    ILog* log;
};

}  // namespace ic
