#include "Intercom.h"

#include "Auth.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace ic {

namespace {
constexpr int kEthStableFrames = 10;     // Ethernet must be up 200 ms before traffic moves to it
constexpr int kTalkerIdleFrames = 50;    // forget a talker 1 s after its last packet
constexpr size_t kMaxTalkers = 16;
constexpr uint32_t kComboFrames = 150;   // MODE + UP held 3 s = auto-tune
constexpr uint32_t kUpHoldFrames = 250;  // or UP alone held 5 s (one-key panels)
constexpr uint32_t kResultFrames = 250;  // show the auto-tune result for 5 s
// Room calls (R2.0).
constexpr uint32_t kCallFrames = 50;        // a call is "on" while its packets keep coming (1 s gap ends it)
constexpr uint32_t kCloseFrames = 100;      // the office's word after the room's answer ends the conversation, 2 s after it stops
constexpr uint32_t kAnswerFrames = 500;     // TALK answers the caller during a call and 10 s after
constexpr uint32_t kAllCallHoldFrames = 25; // an office all-call keeps room calls out until 0.5 s after it
constexpr uint32_t kCallHoldFrames = 100;   // DOWN held 2 s = call the office (again = cancel)
constexpr uint32_t kCallingTimeoutFrames = 15000;  // a call for help to the office gives up after 5 min
constexpr uint32_t kOfficeRingFrames = 500;        // a plain call to the office rings both ends for 10 s, then gives up
constexpr uint32_t kMissedFrames = 15000;   // an unanswered call keeps a slow blink for 5 min (any button stops it)
constexpr uint32_t kCallRgb = 0x0030C0;     // RGB LED colour for calls: blue, like "calling" on the school page
constexpr uint32_t kPendingSoundFrames = 3000;     // a timed chime waits up to 60 s for a call to end
constexpr const char* kSchedKey = "sched";
constexpr uint32_t kDupHoldFrames = 150; // "dUP" stays 3 s after the other unit was last heard
constexpr uint32_t kGroupTalkHoldFrames = 15;  // speaker stays quiet 300 ms after a room-group member's last frame
constexpr uint32_t kFaultCycle = 175;    // faults: room number 3 s, then the code for 0.5 s
constexpr uint32_t kFaultShow = 25;
constexpr uint8_t kLowBatteryPct = 15;
// R3.1
constexpr size_t kMaxEvents = 200;
constexpr uint32_t kFwConfirmFrames = 60 * 50;       // new firmware is confirmed after 1 min running with a link
constexpr uint32_t kFwGiveUpFrames = 5 * 60 * 50;    // ...and dropped after 5 min without one
constexpr uint32_t kEmergencyMaxFrames = 4 * 3600 * 50;  // an emergency nobody cleared ends by itself after 4 h
constexpr uint32_t kEmergencySoundFrames = 60 * 50;      // the signal sounds for 1 min (lockdown, shelter) ...
constexpr uint32_t kEvacuateSoundFrames = 5 * 60 * 50;   // ... or 5 min (evacuate); display and lamp stay on
constexpr uint32_t kEmergencyRgb = 0xC00000;
const char* const kEmergencyNames[] = {"clear", "lockdown", "evacuate", "shelter"};
// R3.2
const char* const kSchoolKeys[] = {"features", "office", "zones", "quiet", "sched", "selfcheck", "conwatch", "ntp"};  // the same on every unit
const char kSyncSep = '\x1f';
const char* const kFeatureNames[] = {"reason", "queue", "zones", "dnd", "quiet", "silent", "failover", "lockdown"};
constexpr uint32_t kUrgentHoldFrames = 75;            // OFFICE held 1.5 s = "help needed"
constexpr uint32_t kSilentBothFrames = 50;            // TALK and OFFICE held together 1 s = silent help call
constexpr uint32_t kPressDelayFrames = 8;             // TALK and OFFICE wait 0.16 s before they act, so pressing both does neither
constexpr uint32_t kHelpAgainFrames = 1500;           // an office unit rings again every 30 s for an unanswered call for help
constexpr uint32_t kSilentTxFrames = 2 * 60 * 50;     // the office hears the room for 2 minutes
constexpr uint32_t kSilentFrames = 30 * 60 * 50;      // the silent call stays up for 30 minutes unless cleared
const char* const kFeatureUsage = "usage: feature [reason|queue|zones|dnd|quiet|silent|failover|lockdown|all on|off]";
const char* const kZoneUsage = "usage: zone | zone N NAME ROOMS (N 1-8, e.g. zone 1 east 101-112,201) | zone N off";
const char* const kQuietUsage = "usage: quiet | quiet HH:MM HH:MM DB (e.g. quiet 18:00 07:00 -12; DB -30 to 0) | quiet off";

std::string targetText(uint8_t flags, uint16_t target) {
    if (flags & kAudioReply) return target ? "room " + std::to_string(target) + " (answer)" : "the office console (answer)";
    if (target == 0) return "everyone";
    if (flags & kAudioFloor) return "floor " + std::to_string(target);
    if (flags & kAudioZone) return "zone " + std::to_string(target);
    if (flags & kAudioGroupCall) return "group " + std::to_string(target);
    return "room " + std::to_string(target);
}
std::string talkerText(uint8_t flags, uint16_t room) {
    return (flags & kAudioFromConsole) ? "office console (via room " + std::to_string(room) + ")" : "room " + std::to_string(room);
}
const char* kGroupKey = "group";
const char* kWifiSsidKey = "wifi_ssid";
const char* kWifiPassKey = "wifi_pass";
const char* kTuneKey = "tune";
const char* kTunePrevKey = "tune_prev";

const char* kHelp =
    "commands:\n"
    "  help                 this list\n"
    "  get [key]            show tuning parameters\n"
    "  set key value ...    change parameters now (not saved)\n"
    "  save                 save parameters to flash\n"
    "  load                 reload saved parameters\n"
    "  defaults             factory parameters (not saved)\n"
    "  undo                 restore the parameters from before the last auto-tune or save\n"
    "  cal                  auto-tune the echo canceller (~5 s, room quiet, unit plays a noise burst)\n"
    "  cal result           last auto-tune measurements\n"
    "  stats                echo canceller and link state\n"
    "  unit [214]           show or set the room number (sets the IP address 10.42.<floor>.<room>)\n"
    "  group [299|off]      room group: units in one room (the gym: 299, 298 ...) do not play each other,\n"
    "                         and go quiet while one of them talks\n"
    "  couple [run S auto|manual 101,102,... [closed|open]|clear]  room coupling test, doors closed (default)\n"
    "                       or open: every unit in the list plays a sweep in turn (S s each, sent to all\n"
    "                       units at once by the office); couple = both results\n"
    "  tone [seconds]       play a 1 kHz test tone on this unit's speaker (default 2 s, 0 stops)\n"
    "  reboot [download]    restart the unit; download = into USB flashing mode (no BOOT button needed)\n"
    "  netupdate [on|off]   allow firmware updates from the web page (needs the web password; USB only)\n"
    "  mute [minutes|off]   stop this unit's TALK for a while (office: stuck button, misuse); it still hears\n"
    "  time [utc_seconds]   show the clock / set it (the office console does this when there's no NTP)\n"
    "  tz MIN [none|us|eu]  time zone: standard-time offset in minutes (US Eastern -300) and daylight-saving rule\n"
    "  sched                timed events: list | add HH:MM mon-fri SOUND [room N|floor N|group N] | del N | clear\n"
    "  dayoff [add|del DATE] days without timed events (2026-12-24); \"skip today\" / \"skip off\"\n"
    "  play SOUND           play a sound here now (chime bell lunch tone)\n"
    "  boottest [on|off|run] power-up light test: display segments, lamps, RGB LED (about 4 s at start)\n"
    "  lamp [white|ir|other 0-100|auto]  lamp outputs; other = call light (blinks for a call, on for an all-call)\n"
    "  ntp [server|default] the school's time server (from the next restart)\n"
    "  send all|ROOM CMD    send a console command to other units, signed with this unit's key (web / USB)\n"
    "  testpass <pw>|off    password for installer tests (auto-tune, coupling) from the web console (USB only)\n"
    "  tests <pw|-> CMD     run CMD (cal, couple run, send ... cal) with the tests password (the school page)\n"
    "  callofc [off]        call the office (as OFFICE, or DOWN held 2 s; the console shows it until answered)\n"
    "  ring [ROOM]          the office rings this room: chime every 2 s and blinking lights for 10 s; TALK\n"
    "                       answers ROOM (the office unit; none = the office console): send 214 ring 105\n"
    "  talk [seconds|off] [room N|floor N|group N]\n"
    "                       transmit as if TALK were held, to everyone or a target (USB only; default 5 s)\n"
    "  selftest [result]    check the board and the speaker->mic path (plays a short tone)\n"
    "  version              firmware version\n"
    "  feature [NAME|all on|off]  optional features: reason queue zones dnd quiet silent failover lockdown (all off)\n"
    "  office [ROOM [STAND-IN]]   the office unit, and the unit that takes over while it is offline (failover)\n"
    "  zone [N NAME ROOMS|N off]  zones for paging: zone 1 east 101-112,201 (zones)\n"
    "  dnd [minutes|off]    do not disturb: only the office and emergencies get through, no bells (dnd)\n"
    "  quiet [HH:MM HH:MM DB|off]  quieter between two times of day (quiet); emergency signals stay loud\n"
    "  log [calls|faults] [N]  the event log: faults and calls this unit saw, school-wide (last 30)\n"
    "  selfcheck [HH:MM|off|now]  nightly speaker->microphone check; a failure shows on the school page\n"
    "  consolewatch [MIN|off]  the office unit chirps when no office page has read it for MIN minutes (1-60)\n"
    "  emergency [lockdown|evacuate|shelter|clear] [all]  emergency signal here; all = on every unit (USB / web)\n"
    "  backup               this unit's settings as key=value lines (no passwords or keys)\n"
    "  restore key=value    put one line of a backup back\n"
    "  netkey <key>         set the network command key (USB only; \"netkey off\" refuses all network commands)\n"
    "  webpass <password>   set the web interface password (USB only; \"webpass off\" turns it off)\n"
    "  wifi <ssid> <pass>   Wi-Fi for failover and portable units (USB only; \"wifi off\"; same IP as Ethernet)\n"
    "parameters:\n";
}  // namespace

Intercom::Intercom(const Hal& hal, EchoCanceller& aec, IntercomOptions opt) : hal_(hal), aec_(aec), opt_(opt) {
    cal_.setRunner(opt_.runBackground);
}

void Intercom::logf(const char* fmt, ...) {
    if (!hal_.log) return;
    char buf[480];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    hal_.log->log(buf);
}

void Intercom::event(EventKind k, uint16_t room, const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    events_.push_back(Event{++eventN_, utcNow(), frame_, k, room, buf});
    if (events_.size() > kMaxEvents) events_.erase(events_.begin());
    if (hal_.log) hal_.log->log((std::string(k == EventKind::Fault ? "FAULT: " : k == EventKind::Call ? "call: " : "event: ") + buf).c_str());
}

// Feature switches, the office unit and its stand-in, quiet hours and zones, from flash.
void Intercom::loadFeatures() {
    // Nothing saved yet: help needed, the silent help call and the red lockdown button work out of the box.
    {
        unsigned v[3] = {0, 0, 0};
        if (std::sscanf(hal_.store->loadText("shutterus").c_str(), "%u %u %u", &v[0], &v[1], &v[2]) == 3)
            for (int i = 0; i < 3; i++)
                if (v[i] >= 400 && v[i] <= 2600) shutterUs_[i] = uint16_t(v[i]);
    }
    const std::string feat = hal_.store->loadText("features");
    features_ = feat.empty() ? uint8_t(kFeatReason | kFeatSilent | kFeatLockdown) : uint8_t(std::strtoul(feat.c_str(), nullptr, 10) & 0xff);
    offices_.clear();
    {
        std::stringstream os(hal_.store->loadText("office"));
        unsigned long r;
        while (os >> r && offices_.size() < 4)
            if (r >= 1 && r <= 999 && std::find(offices_.begin(), offices_.end(), uint16_t(r)) == offices_.end()) offices_.push_back(uint16_t(r));
    }
    office_ = offices_.empty() ? uint16_t(0) : offices_[0];
    office2_ = offices_.size() > 1 ? offices_[1] : uint16_t(0);
    quietFrom_ = quietTo_ = -1;
    quietSetDb_ = 0;
    unsigned h1 = 0, m1 = 0, h2 = 0, m2 = 0;
    float db = 0;
    if (std::sscanf(hal_.store->loadText("quiet").c_str(), "%u:%u %u:%u %f", &h1, &m1, &h2, &m2, &db) == 5 && h1 < 24 && m1 < 60 &&
        h2 < 24 && m2 < 60 && db <= 0 && db >= -30) {
        quietFrom_ = int(h1 * 60 + m1);
        quietTo_ = int(h2 * 60 + m2);
        quietSetDb_ = db;
    }
    zones_.clear();  // "1=east:101-112,201;2=gym:296-299"
    std::stringstream zs(hal_.store->loadText("zones"));
    std::string item;
    while (std::getline(zs, item, ';')) {
        size_t eq = item.find('='), co = item.find(':');
        if (eq == std::string::npos || co == std::string::npos || co < eq) continue;
        unsigned long n = std::strtoul(item.c_str(), nullptr, 10);
        if (n < 1 || n > 8 || zones_.size() >= 8) continue;
        zones_.push_back(Zone{uint8_t(n), item.substr(eq + 1, co - eq - 1), item.substr(co + 1)});
    }
}

bool Intercom::inZone(uint16_t zone, uint16_t room) const {
    for (const Zone& z : zones_) {
        if (z.n != zone) continue;
        std::stringstream ss(z.rooms);
        std::string part;
        while (std::getline(ss, part, ',')) {
            unsigned lo = 0, hi = 0;
            int got = std::sscanf(part.c_str(), "%u-%u", &lo, &hi);
            if (got == 1) hi = lo;
            if (got >= 1 && room >= lo && room <= hi) return true;
        }
    }
    return false;
}

// The office unit, or its stand-in while the office unit is not heard (feature failover).
uint16_t Intercom::actingOffice() const {
    if (!office_) return 0;
    auto up = [&](uint16_t r) {
        if (r == unit_.room) return true;
        for (const auto& p : peers_)
            if (p.status.from.room == r && (!p.status.syncHash || p.status.syncRev >= syncRev_)) return true;  // online, and not behind on the settings
        return false;
    };
    if (!feature(kFeatFailover)) return office_;
    for (uint16_t r : offices_)
        if (up(r)) return r;
    return office_;
}

bool Intercom::isOffice(uint16_t room) const { return room && std::find(offices_.begin(), offices_.end(), room) != offices_.end(); }

uint16_t Intercom::schoolHash() const {
    uint32_t h = 2166136261u;
    auto mix = [&](const std::string& t) {
        for (unsigned char ch : t) h = (h ^ ch) * 16777619u;
    };
    for (const char* k : kSchoolKeys) mix(std::string(k) + "=" + (std::string(k) == "sched" ? sched_.toString() : hal_.store->loadText(k)) + "\n");
    const uint16_t x = uint16_t(h ^ (h >> 16));
    return x ? x : uint16_t(1);
}

void Intercom::setSyncRev(unsigned long rev) {
    syncRev_ = uint16_t(std::min(rev, 65535ul));
    hal_.store->saveText("syncrev", std::to_string(syncRev_));
}

// Once a second. Every 5 s: look whether the school-wide settings changed here; on an office unit that counts as a new
// revision. The acting office unit then brings one unit a second up to date.
void Intercom::syncTick() {
    if (!syncHash_ || (frame_ / kStatusEveryFrames) % 5 == 0) {
        const uint16_t h = schoolHash();
        if (isOffice(unit_.room) && (!syncRev_ || (syncHash_ && h != syncHash_))) setSyncRev(syncRev_ + 1ul);
        syncHash_ = h;
    }
    if (frame_ < 500 || !syncRev_ || unit_.room != actingOffice() || peers_.empty()) return;
    for (size_t n = 0; n < peers_.size(); n++) {
        const size_t i = (syncAt_ + n) % peers_.size();
        Peer& p = peers_[i];
        const StatusPacket& s = p.status;
        if (!s.syncHash || s.syncRev > syncRev_ || (s.syncRev == syncRev_ && s.syncHash == syncHash_)) continue;
        if (p.syncSent && frame_ - p.syncSent < 1500) continue;  // sent less than 30 s ago: wait for its next status
        p.syncSent = frame_ ? frame_ : 1;
        syncAt_ = i + 1;
        const std::string to = std::to_string(s.from.room);
        if (s.syncHash == syncHash_) {  // the same settings, an older count
            sendSigned(to, "syncrev " + std::to_string(syncRev_) + " " + std::to_string(syncHash_));
            return;
        }
        std::string rest;
        for (const char* k : kSchoolKeys)
            if (std::string(k) != "sched") rest += (rest.empty() ? "" : std::string(1, kSyncSep)) + k + "=" + hal_.store->loadText(k);
        sendSigned(to, "sync 0 sched=" + sched_.toString());
        const std::string r = sendSigned(to, "sync " + std::to_string(syncRev_) + " " + rest);
        event(EventKind::Info, s.from.room, "room %u: school settings sent to it (new, replaced or out of date)%s", unsigned(s.from.room),
              r.find("sent") == std::string::npos ? " - NOT SENT" : "");
        return;
    }
}

// "sync REV key=value<1f>key=value...": school-wide settings from the office unit (REV 0 = more follows, keep the count).
// "syncrev REV HASH": the settings here are already the same, only the count is behind.
std::string Intercom::syncCommand(const std::vector<std::string>& a, const std::string& line) {
    if (a.size() < 3) return "usage: sync REV key=value...";
    const unsigned long rev = std::strtoul(a[1].c_str(), nullptr, 10);
    if (a[0] == "syncrev") {
        if (std::strtoul(a[2].c_str(), nullptr, 10) != schoolHash()) return "settings differ";
        syncHash_ = schoolHash();
        if (rev >= syncRev_) setSyncRev(rev);
        return "ok";
    }
    if (rev && rev < syncRev_) return "newer settings here";
    size_t p = line.find(a[1], line.find("sync") + 4);
    if (p == std::string::npos) return "usage: sync REV key=value...";
    p += a[1].size();
    while (p < line.size() && line[p] == ' ') p++;
    std::stringstream ss(line.substr(p));
    std::string item;
    while (std::getline(ss, item, kSyncSep)) {
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = item.substr(0, eq), v = item.substr(eq + 1);
        bool known = false;
        for (const char* sk : kSchoolKeys) known = known || k == sk;
        if (!known || v.size() > 600) continue;
        if (k == "sched") {
            Schedule n;
            if (n.fromString(v)) {
                sched_ = n;
                saveSchedule();
            }
        } else {
            hal_.store->saveText(k.c_str(), v);
        }
    }
    loadFeatures();
    loadSelfCheck();
    syncHash_ = schoolHash();
    if (rev) {
        setSyncRev(rev);
        logf("school settings received from the office (revision %lu)", rev);
    }
    return "ok";
}

std::vector<Intercom::Waiting> Intercom::callQueue() const {
    std::vector<Waiting> q = waiting_;
    std::stable_sort(q.begin(), q.end(), [](const Waiting& a, const Waiting& b) {
        if (a.silent != b.silent) return a.silent;
        if (a.urgent != b.urgent) return a.urgent;
        return a.since < b.since;
    });
    return q;
}

// Silent help call: nothing sounds or shows in the room; its microphone goes to the office for 2 minutes.
void Intercom::setSilent(bool on, const char* why) {
    if (on == silentHelp()) return;
    if (on) {
        callingOffice_ = false;  // without the "call ended" line and display change of a normal call
        urgent_ = false;
        silentUntil_ = frame_ + kSilentFrames;
        silentTxUntil_ = frame_ + kSilentTxFrames;
        event(EventKind::Fault, unit_.room, "room %u: SILENT HELP CALL (%s)", unsigned(unit_.room), why);
    } else {
        silentUntil_ = silentTxUntil_ = 0;
        event(EventKind::Info, unit_.room, "room %u: silent help call ended (%s)", unsigned(unit_.room), why);
    }
    sendStatus();
}

// Once a second: who acts as the office, quiet hours, and the timers of do-not-disturb and the silent call.
void Intercom::everySecond() {
    if (frame_ > 250) {  // not in the first 5 s, before the other units were heard
        const uint16_t act = actingOffice();
        if (act != actingWas_) {
            if (actingWas_ && act != office_)
                event(EventKind::Fault, act, "room %u is acting as the office (room %u is offline)", unsigned(act), unsigned(office_));
            else if (actingWas_ && actingWas_ != office_ && act == office_)
                event(EventKind::Info, act, "room %u is the office again", unsigned(act));
            actingWas_ = act;
        }
    }
    syncTick();
    quietNowDb_ = 0;
    if (feature(kFeatQuiet) && quietFrom_ >= 0) {
        int64_t now = utcNow();
        if (now) {
            LocalTime lt = sched_.local(now);
            const int m = lt.hour * 60 + lt.minute;
            const bool in = quietFrom_ <= quietTo_ ? (m >= quietFrom_ && m < quietTo_) : (m >= quietFrom_ || m < quietTo_);
            if (in) quietNowDb_ = quietSetDb_;
        }
    }
    if (dndUntil_ && frame_ >= dndUntil_) {
        dndUntil_ = 0;
        event(EventKind::Info, unit_.room, "room %u: do not disturb ended", unsigned(unit_.room));
        sendStatus();
    }
    if (silentUntil_ && frame_ >= silentUntil_) setSilent(false, "30 minutes passed");
}

void Intercom::loadSelfCheck() {
    selfMin_ = -1;
    unsigned h = 0, m = 0;
    if (std::sscanf(hal_.store->loadText("selfcheck").c_str(), "%u:%u", &h, &m) == 2 && h < 24 && m < 60) selfMin_ = int(h * 60 + m);
    syncRev_ = uint16_t(std::strtoul(hal_.store->loadText("syncrev").c_str(), nullptr, 10));
    conWatchMin_ = std::min(60, std::max(0, std::atoi(hal_.store->loadText("conwatch").c_str())));
}

void Intercom::begin() {
    unit_ = hal_.store->loadUnit();
    unit_.sub = 0;  // letters are gone since protocol 2
    group_ = uint16_t(std::strtoul(hal_.store->loadText(kGroupKey).c_str(), nullptr, 10));
    sched_.fromString(hal_.store->loadText(kSchedKey));
    if (hal_.store->loadText("boottest") != "off") bootT0_ = 1;  // runs from the first frame
    couplings_ = CouplingTest::fromString(hal_.store->loadText("couple_map"));
    couplingsOpen_ = CouplingTest::fromString(hal_.store->loadText("couple_open_map"));
    refreshPower();
    loadSelfCheck();
    loadFeatures();
    if (!opt_.bootNote.empty())
        event(opt_.bootFault ? EventKind::Fault : EventKind::Info, unit_.room, "room %u started: %s", unsigned(unit_.room), opt_.bootNote.c_str());
    hal_.links->setWifi(hal_.store->loadText(kWifiSsidKey), hal_.store->loadText(kWifiPassKey));
    lastNonce_ = netauth::loadNonce(*hal_.store);
    params_ = TuneParams();
    params_.taps = std::min(params_.taps, opt_.maxTaps);
    std::string saved = hal_.store->loadText(kTuneKey);
    if (!saved.empty()) params_.fromString(saved);
    applyParams();
    char u[8], ip[16];
    formatUnit(unit_, u);
    formatIp(unit_, ip);
    logf("unit %s, ip %s, group %u, id %08lx, aec %s, %s tuning: %s", u, ip, unsigned(group()), (unsigned long)opt_.unitUid,
         aec_.name(), saved.empty() ? "default" : "saved", params_.toString().c_str());
}

void Intercom::applyParams() {
    if (params_.taps > opt_.maxTaps) params_.taps = opt_.maxTaps;
    aec_.configure(params_);
    applyCodec();
}

void Intercom::applyCodec() {
    Codec want = params_.opus && codecAvailable(Codec::Opus) ? Codec::Opus : Codec::Pcm16;
    if (!encoder_ || encoder_->codec() != want) encoder_ = makeEncoder(want);
    if (!encoder_) encoder_ = makeEncoder(Codec::Pcm16);
    encoder_->setBitrate(params_.bitrateKbps * 1000);
    encoder_->setFec(link_ == LinkType::WiFi, 5);  // Wi-Fi loses packets; wired doesn't
}

// ---- auto-tune -------------------------------------------------------------------------
void Intercom::startCal(const char* why) {
    if (cal_.active()) return;
    for (auto& t : talkers_) t->jb.reset();
    cal_.start(params_, opt_.maxTaps);
    logf("auto-tune started (%s): ~5 s, keep the room quiet", why);
}

void Intercom::finishCal() {
    const CalResult& r = cal_.result();
    haveCal_ = true;
    showResultUntil_ = frame_ + kResultFrames;
    logf("auto-tune: %s", r.summary().c_str());
    if (!r.ok) return;
    hal_.store->saveText(kTunePrevKey, params_.toString());
    params_ = cal_.tuned();
    applyParams();
    hal_.store->saveText(kTuneKey, params_.toString());
    logf("auto-tune applied and saved (\"undo\" restores the previous settings): %s", params_.toString().c_str());
}

// ---- console ---------------------------------------------------------------------------
std::string Intercom::command(const std::string& line, CmdSource src) {
    std::istringstream in(line);
    std::vector<std::string> a;
    std::string tok;
    while (in >> tok) a.push_back(tok);
    if (a.empty()) return "";
    const std::string& c = a[0];
    char buf[256];

    // Installer tests (auto-tune, coupling test) from the web console need the tests password, set on USB
    // with "testpass" (none set = no password). The school page sends them as "tests <password|-> <command>".
    if (c == "testpass") {
        if (src != CmdSource::Local) return "testpass only works from the USB console";
        if (a.size() == 2) hal_.store->saveText("testpass", a[1] == "off" ? "" : a[1]);
        else if (a.size() != 1) return "usage: testpass <password> | testpass off";
        return testsLocked() ? "tests password set" : "no tests password (installer tests open to the web console login)";
    }
    if (c == "tests") {
        if (a.size() < 3) return "usage: tests <password|-> <command ...>";
        if (testsLocked() && a[1] != hal_.store->loadText("testpass")) return "wrong tests password";
        std::string inner;
        for (size_t i = 2; i < a.size(); i++) inner += (i > 2 ? " " : "") + a[i];
        testsOk_ = true;
        std::string r = command(inner, src);
        testsOk_ = false;
        return r;
    }
    if (src == CmdSource::Web && testsLocked() && !testsOk_) {
        // a test started straight from /api/cmd (also "send ... cal"): needs the password
        size_t k = c == "send" && a.size() > 2 ? 2 : 0;
        if (k < a.size() && (a[k] == "cal" || (a[k] == "couple" && k + 1 < a.size() && a[k + 1] == "run")))
            return "installer tests need the tests password";
    }

    if (c == "help" || c == "?") return std::string(kHelp) + TuneParams::help();
    if (c == "get") {
        if (a.size() == 1) return params_.toString();
        std::string out, v;
        for (size_t i = 1; i < a.size(); i++) {
            if (!params_.get(a[i], &v)) return "unknown parameter " + a[i];
            out += (out.empty() ? "" : " ") + a[i] + "=" + v;
        }
        return out;
    }
    if (c == "set") {
        if (a.size() < 3 || a.size() % 2 == 0) return "usage: set key value [key value ...]";
        TuneParams p = params_;
        std::string out, v;
        for (size_t i = 1; i + 1 < a.size(); i += 2) {
            if (!p.set(a[i], a[i + 1])) return "bad parameter or value: " + a[i] + " " + a[i + 1];
            p.get(a[i], &v);
            out += (out.empty() ? "" : " ") + a[i] + "=" + v;
        }
        params_ = p;
        applyParams();
        logf("tuning set: %s", out.c_str());
        return "ok " + out + " (not saved; \"save\" keeps it)";
    }
    if (c == "save") {
        hal_.store->saveText(kTunePrevKey, hal_.store->loadText(kTuneKey));
        hal_.store->saveText(kTuneKey, params_.toString());
        return "saved: " + params_.toString();
    }
    if (c == "load") {
        std::string s = hal_.store->loadText(kTuneKey);
        params_ = TuneParams();
        if (!s.empty()) params_.fromString(s);
        applyParams();
        return "loaded: " + params_.toString();
    }
    if (c == "defaults") {
        params_ = TuneParams();
        applyParams();
        return "defaults (not saved): " + params_.toString();
    }
    if (c == "undo") {
        std::string prev = hal_.store->loadText(kTunePrevKey);
        if (prev.empty()) return "nothing to undo";
        std::string cur = hal_.store->loadText(kTuneKey);
        params_ = TuneParams();
        params_.fromString(prev);
        applyParams();
        hal_.store->saveText(kTuneKey, prev);
        hal_.store->saveText(kTunePrevKey, cur);
        return "restored and saved: " + params_.toString();
    }
    if (c == "cal") {
        if (a.size() > 1 && a[1] == "result") return haveCal_ ? cal_.result().summary() : "no auto-tune run yet";
        if (cal_.active() || testPhase_) return "auto-tune or self-test already running";
        startCal("console");
        return "auto-tune started: ~5 s, keep the room quiet; result in the log and \"cal result\"";
    }
    if (c == "stats") {
        const AecStats& s = aec_.stats();
        static const char* link[] = {"none", "ethernet", "wifi"};
        snprintf(buf, sizeof(buf), "erle_db=%.1f far=%d double_talk=%d converged=%d nlp_gain=%.2f link=%s mic=%u sent=%u spk=%u talkers=%d codec=%s",
                 double(s.erleDb), s.farActive, s.doubleTalk, s.converged, double(s.nlpGain), link[int(link_)], micLevel_,
                 sentLevel_, spkLevel_, playing_, codecName(sendCodec()));
        return buf;
    }
    if (c == "unit") {
        if (a.size() > 1) {
            uint16_t room;
            uint8_t sub;
            if (!parseTarget(a[1], &room, &sub) || room == kTargetAllRooms || sub != kTargetAnySub || room == 0)
                return "usage: unit 214 (1-999, no letters; several units in one room: own numbers plus group)";
            UnitId id;
            id.room = room;
            setUnit(id);
        }
        char u[8], ip[16];
        formatUnit(unit_, u);
        formatIp(unit_, ip);
        snprintf(buf, sizeof(buf), "unit %s ip %s group %u%s", u, ip, unsigned(group()),
                 duplicate() ? " DUPLICATE: another unit uses this number" : "");
        return buf;
    }
    if (c == "group") {
        if (a.size() > 1) {
            if (a[1] == "off") {
                group_ = 0;
            } else {
                unsigned long g = std::strtoul(a[1].c_str(), nullptr, 10);
                if (g < 1 || g > 999) return "usage: group 299 | group off";
                group_ = uint16_t(g == unit_.room ? 0 : g);
            }
            hal_.store->saveText(kGroupKey, group_ ? std::to_string(group_) : "");
            logf("room group set to %u", unsigned(group()));
            sendStatus();
        }
        snprintf(buf, sizeof(buf), "group %u%s", unsigned(group()), group_ ? "" : " (own room)");
        return buf;
    }
    if (c == "version") return std::string("firmware ") + kFirmwareVersion;
    if (c == "boottest") {
        if (a.size() > 1 && (a[1] == "on" || a[1] == "off")) hal_.store->saveText("boottest", a[1] == "off" ? "off" : "");
        if (a.size() > 1 && a[1] == "run") bootT0_ = frame_ ? frame_ : 1;
        return std::string("power-up light test ") + (hal_.store->loadText("boottest") == "off" ? "off" : "on") +
               (bootTesting() ? " (running)" : "");
    }
    if (c == "lamp") {
        static const char* names[] = {"white", "ir", "other"};
        if (a.size() >= 3) {
            int which = -1;
            for (int i = 0; i < int(Lamp::Count); i++)
                if (a[1] == names[i]) which = i;
            if (which < 0) return "usage: lamp white|ir|other 0-100|auto";
            if (a[2] == "auto") lampManual_[which] = 255;
            else lampManual_[which] = uint8_t(std::min(100L, std::max(0L, std::strtol(a[2].c_str(), nullptr, 10))));
            updateLamps();
        }
        std::string out;
        for (int i = 0; i < int(Lamp::Count); i++)
            out += std::string(i ? ", " : "") + names[i] + " " + std::to_string(lamp_[i]) + "%" + (lampManual_[i] == 255 ? " (auto)" : " (manual)");
        return out;
    }
    if (c == "ntp") {  // the school's time server (default: public pools); used from the next restart
        if (a.size() > 1) hal_.store->saveText("ntp", a[1] == "default" ? "" : a[1]);
        std::string s = hal_.store->loadText("ntp");
        return "ntp server: " + (s.empty() ? std::string("pool.ntp.org (default)") : s) + (a.size() > 1 ? " (from the next restart)" : "");
    }
    if (c == "reboot") {
        if (a.size() > 2 || (a.size() == 2 && a[1] != "download")) return "usage: reboot [download]";
        if (!opt_.restart) return "reboot is not available on this platform";
        restartDownload_ = a.size() == 2;
        restartAt_ = frame_ + 25;
        logf("restart requested (%s)%s", src == CmdSource::Local ? "USB" : src == CmdSource::Web ? "web" : "network",
             restartDownload_ ? ": USB flashing mode" : "");
        return restartDownload_ ? "restarting into USB flashing mode in 0.5 s (flash it, then reset or power-cycle)"
                                : "restarting in 0.5 s";
    }
    if (c == "couple") {
        if (a.size() > 1 && a[1] == "clear") {
            couplings_.clear();
            couplingsOpen_.clear();
            hal_.store->saveText("couple_map", "");
            hal_.store->saveText("couple_open_map", "");
            return "coupling maps cleared (no ducking)";
        }
        if (a.size() > 1 && a[1] == "run") {
            if (a.size() < 5 || (a.size() > 5 && a[5] != "open" && a[5] != "closed"))
                return "usage: couple run <seconds per unit, 1-3> auto|manual 101,102,214,... [closed|open] (doors; default closed)";
            if (cal_.active() || testPhase_ || couple_.active()) return "busy (auto-tune, self-test or coupling test running)";
            float secs = std::strtof(a[2].c_str(), nullptr);
            std::vector<uint16_t> order;
            std::stringstream ss(a[4]);
            std::string r;
            while (std::getline(ss, r, ',')) {
                unsigned long n = std::strtoul(r.c_str(), nullptr, 10);
                if (n >= 1 && n <= 999) order.push_back(uint16_t(n));
            }
            std::string err = couple_.start(order, unit_.room, frame_, uint32_t(secs * 1000 / kFrameMs), a[3] == "auto");
            if (!err.empty()) return "couple: " + err;
            coupleOpen_ = a.size() > 5 && a[5] == "open";
            for (auto& t : talkers_) t->jb.reset();
            logf("coupling test started: %u units, %.0f s", unsigned(order.size()), double(couple_.totalFrames() * kFrameMs / 1000));
            snprintf(buf, sizeof(buf), "coupling test running, about %.0f s; then \"couple\" shows the result",
                     double(couple_.totalFrames() * kFrameMs / 1000));
            return buf;
        }
        if (couple_.active()) return "coupling test running";
        if (couplings_.empty() && couplingsOpen_.empty()) return "no coupling test run yet";
        std::string out = "coupling (dB vs this unit's own speaker):";
        for (int open = 0; open < 2; open++) {
            const auto& m = open ? couplingsOpen_ : couplings_;
            out += open ? "\ndoors open:" : "\ndoors closed:";
            if (m.empty()) out += " not run yet";
            for (const auto& r : m) {
                char b[64];
                if (!r.heard) snprintf(b, sizeof(b), " %u not heard;", unsigned(r.room));
                else snprintf(b, sizeof(b), " %u %.0f dB%s;", unsigned(r.room), double(r.db),
                              r.db >= CouplingTest::kSameRoomDb ? " (same room)" : r.db >= CouplingTest::kCoupledDb ? " (through wall/door)" : "");
                out += b;
            }
        }
        return out;
    }
    if (c == "mute") {  // office: stop a unit's TALK (stuck button, misuse); it still hears everyone
        if (a.size() > 1 && a[1] == "off") {
            muteUntil_ = 0;
        } else if (a.size() > 1) {
            float mins = std::strtof(a[1].c_str(), nullptr);
            if (!(mins > 0) || mins > 1440) return "usage: mute <minutes 1-1440> | mute off | mute";
            muteUntil_ = frame_ + uint32_t(mins * 60000 / kFrameMs);
            logf("TALK muted for %.0f min (%s)", double(mins), src == CmdSource::Local ? "USB" : "office");
        }
        sendStatus();
        if (!muted()) return "not muted";
        snprintf(buf, sizeof(buf), "TALK muted, %u min left", unsigned((muteUntil_ - frame_) * kFrameMs / 60000 + 1));
        return buf;
    }
    if (c == "talk") {
        if (src != CmdSource::Local) return "talk only works from the USB console";
        if (a.size() > 1 && a[1] == "off") {
            talkFrames_ = 0;
            return "talk off";
        }
        float secs = a.size() > 1 ? std::strtof(a[1].c_str(), nullptr) : 5.0f;
        secs = std::max(0.0f, std::min(secs, 60.0f));
        TalkTarget t;
        if (a.size() > 2) {
            unsigned long n = a.size() > 3 ? std::strtoul(a[3].c_str(), nullptr, 10) : 0;
            if (a[2] == "room") t.kind = TalkTarget::Room;
            else if (a[2] == "floor") t.kind = TalkTarget::Floor;
            else if (a[2] == "group") t.kind = TalkTarget::Group;
            else if (a[2] == "zone") t.kind = TalkTarget::Zone;
            else n = 0;
            if (n < 1 || n > 999) return "usage: talk [seconds] [room N|floor N|group N|zone N]";
            t.n = uint16_t(n);
        }
        talkTarget_ = t;
        talkFrames_ = uint32_t(secs * 1000 / kFrameMs);
        static const char* kinds[] = {"everyone", "room", "floor", "group", "zone"};
        std::string num = t.kind ? " " + std::to_string(t.n) : std::string();
        snprintf(buf, sizeof(buf), "talking for %.0f s to %s%s (\"talk off\" stops)", double(secs), kinds[t.kind], num.c_str());
        return buf;
    }
    if (c == "time") {
        if (a.size() > 1) {
            long long t = std::strtoll(a[1].c_str(), nullptr, 10);
            if (t < 1600000000LL || t > 4100000000LL) return "usage: time <UTC seconds since 1970>";
            setEpoch_ = t;
            setFrame_ = frame_;
        }
        int64_t now = utcNow();
        if (!now) return "no clock yet (no NTP; the office console sets it)";
        LocalTime lt = sched_.local(now);
        static const char* wd[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        bool ntp = opt_.utcNow && opt_.utcNow() > 1600000000LL;
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d %s%s, clock from %s%s", lt.year, lt.month, lt.day, lt.hour, lt.minute,
                 lt.second, wd[lt.wday], lt.dst ? " (daylight saving)" : "", ntp ? "NTP" : "the office",
                 sched_.isDayOff(lt) ? ", day off: no timed events today" : "");
        return buf;
    }
    if (c == "tz") {
        if (a.size() < 2) return sched_.list().substr(0, sched_.list().find('\n'));
        int m = std::atoi(a[1].c_str());
        if (m < -720 || m > 840) return "usage: tz <minutes from UTC, standard time> [none|us|eu]";
        sched_.tzMinutes = m;
        if (a.size() > 2) sched_.dst = a[2] == "us" ? DstRule::Us : a[2] == "eu" ? DstRule::Eu : DstRule::None;
        saveSchedule();
        return "ok " + sched_.list().substr(0, sched_.list().find('\n'));
    }
    if (c == "sched" || c == "dayoff" || c == "skip") return schedCommand(a);
    if (c == "play") {
        Sound s;
        if (a.size() < 2 || !soundFromName(a[1], &s)) return "usage: play " + soundList();
        startSound(s);
        return std::string("playing ") + soundName(s);
    }
    if (c == "send") {  // web console / USB: "send all sched", "send 214 stats"
        if (src == CmdSource::Network) return "send only works from this unit's web page or USB";
        if (a.size() < 3) return "usage: send <all|room> <command ...>";
        std::string cmd;
        for (size_t i = 2; i < a.size(); i++) cmd += (i > 2 ? " " : "") + a[i];
        return sendSigned(a[1], cmd);
    }
    if (c == "shutter") {  // closed | open | flag | auto: move the camera shutter by hand (to set it up); "auto" gives it back
        if (a.size() == 1) return std::string("shutter: ") + (shutter_ == Shutter::Flag ? "flag" : shutter_ == Shutter::Open ? "open" : "closed") + (shutterManual_ >= 0 ? " (by hand)" : "");
        if (src == CmdSource::Network) return "shutter only works from USB or this unit's web page";
        const int k = a[1] == "closed" ? 0 : a[1] == "open" ? 1 : a[1] == "flag" ? 2 : a[1] == "auto" ? -1 : -2;
        if (k == -2) return "usage: shutter [closed|open|flag|auto]";
        shutterManual_ = int8_t(k);
        return "shutter " + a[1];
    }
    if (c == "shutterus") {  // the servo's pulse width in microseconds for closed, open and flag
        if (a.size() == 4) {
            unsigned v[3];
            for (int i = 0; i < 3; i++) {
                v[i] = unsigned(std::strtoul(a[size_t(i) + 1].c_str(), nullptr, 10));
                if (v[i] < 400 || v[i] > 2600) return "usage: shutterus CLOSED OPEN FLAG  (400-2600 microseconds each)";
            }
            for (int i = 0; i < 3; i++) shutterUs_[i] = uint16_t(v[i]);
            hal_.store->saveText("shutterus", a[1] + " " + a[2] + " " + a[3]);
            shutterSent_ = false;  // go to the new place now
        } else if (a.size() != 1) {
            return "usage: shutterus CLOSED OPEN FLAG  (400-2600 microseconds each)";
        }
        return "shutterus " + std::to_string(shutterUs_[0]) + " " + std::to_string(shutterUs_[1]) + " " + std::to_string(shutterUs_[2]);
    }
    if (c == "ring") {  // the office rings this room (no audio): attention until TALK answers, then talk to the office
        UnitId from;
        if (a.size() > 1 && !parseTarget(a[1], &from.room, &from.sub)) return "usage: ring [office unit, e.g. 105]";
        if (from.sub == kTargetAnySub) from.sub = 0;
        callFrom_ = from;
        callLast_ = frame_ ? frame_ : 1;
        callAnswered_ = false;
        missedUntil_ = 0;
        ringUntil_ = callLast_ + kAnswerFrames;
        startSound(Sound::Chime);
        logf("ringing (answers go to %s)", from.room ? ("unit " + std::to_string(from.room)).c_str() : "the office console");
        return "ringing for 10 s (TALK answers)";
    }
    if (c == "callofc") {
        const char* by = src == CmdSource::Local ? "USB" : "console";
        const std::string how = a.size() > 1 ? a[1] : "";
        if (how == "silent") {  // as OFFICE held 5 s
            if (!feature(kFeatSilent)) return "the silent feature is off (feature silent on)";
            setSilent(true, by);
            return "silent help call started";
        }
        if (how == "off") setSilent(false, by);
        if (how == "help") {  // as OFFICE held 1.5 s
            if (!feature(kFeatReason)) return "the reason feature is off (feature reason on)";
            const bool was = callingOffice_;
            urgent_ = true;
            if (was) {
                event(EventKind::Fault, unit_.room, "room %u: HELP NEEDED (%s)", unsigned(unit_.room), by);
                sendStatus();
            }
        }
        setCallingOffice(how != "off", by);
        return callingOffice_ ? (urgent_ ? "calling the office: help needed" : "calling the office") : "not calling the office";
    }
    if (c == "selftest") {
        if (a.size() > 1 && a[1] == "result") return testResult_.empty() ? "no self-test run yet" : testResult_;
        if (cal_.active() || testPhase_) return "busy (auto-tune or self-test running)";
        startSelfTest(false);
        return "self-test started (~1.5 s, plays a tone); \"selftest result\" shows the result";
    }
    if (c == "netkey") {
        if (src != CmdSource::Local) return "netkey only works from the USB console";
        if (a.size() != 2)
            return netauth::loadKey(*hal_.store).empty() ? "network commands off (no key)" : "network commands on (key set)";
        if (a[1] == "off") {
            netauth::saveKey(*hal_.store, "");
            return "network commands off";
        }
        if (int(a[1].size()) < netauth::kMinKeyLen) return "key too short (at least 16 characters, no spaces)";
        netauth::saveKey(*hal_.store, a[1]);
        logf("network key changed");
        return "network key set; network commands on";
    }
    if (c == "tone") {
        float secs = a.size() > 1 ? std::strtof(a[1].c_str(), nullptr) : 2.0f;
        if (secs < 0) secs = 0;
        if (secs > 30) secs = 30;
        toneFrames_ = uint32_t(secs * 1000 / kFrameMs);
        return toneFrames_ ? "playing a 1 kHz test tone" : "tone stopped";
    }
    if (c == "wifi") {
        if (src != CmdSource::Local) return "wifi only works from the USB console";
        if (a.size() == 2 && a[1] == "off") {
            hal_.store->saveText(kWifiSsidKey, "");
            hal_.store->saveText(kWifiPassKey, "");
            hal_.links->setWifi("", "");
            return "wifi off";
        }
        if (a.size() == 3) {
            if (a[2].size() < 8) return "password too short (WPA2 needs at least 8 characters)";
            hal_.store->saveText(kWifiSsidKey, a[1]);
            hal_.store->saveText(kWifiPassKey, a[2]);
            hal_.links->setWifi(a[1], a[2]);
            logf("Wi-Fi set to \"%s\"", a[1].c_str());
            return "wifi set to \"" + a[1] + "\"; it connects in the background";
        }
        std::string ssid = hal_.store->loadText(kWifiSsidKey);
        if (ssid.empty()) return "wifi off (usage: wifi <ssid> <password>; names with spaces are not supported)";
        return "wifi \"" + ssid + "\" " + (hal_.links->wifiUp() ? "connected" : "not connected");
    }
    if (c == "feature") {
        if (a.size() == 3 && (a[2] == "on" || a[2] == "off")) {
            int k = -1;
            for (int i = 0; i < 8; i++)
                if (a[1] == kFeatureNames[i]) k = i;
            if (k < 0 && a[1] != "all") return kFeatureUsage;
            const uint8_t bits = k < 0 ? uint8_t(0xff) : uint8_t(1u << k);
            features_ = a[2] == "on" ? uint8_t(features_ | bits) : uint8_t(features_ & ~bits);
            hal_.store->saveText("features", features_ ? std::to_string(features_) : "");
            if (!feature(kFeatDnd)) dndUntil_ = 0;
            if (!feature(kFeatReason)) urgent_ = false;
            logf("feature %s %s", a[1].c_str(), a[2].c_str());
            sendStatus();
        } else if (a.size() != 1) {
            return kFeatureUsage;
        }
        std::string out = "features:";
        for (int i = 0; i < 8; i++) out += std::string(" ") + kFeatureNames[i] + ((features_ >> i) & 1 ? "=on" : "=off");
        return out;
    }
    if (c == "office") {  // "office 105 104 901 902": the office units in order; the first one that is online acts as the office
        static const char* const usage = "usage: office | office ROOM [STAND-IN ROOM ...] (up to 4) | office off";
        if (a.size() > 1) {
            std::string list;
            if (a[1] == "off") {
                if (a.size() > 2) return usage;
            } else {
                if (a.size() > 5) return usage;
                std::vector<unsigned long> seen;
                for (size_t i = 1; i < a.size(); i++) {
                    const unsigned long r = std::strtoul(a[i].c_str(), nullptr, 10);
                    if (r == 0 && i > 1 && a[i] == "0") continue;  // "office 105 0": no stand-in (the older form)
                    if (r < 1 || r > 999 || std::find(seen.begin(), seen.end(), r) != seen.end()) return usage;
                    seen.push_back(r);
                    list += (list.empty() ? "" : " ") + std::to_string(r);
                }
            }
            hal_.store->saveText("office", list);
            loadFeatures();
        }
        if (!office_) return "no office unit set (usage: office ROOM [STAND-IN ROOM ...])";
        std::string out = "office unit " + std::to_string(office_) + ", stand-in";
        for (size_t i = 1; i < offices_.size(); i++) out += " " + std::to_string(offices_[i]);
        if (offices_.size() < 2) out += " (none)";
        else if (!feature(kFeatFailover)) out += " (feature failover is off)";
        return out + ", acting now: " + std::to_string(actingOffice());
    }
    if (c == "zone") {
        if (a.size() > 1) {
            unsigned long n = std::strtoul(a[1].c_str(), nullptr, 10);
            if (n < 1 || n > 8 || a.size() < 3 || a.size() > 4 || (a.size() == 3 && a[2] != "off")) return kZoneUsage;
            if (a.size() == 4) {
                if (a[2].empty() || a[2].size() > 12 || a[3].size() > 100) return kZoneUsage;
                for (char ch : a[2])
                    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '-' || ch == '_')) return kZoneUsage;
                for (char ch : a[3])
                    if (!((ch >= '0' && ch <= '9') || ch == '-' || ch == ',')) return kZoneUsage;
            }
            std::string s;
            for (const Zone& z : zones_)
                if (z.n != n) s += std::to_string(z.n) + "=" + z.name + ":" + z.rooms + ";";
            if (a.size() == 4) s += std::to_string(n) + "=" + a[2] + ":" + a[3] + ";";
            hal_.store->saveText("zones", s);
            loadFeatures();
        }
        if (zones_.empty()) return std::string("no zones. ") + kZoneUsage;
        std::string out = feature(kFeatZones) ? "" : "(feature zones is off)\n";
        for (const Zone& z : zones_)
            out += "zone " + std::to_string(z.n) + " " + z.name + ": " + z.rooms + (inZone(z.n, unit_.room) ? "  (this room is in it)\n" : "\n");
        return out;
    }
    if (c == "dnd") {
        if (a.size() == 2 && a[1] == "off") {
            if (dndUntil_) event(EventKind::Info, unit_.room, "room %u: do not disturb ended", unsigned(unit_.room));
            dndUntil_ = 0;
            sendStatus();
        } else if (a.size() == 2) {
            if (!feature(kFeatDnd)) return "the dnd feature is off (feature dnd on)";
            float mins = std::strtof(a[1].c_str(), nullptr);
            if (!(mins >= 1) || mins > 240) return "usage: dnd <minutes 1-240> | dnd off | dnd";
            dndUntil_ = frame_ + uint32_t(mins * 60000 / kFrameMs);
            event(EventKind::Info, unit_.room, "room %u: do not disturb for %u min", unsigned(unit_.room), unsigned(mins));
            sendStatus();
        } else if (a.size() != 1) {
            return "usage: dnd <minutes 1-240> | dnd off | dnd";
        }
        if (!dnd()) return "do not disturb off";
        snprintf(buf, sizeof(buf), "do not disturb, %u min left", unsigned(((dndUntil_ - frame_) * kFrameMs + 59999) / 60000));
        return buf;
    }
    if (c == "quiet") {
        if (a.size() == 2 && a[1] == "off") {
            hal_.store->saveText("quiet", "");
            loadFeatures();
        } else if (a.size() == 4) {
            unsigned h1 = 0, m1 = 0, h2 = 0, m2 = 0;
            float db = std::strtof(a[3].c_str(), nullptr);
            if (std::sscanf(a[1].c_str(), "%u:%u", &h1, &m1) != 2 || std::sscanf(a[2].c_str(), "%u:%u", &h2, &m2) != 2 || h1 > 23 || m1 > 59 ||
                h2 > 23 || m2 > 59 || !(db <= 0) || db < -30)
                return kQuietUsage;
            snprintf(buf, sizeof(buf), "%02u:%02u %02u:%02u %.0f", h1, m1, h2, m2, double(db));
            hal_.store->saveText("quiet", buf);
            loadFeatures();
        } else if (a.size() != 1) {
            return kQuietUsage;
        }
        if (quietFrom_ < 0) return std::string("no quiet hours. ") + kQuietUsage;
        snprintf(buf, sizeof(buf), "quiet hours %02d:%02d to %02d:%02d, %.0f dB%s%s", (quietFrom_ / 60) % 100, quietFrom_ % 60, (quietTo_ / 60) % 100,
                 quietTo_ % 60, double(quietSetDb_), feature(kFeatQuiet) ? "" : " (feature quiet is off)", quietNowDb_ < 0 ? " (on now)" : "");
        return buf;
    }
    if (c == "log") {  // "log", "log calls", "log faults", "log 50"
        int want = -1;
        size_t count = 30;
        for (size_t i = 1; i < a.size(); i++) {
            if (a[i] == "calls") want = int(EventKind::Call);
            else if (a[i] == "faults") want = int(EventKind::Fault);
            else count = size_t(std::max(1L, std::min(200L, std::strtol(a[i].c_str(), nullptr, 10))));
        }
        std::vector<const Event*> pick;
        for (auto it = events_.rbegin(); it != events_.rend() && pick.size() < count; ++it)
            if (want < 0 || int(it->kind) == want) pick.push_back(&*it);
        if (pick.empty()) return "nothing logged since this unit started";
        static const char* kinds[] = {"FAULT", "call ", "info "};
        std::string out;
        for (auto it = pick.rbegin(); it != pick.rend(); ++it) {
            const Event& e = **it;
            if (e.utc) {
                LocalTime lt = sched_.local(e.utc);
                snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d:%02d ", lt.month % 100, lt.day % 100, lt.hour % 100, lt.minute % 100, lt.second % 100);
            } else {
                snprintf(buf, sizeof(buf), "+%us ", unsigned(e.frame / 50));
            }
            out += std::string(buf) + kinds[int(e.kind)] + " " + e.text + "\n";
        }
        return out;
    }
    if (c == "sync" || c == "syncrev") return syncCommand(a, line);
    if (c == "consolewatch") {  // the office unit chirps while no office page is open
        if (a.size() == 2) {
            const int n = std::atoi(a[1].c_str());
            if (a[1] == "off") hal_.store->saveText("conwatch", "");
            else if (n >= 1 && n <= 60) hal_.store->saveText("conwatch", std::to_string(n));
            else return "usage: consolewatch [1-60|off]";
            loadSelfCheck();
        } else if (a.size() > 2) {
            return "usage: consolewatch [1-60|off]";
        }
        snprintf(buf, sizeof(buf), "console watch: the office unit chirps after %d min without the office page%s", conWatchMin_, conMissing_ ? "; NOT OPEN NOW" : "");
        return conWatchMin_ ? buf : "console watch off";
    }
    if (c == "selfcheck") {  // nightly speaker->microphone check
        if (a.size() == 2 && a[1] == "now") {
            if (cal_.active() || testPhase_) return "busy (auto-tune or self-test running)";
            startSelfTest(true);
            return "self-check started (~1.5 s, plays a tone)";
        }
        if (a.size() == 2) {
            unsigned h = 0, m = 0;
            if (a[1] == "off") hal_.store->saveText("selfcheck", "");
            else if (std::sscanf(a[1].c_str(), "%u:%u", &h, &m) == 2 && h < 24 && m < 60) hal_.store->saveText("selfcheck", a[1]);
            else return "usage: selfcheck [HH:MM|off|now]";
            loadSelfCheck();
        } else if (a.size() > 2) {
            return "usage: selfcheck [HH:MM|off|now]";
        }
        snprintf(buf, sizeof(buf), "nightly self-check at %02d:%02d", (selfMin_ / 60) % 100, selfMin_ % 60);
        return std::string(selfMin_ < 0 ? "nightly self-check off" : buf) + (audioFault_ ? "; THE LAST CHECK FAILED (see: selftest result)" : "");
    }
    if (c == "emergency") {  // lockdown | evacuate | shelter | clear; "all" also tells every other unit
        if (a.size() == 1) return std::string("emergency: ") + (emerg_ == Emergency::None ? "none" : kEmergencyNames[int(emerg_)]);
        int k = -1;
        for (int i = 0; i < 4; i++)
            if (a[1] == kEmergencyNames[i]) k = i;
        const bool all = a.size() == 3 && a[2] == "all";
        if (k < 0 || a.size() > 3 || (a.size() == 3 && !all)) return "usage: emergency [lockdown|evacuate|shelter|clear] [all]";
        if (all && src == CmdSource::Network) return "\"emergency ... all\" only works from USB or this unit's web page";
        setEmergency(Emergency(k), src == CmdSource::Local ? "USB" : src == CmdSource::Web ? "web page" : "network");
        std::string out = k ? std::string("EMERGENCY: ") + kEmergencyNames[k] : std::string("emergency cleared");
        if (all) {
            std::string r = sendSigned("all", std::string("emergency ") + kEmergencyNames[k]);
            if (r.compare(0, 4, "sent") != 0) {
                emergTxKind_ = -1;
                return out + " ON THIS UNIT ONLY, the other units were not told: " + r;
            }
            emergTxKind_ = k;
            emergTxCount_ = 1;
            emergTxNext_ = frame_ + 100;
            out += ", sent to all units (and repeated)";
        }
        return out;
    }
    if (c == "backup") return backupText();
    if (c == "restore") {
        size_t p = line.find("restore");
        std::string err = restoreLine(line.substr(p + 7));
        return err.empty() ? "ok" : err;
    }
    if (c == "netupdate") {  // firmware updates from the web page / net_update.py: switched on over USB only
        if (a.size() > 2 || (a.size() == 2 && a[1] != "on" && a[1] != "off")) return "usage: netupdate [on|off]";
        if (a.size() == 2) {
            if (src != CmdSource::Local) return "netupdate only works from the USB console";
            if (!hal_.fw) return "firmware updates over the network are not available on this unit";
            hal_.store->saveText("netupdate", a[1] == "on" ? "on" : "");
            logf("firmware updates over the network: %s", a[1].c_str());
        }
        return std::string("firmware updates over the network: ") + (netUpdateAllowed() ? "on" : "off");
    }
    if (c == "webpass") {
        if (src != CmdSource::Local) return "webpass only works from the USB console";
        if (a.size() != 2) return auth::hasPassword(*hal_.store) ? "web interface on (password set)" : "web interface off (no password)";
        if (a[1] == "off") {
            auth::clearPassword(*hal_.store);
            return "web interface off";
        }
        if (int(a[1].size()) < auth::kMinPasswordLen) return "password too short (at least 8 characters, no spaces)";
        auth::setPassword(*hal_.store, a[1], frame_ ^ unit_.key());
        logf("web password changed");
        return "web password set; web interface on";
    }
    return "unknown command \"" + c + "\" (try help)";
}

// Web console: send a console command to one unit or all of them, signed with this unit's network key,
// as the PC console does. Units only take a nonce higher than the last one they accepted; the PC console
// uses milliseconds since 1970, so this does too (it needs the clock), and never repeats or goes back.
std::string Intercom::sendSigned(const std::string& target, const std::string& cmd) {
    std::string key = netauth::loadKey(*hal_.store);
    if (key.empty()) return "no network key on this unit (set it on USB: netkey <key>)";
    int64_t now = utcNow();
    if (!now) return "no clock yet (NTP or the office console sets it): can't sign commands";
    TextPacket t;
    t.type = PacketType::Command;
    t.from = unit_;
    if (!parseTarget(target, &t.targetRoom, &t.targetSub)) return "bad target (all, or a room number)";
    if (sentNonce_ == 0) sentNonce_ = std::strtoull(hal_.store->loadText("send_nonce").c_str(), nullptr, 10);
    uint64_t nonce = std::max<uint64_t>(sentNonce_ + 1, uint64_t(now) * 1000 + (frame_ % 50) * 20);
    sentNonce_ = nonce;
    hal_.store->saveText("send_nonce", std::to_string(nonce));
    t.text = cmd;
    t.text = netauth::sign(key, nonce, t);
    uint8_t buf[kMaxPacketBytes + 16];
    hal_.net->send(buf, encodeText(t, buf));
    bool secret = cmd.compare(0, 7, "webpass") == 0 || cmd.compare(0, 6, "netkey") == 0;
    logf("web console sent to %s: %s", target.c_str(), secret ? "(password command, hidden)" : cmd.c_str());
    return "sent to " + target;
}

void Intercom::sendReply(const TextPacket& to, const std::string& text) {
    // Split long replies (help) into packets on line boundaries.
    TextPacket r;
    r.type = PacketType::Reply;
    r.from = unit_;
    r.targetRoom = to.from.room;
    r.targetSub = to.from.sub;
    size_t pos = 0;
    uint8_t buf[kMaxPacketBytes];
    do {
        size_t n = std::min(text.size() - pos, kMaxTextBytes);
        if (pos + n < text.size()) {
            size_t nl = text.rfind('\n', pos + n);
            if (nl != std::string::npos && nl > pos) n = nl + 1 - pos;
        }
        r.text = text.substr(pos, n);
        hal_.net->send(buf, encodeText(r, buf));
        pos += n;
    } while (pos < text.size());
}

// ---- network ---------------------------------------------------------------------------
void Intercom::updateLink() {
    bool eth = ethUp_ = hal_.links->ethernetUp();
    bool wifi = hal_.links->wifiUp();
    ethStable_ = eth ? ethStable_ + 1 : 0;

    LinkType want = LinkType::None;
    if (eth && ethStable_ >= kEthStableFrames) want = LinkType::Ethernet;
    else if (wifi) want = LinkType::WiFi;
    else if (eth) want = LinkType::Ethernet;  // only link available, use it while it settles

    if (want == link_) return;
    static const char* names[] = {"none", "Ethernet", "Wi-Fi"};
    if (link_ == LinkType::None) {
        logf("link %s, %u ms %s", names[int(want)], unsigned((frame_ - linkLostAt_) * kFrameMs),
             everLinked_ ? "without network" : "after start");
    } else {
        logf("link %s -> %s", names[int(link_)], names[int(want)]);
    }
    if (want == LinkType::None) event(EventKind::Fault, unit_.room, "room %u: network lost", unsigned(unit_.room));
    else if (link_ == LinkType::None && everLinked_) event(EventKind::Info, unit_.room, "room %u: network back (%s)", unsigned(unit_.room), names[int(want)]);
    else if (want == LinkType::WiFi && link_ == LinkType::Ethernet) event(EventKind::Fault, unit_.room, "room %u: Ethernet lost, now on Wi-Fi", unsigned(unit_.room));
    else if (want == LinkType::Ethernet && link_ == LinkType::WiFi) event(EventKind::Info, unit_.room, "room %u: back on Ethernet", unsigned(unit_.room));
    if (want == LinkType::None) linkLostAt_ = frame_;
    else everLinked_ = true;
    link_ = want;
    hal_.links->useLink(want);
    if (encoder_) encoder_->setFec(link_ == LinkType::WiFi, 5);
    sendStatus();  // tell the console right away
}

Intercom::Talker* Intercom::talkerFor(const UnitId& id) {
    for (auto& t : talkers_)
        if (t->id == id) return t.get();
    if (talkers_.size() >= kMaxTalkers) return nullptr;
    talkers_.push_back(std::unique_ptr<Talker>(new Talker()));
    talkers_.back()->id = id;
    return talkers_.back().get();
}

void Intercom::setUnit(const UnitId& id) {
    if (id == unit_) return;
    unit_ = id;
    hal_.store->saveUnit(unit_);
    hal_.links->setAddress(unit_);
    char u[8], ip[16];
    formatUnit(unit_, u);
    formatIp(unit_, ip);
    logf("room set to %s, ip %s", u, ip);
    sendStatus();
}

void Intercom::startSelfTest(bool automatic) {
    testBoard_ = hal_.diag ? hal_.diag->boardTest() : std::string("ok board checks not available on this platform\n");
    testPhase_ = 1;
    testCount_ = 0;
    testQuiet_ = testLoud_ = 0;
    testAuto_ = automatic;
}

// Nightly self-check (R3.1): at the set time, when the unit is idle. Each room starts at its own second,
// so a neighbour's tone doesn't fall into this room's quiet measurement.
void Intercom::checkSelfCheck(bool busy) {
    busy = busy || soundActive_ || cal_.active() || testPhase_ || setup_.active() || emerg_ != Emergency::None;
    if (selfRetryAt_ && frame_ >= selfRetryAt_ && !busy) {
        selfRetryAt_ = 0;
        startSelfTest(true);
        return;
    }
    if (selfMin_ < 0 || busy) return;
    int64_t now = utcNow();
    if (!now) return;
    LocalTime lt = sched_.local(now);
    if (lt.hour * 60 + lt.minute != selfMin_ || lt.ymd() == selfDay_ || lt.second < int((unit_.room * 7u) % 50u)) return;
    selfDay_ = lt.ymd();
    startSelfTest(true);
}

// Self-test audio check: 0.5 s quiet, then 1 s of 1 kHz tone; the mic must hear the tone.
void Intercom::selfTestFrame(const int16_t* mic, int16_t* speaker) {
    constexpr int kQuietFrames = 25, kToneFrames = 50, kSkipFrames = 5;
    double p = 0;
    for (int i = 0; i < kFrameSamples; i++) p += double(mic[i]) * mic[i];
    p /= kFrameSamples;
    std::memset(speaker, 0, sizeof(int16_t) * kFrameSamples);
    if (testPhase_ == 1) {
        testQuiet_ += p;
        if (++testCount_ >= kQuietFrames) {
            testPhase_ = 2;
            testCount_ = 0;
        }
        return;
    }
    const float w = 2.0f * 3.14159265f * 1000.0f / kSampleRate;
    for (int i = 0; i < kFrameSamples; i++, tonePhase_++)
        speaker[i] = int16_t(8200.0f * std::sin(w * float(tonePhase_ % kSampleRate)));
    if (testCount_ >= kSkipFrames) testLoud_ += p;  // skip the speaker->mic latency at the start
    if (++testCount_ < kToneFrames) return;

    double quietDb = 10 * std::log10(testQuiet_ / kQuietFrames / (32768.0 * 32768.0) + 1e-12);
    double loudDb = 10 * std::log10(testLoud_ / (kToneFrames - kSkipFrames) / (32768.0 * 32768.0) + 1e-12);
    char buf[200];
    bool ok = loudDb - quietDb >= 15 && loudDb > -60;
    snprintf(buf, sizeof(buf), "%s speaker->mic: tone heard at %.1f dBFS, quiet room %.1f dBFS (%.1f dB above)%s\n",
             ok ? "ok" : "FAIL", loudDb, quietDb, loudDb - quietDb, ok ? "" : " - check the speaker, amp and mics");
    testResult_ = testBoard_ + buf;
    bool allOk = testResult_.find("FAIL") == std::string::npos;
    testResult_ += allOk ? "SELF-TEST PASSED" : "SELF-TEST FAILED";
    testPhase_ = 0;
    logf("self-test:\n%s", testResult_.c_str());
    // A failed nightly check runs once more 30 s later before it counts: a noise in the room can spoil one run.
    const bool retry = !allOk && testAuto_ && !selfRetried_;
    selfRetried_ = retry;
    if (retry) {
        selfRetryAt_ = frame_ + 1500;
        return;
    }
    audioFault_ = !ok;
    if (!allOk)
        event(EventKind::Fault, unit_.room, "room %u self-check FAILED: %s", unsigned(unit_.room),
              ok ? "a board check (see: selftest result)" : "the microphone did not hear the speaker");
    else if (testAuto_)
        event(EventKind::Info, unit_.room, "room %u self-check passed", unsigned(unit_.room));
    sendStatus();
}

void Intercom::receive() {
    uint8_t buf[kMaxPacketBytes + 16];
        size_t n;
    while ((n = hal_.net->recv(buf, sizeof(buf))) > 0) {
        if (link_ == LinkType::None) continue;
        bool ok;
        PacketType type = peekType(buf, n, &ok);
        if (!ok) continue;
        if (type == PacketType::Audio) {
            if (cal_.measuring() || testPhase_ || couple_.active()) continue;  // the room must stay quiet while measuring
            AudioHeader h;
            const uint8_t* payload;
            size_t plen;
            if (!decodeAudioData(buf, n, &h, &payload, &plen) || !codecAvailable(h.codec)) continue;
            if (h.from == unit_) continue;                                               // our own multicast echo
            noteTalk(h);                                                                  // the call log covers every call on the network
            if (sameGroup(h.from) && !(h.flags & kAudioFromConsole)) {                     // same room group: heard acoustically
                // and while it talks, this speaker goes quiet: the talker's echo canceller only knows its own
                // speaker, so the room's other speakers would go back out through its microphone as echo.
                groupTalkUntil_ = frame_ + kGroupTalkHoldFrames;
                continue;
            }
            if (!isForMe(h)) continue;                                                     // a call for another room
            const bool fromOffice = (h.flags & kAudioFromConsole) || (office_ && h.from.room == actingOffice());
            if (dnd() && !fromOffice) continue;                                            // do not disturb: only the office gets through
            if (silentHelp() && (h.target != 0 || (h.flags & kAudioReply))) continue;      // silent help call: no call may sound in this room
            if (h.target == 0 && !(h.flags & kAudioReply) && (h.flags & kAudioFromConsole))
                allCallUntil_ = frame_ + kAllCallHoldFrames;
            if (h.target != 0 || (h.flags & kAudioReply)) {
                if (frame_ < allCallUntil_) continue;  // an office all-call is on: it wins
                if (!inCall()) {
                    char u[8];
                    formatUnit(h.from, u);
                    logf("call from %s", h.from.room ? u : "the office");
                    if (!canAnswer()) {  // a new call, not the next sentence of one
                        startSound(Sound::Chime);
                        // A call to this room alone: the call LEDs blink until TALK answers (a floor or group
                        // announcement doesn't ask for an answer, so no attention blink or missed call).
                        callAnswered_ = (h.flags & (kAudioFloor | kAudioGroupCall | kAudioZone)) != 0;
                        missedUntil_ = 0;
                        replied_ = closing_ = false;
                    }
                }
                callFrom_ = h.from;
                callLast_ = frame_;
                if (fromOffice && replied_) closing_ = true;  // the office has the room's answer and spoke last
                if (fromOffice) setCallingOffice(false, "the office answered");
            }
            if (Talker* t = talkerFor(h.from)) {
                t->jb.push(h.seq, h.codec, payload, plen, h.level);
                t->lastFrame = frame_;
            }
        } else if (type == PacketType::Status) {
            StatusPacket s;
            if (!decodeStatus(buf, n, &s)) continue;
            if (s.from == unit_) {  // our own multicast echo, or another unit set to our room number
                if (s.unitUid && opt_.unitUid && s.unitUid != opt_.unitUid) {
                    if (!duplicate() || s.unitUid != dupUid_)
                        event(EventKind::Fault, unit_.room, "DUPLICATE: unit %08lx also uses room %u", (unsigned long)s.unitUid, unsigned(unit_.room));
                    dupUntil_ = frame_ + kDupHoldFrames;
                    dupUid_ = s.unitUid;
                }
                continue;
            }
            Peer* p = nullptr;
            for (auto& q : peers_)
                if (q.status.from == s.from) p = &q;
            Peer* k = nullptr;
            for (auto& q : known_)
                if (q.status.from == s.from) k = &q;
            peerEvents(p ? &p->status : nullptr, s, k && k->offlineLogged);
            {   // the queue of rooms waiting for the office
                const bool wants = (s.flags & kStatusCalling) || (s.flags2 & kStatus2Silent);
                size_t w = 0;
                while (w < waiting_.size() && waiting_[w].room != s.from.room) w++;
                if (wants && w == waiting_.size() && waiting_.size() < 64) {
                    waiting_.push_back(Waiting{s.from.room, false, false, frame_});
                    // The office's units (the office unit, its stand-in, and units sharing the office's room) call for attention.
                    if (office_ && (isOffice(unit_.room) || group() == office_)) {
                        ofcAlertRoom_ = s.from.room;
                        ofcAlertUntil_ = frame_ + kOfficeRingFrames;
                        ofcAlertNext_ = frame_;
                        helpAlertNext_ = frame_ + kHelpAgainFrames;
                    }
                }
                if (w < waiting_.size()) {
                    if (!wants) {
                        waiting_.erase(waiting_.begin() + long(w));
                    } else {
                        waiting_[w].urgent = (s.flags & kStatusUrgent) != 0;
                        waiting_[w].silent = (s.flags2 & kStatus2Silent) != 0;
                    }
                }
            }
            if (!p) {
                peers_.push_back(Peer());
                p = &peers_.back();
            }
            p->status = s;
            p->lastFrame = frame_;
            p->offlineLogged = false;
            if (!k && known_.size() < 200) {
                known_.push_back(Peer());
                k = &known_.back();
            }
            if (k) *k = *p;
        } else if (type == PacketType::Reply) {  // answers to commands this unit sent (web console)
            TextPacket r;
            if (!decodeText(buf, n, &r) || r.targetRoom != unit_.room) continue;
            size_t a = 0;
            while (a < r.text.size()) {
                size_t e = r.text.find('\n', a);
                if (e == std::string::npos) e = r.text.size();
                if (e > a) replies_.push_back({++replyN_, r.from, r.text.substr(a, e - a)});
                a = e + 1;
            }
            if (replies_.size() > 200) replies_.erase(replies_.begin(), replies_.end() - 200);
        } else if (type == PacketType::Command) {
            // Signed with the network key (netkey), and never replayed.
            TextPacket t;
            if (!decodeText(buf, n, &t) || !t.addressedTo(unit_)) continue;
            std::string cmd;
            uint64_t nonce = 0;
            netauth::Check ck = netauth::verify(netauth::loadKey(*hal_.store), lastNonce_, t, &cmd, &nonce);
            if (ck != netauth::Check::Ok) {
                logf("net command from %u refused: %s", unsigned(t.from.room), netauth::describe(ck));
                sendReply(t, netauth::describe(ck));
                continue;
            }
            lastNonce_ = nonce;
            netauth::saveNonce(*hal_.store, nonce);
            std::string reply = command(cmd, CmdSource::Network);
            bool secret = cmd.compare(0, 7, "webpass") == 0 || cmd.compare(0, 6, "netkey") == 0;
            logf("net command from %u: %s", unsigned(t.from.room), secret ? "(password command, hidden)" : cmd.c_str());
            sendReply(t, reply);
        }
    }
    for (size_t i = 0; i < peers_.size();) {
        if (frame_ - peers_[i].lastFrame > uint32_t(kPeerTimeoutFrames)) {
            for (size_t w = 0; w < waiting_.size(); w++)
                if (waiting_[w].room == peers_[i].status.from.room) {
                    waiting_.erase(waiting_.begin() + long(w));
                    break;
                }
            // With our own network down every room falls silent: that is this unit's fault, already logged.
            if (link_ != LinkType::None) {
                const uint16_t room = peers_[i].status.from.room;
                event(EventKind::Fault, room, "room %u offline", unsigned(room));
                for (auto& q : known_)
                    if (q.status.from == peers_[i].status.from) q.offlineLogged = true;
            }
            peers_.erase(peers_.begin() + long(i));
        } else {
            i++;
        }
    }
    for (size_t i = 0; i < heardCalls_.size();) {  // a transmission ended (1 s without a packet): one line in the call log
        const HeardCall& c = heardCalls_[i];
        if (frame_ - c.last <= 50) {
            i++;
            continue;
        }
        event(EventKind::Call, c.from.room, "%s to %s, %u s", talkerText(c.flags, c.from.room).c_str(),
              targetText(c.flags, c.target).c_str(), unsigned((c.last - c.first) / 50 + 1));
        heardCalls_.erase(heardCalls_.begin() + long(i));
    }
}

void Intercom::noteTalk(const AudioHeader& h) {
    for (auto& c : heardCalls_)
        if (c.from == h.from && c.flags == h.flags && c.target == h.target) {
            c.last = frame_;
            return;
        }
    if (heardCalls_.size() < 16) heardCalls_.push_back(HeardCall{h.from, h.target, h.flags, frame_, frame_});
}

// What changed at another unit since its last status packet goes into the event log.
void Intercom::peerEvents(const StatusPacket* old, const StatusPacket& s, bool backOnline) {
    const unsigned r = s.from.room;
    static const char* faults[] = {"", "E1 no Ethernet (on Wi-Fi)", "E2 no network", "Lo battery low", "dUP duplicate room number",
                                   "oL an output is shorted"};
    if (!old) {  // first heard since this unit started says nothing; after an "offline" line it is news
        if (backOnline) event(EventKind::Info, s.from.room, "room %u back online%s", r, s.uptimeS < 60 ? " (it restarted)" : "");
        return;
    }
    if (s.uptimeS + 5 < old->uptimeS) event(EventKind::Fault, s.from.room, "room %u restarted", r);
    if (s.fault != old->fault) {
        if (s.fault != Fault::None) event(EventKind::Fault, s.from.room, "room %u: %s", r, faults[int(s.fault)]);
        else event(EventKind::Info, s.from.room, "room %u: fault gone", r);
    }
    if (s.power != old->power) {
        if (s.power == PowerSource::Battery) event(EventKind::Fault, s.from.room, "room %u on battery (%u %%)", r, unsigned(s.battery));
        else event(EventKind::Info, s.from.room, "room %u back on PoE", r);
    }
    const uint8_t up = uint8_t(s.flags & ~old->flags);
    if (up & kStatusUrgent) event(EventKind::Fault, s.from.room, "room %u: HELP NEEDED", r);
    else if (up & kStatusCalling) event(EventKind::Call, s.from.room, "room %u is calling the office", r);
    const uint8_t up2 = uint8_t(s.flags2 & ~old->flags2);
    if (up2 & kStatus2Silent) event(EventKind::Fault, s.from.room, "room %u: SILENT HELP CALL", r);
    if (up2 & kStatus2Dnd) event(EventKind::Info, s.from.room, "room %u: do not disturb", r);
    if (up & kStatusAudioFault) event(EventKind::Fault, s.from.room, "room %u: self-check failed (speaker or microphone)", r);
    if (up & kStatusTalkHeld) event(EventKind::Fault, s.from.room, "room %u: TALK held over 2 minutes (stuck button?)", r);
    if (s.emergency != old->emergency && s.emergency)
        event(EventKind::Fault, s.from.room, "room %u shows EMERGENCY %s", r, kEmergencyNames[s.emergency & 3]);
}

void Intercom::sendStatus() {
    if (link_ == LinkType::None) return;
    refreshPower();
    const PowerState& pw = power_;
    StatusPacket s;
    s.from = unit_;
    s.link = link_;
    s.power = pw.source;
    s.battery = pw.hasBattery ? pw.batteryPct : 0;
    s.flags = statusFlags();
    s.flags2 = statusFlags2();
    s.fwVer = packVersion(kFirmwareVersion);
    s.emergency = uint8_t(emerg_);
    s.fault = faultCode();
    s.uptimeS = frame_ * kFrameMs / 1000;
    s.unitUid = opt_.unitUid;
    s.group = group_;
    s.syncRev = syncRev_;
    s.syncHash = syncHash_;
    uint8_t buf[32];
    hal_.net->send(buf, encodeStatus(s, buf));
}

uint8_t Intercom::statusFlags() const {
    return uint8_t((transmitting_ ? kStatusTalking : 0) | (muted() ? kStatusMuted : 0) | (talkHeld() ? kStatusTalkHeld : 0) |
                   (callingOffice_ ? kStatusCalling : 0) | (urgent() ? kStatusUrgent : 0) | (emerg_ != Emergency::None ? kStatusEmergency : 0) |
                   (audioFault_ ? kStatusAudioFault : 0));
}

void Intercom::updatePanel() {
    if (frame_ % kStatusEveryFrames == 1) refreshPower();  // once a second, whatever the link
    char text[3];
    uint8_t dots = 0;
    uint8_t testLamps[int(Lamp::Count)];
    uint32_t testRgb;
    if (bootT0_ && bootTestStep(text, &dots, testLamps, &testRgb) < 0) bootT0_ = 0;  // test over
    if (bootT0_) {
        // power-up light test: text and dots already set
    } else if (setup_.active()) {
        setup_.render(frame_, text, &dots);
    } else if (cal_.active()) {  // "c42" = auto-tune 42 % done
        int pct = std::min(99, cal_.progressPct());
        text[0] = 'c';
        text[1] = char('0' + pct / 10);
        text[2] = char('0' + pct % 10);
    } else if (frame_ < showResultUntil_) {  // "e17" = 17 dB echo reduction, "err" = failed
        const CalResult& r = cal_.result();
        if (r.ok) {
            int e = std::max(0, std::min(99, int(r.erleAfterDb + 0.5f)));
            text[0] = 'e';
            text[1] = char('0' + e / 10);
            text[2] = char('0' + e % 10);
        } else {
            text[0] = 'e';
            text[1] = text[2] = 'r';
        }
    } else {
        unsigned r = unit_.room % 1000;
        text[0] = r >= 100 ? char('0' + r / 100) : ' ';
        text[1] = r >= 10 ? char('0' + (r / 10) % 10) : ' ';
        text[2] = char('0' + r % 10);
        if (r == 0) text[0] = text[1] = text[2] = '-';  // no room number set yet
        if (canAnswer() && !transmitting_) {  // a call for this room: CAL (TALK answers the caller), flashing until answered
            std::memcpy(text, (callAnswered_ || (frame_ / 30) % 2 == 0) ? "CAL" : "   ", 3);
        } else if (missedCall() && (frame_ / 100) % 2) {  // missed call: CAL alternates with the room number
            std::memcpy(text, "CAL", 3);
        } else if (callingOffice_ && !transmitting_ && (frame_ / 50) % 2) {  // calling the office: oFC / number
            std::memcpy(text, "oFC", 3);
            if (urgent_) {  // help needed: HLP
                text[0] = char(0x80 | 0x76);
                text[1] = 'L';
                text[2] = 'P';
            }
        } else if (dnd() && (frame_ / 100) % 2) {  // do not disturb: dnd / number, 2 s each
            std::memcpy(text, "dnd", 3);
        }
        // The acting office unit, with rooms waiting: the first one's number every other second, third dot lit.
        if (feature(kFeatQueue) && office_ && !waiting_.empty() && !canAnswer() && (frame_ / 50) % 2 && actingOffice() == unit_.room) {
            const unsigned q = callQueue()[0].room % 1000;
            text[0] = q >= 100 ? char('0' + q / 100) : ' ';
            text[1] = q >= 10 ? char('0' + (q / 10) % 10) : ' ';
            text[2] = char('0' + q % 10);
            dots |= 0x04;
        }
        if (ofcAlertUntil_ && !transmitting_ && !canAnswer()) {  // an office unit: the calling room's number, blinking fast
            const unsigned q = ofcAlertRoom_ % 1000;
            const bool on = (frame_ / 15) % 2 == 0;
            text[0] = on && q >= 100 ? char('0' + q / 100) : ' ';
            text[1] = on && q >= 10 ? char('0' + (q / 10) % 10) : ' ';
            text[2] = on ? char('0' + q % 10) : ' ';
        }
        if (emerg_ != Emergency::None && (frame_ / 50) % 2 == 0) {  // LoC / EUA / SHE, a second each with the room number
            static const char kText[4][3] = {{' ', ' ', ' '}, {'L', 'o', 'C'}, {'E', 'U', 'A'}, {char(0x80 | 0x6d), char(0x80 | 0x76), 'E'}};
            std::memcpy(text, kText[int(emerg_)], 3);
        }
        if (transmitting_) dots |= 0x01;
        if (playing_) dots |= 0x02;
        if (silentHelp()) dots = 0x04;  // the only sign in the room that the silent call went out: the third dot
        // The room number stays the main thing shown. A duplicate number alternates with "dUP" every
        // second; other faults flash their code for 0.5 s every 3.5 s.
        const char* f = fault();
        bool showCode = duplicate() ? ((frame_ / 50) % 2 != 0) : (*f && frame_ % kFaultCycle >= kFaultCycle - kFaultShow);
        if (*f && showCode) {
            std::memcpy(text, f, 3);
            dots = 0;
        }
    }
    if (std::memcmp(text, shown_, 3) != 0 || dots != shownDots_) {  // the display sits behind I2C: write only on change
        std::memcpy(shown_, text, 3);
        shownDots_ = dots;
        hal_.panel->show(text, dots);
    }
}

const char* Intercom::fault() const {
    if (duplicate()) return "dUP";
    if (link_ == LinkType::None) return "E2 ";                        // no network at all
    if (outFaults_) return "oL ";                                     // an output (lamp, MISC, servos) shorted / overloaded
    if (power_.source == PowerSource::PoE && !ethUp_) return "E1 ";  // PoE power but no Ethernet link
    if (power_.source == PowerSource::Battery && power_.hasBattery && power_.batteryPct < kLowBatteryPct) return "Lo ";
    return "";
}

Fault Intercom::faultCode() const {
    const char* f = fault();
    if (f[0] == 'd') return Fault::Duplicate;
    if (f[0] == 'E') return f[1] == '1' ? Fault::NoEthernet : Fault::NoNetwork;
    if (f[0] == 'L') return Fault::LowBattery;
    if (f[0] == 'o') return Fault::Overload;
    return Fault::None;
}

bool Intercom::isForMe(const AudioHeader& h) const {
    if (h.flags & kAudioReply) return h.target != 0 && h.target == unit_.room;  // 0 = an answer to the office console
    if (h.target == 0) return true;
    if (h.flags & kAudioFloor) return unit_.room / 100 == h.target;
    if (h.flags & kAudioZone) return feature(kFeatZones) && inZone(h.target, unit_.room);
    if (h.flags & kAudioGroupCall) return group() == h.target || unit_.room == h.target;
    return h.target == unit_.room;
}

int64_t Intercom::utcNow() const {
    int64_t t = opt_.utcNow ? opt_.utcNow() : 0;
    if (t > 1600000000LL) return t;
    if (setEpoch_) return setEpoch_ + int64_t(frame_ - setFrame_) * kFrameMs / 1000;
    return 0;
}

void Intercom::startSound(Sound s) {
    sound_ = s;
    soundPos_ = 0;
    soundActive_ = true;
}

void Intercom::saveSchedule() { hal_.store->saveText(kSchedKey, sched_.toString()); }

std::string Intercom::schedCommand(const std::vector<std::string>& a) {
    const std::string& c = a[0];
    std::string sub = a.size() > 1 ? a[1] : "";
    if (c == "skip") {
        if (sub == "off") sched_.skipDay = 0;
        else if (sub == "today") {
            int64_t now = utcNow();
            if (!now) return "no clock yet";
            sched_.skipDay = sched_.local(now).ymd();
        } else {
            return "usage: skip today | skip off";
        }
        saveSchedule();
        return sched_.skipDay ? "no timed events today" : "timed events back on";
    }
    if (c == "dayoff") {
        if (sub.empty()) return sched_.list();
        uint32_t d;
        if ((sub != "add" && sub != "del") || a.size() < 3 || !Schedule::parseDate(a[2], &d)) return "usage: dayoff add|del 2026-12-24";
        auto it = std::find(sched_.daysOff.begin(), sched_.daysOff.end(), d);
        if (sub == "add" && it == sched_.daysOff.end()) {
            if (sched_.daysOff.size() >= Schedule::kMaxDaysOff) return "too many days off (60)";
            sched_.daysOff.push_back(d);
            std::sort(sched_.daysOff.begin(), sched_.daysOff.end());
        }
        if (sub == "del" && it != sched_.daysOff.end()) sched_.daysOff.erase(it);
        saveSchedule();
        return "ok";
    }
    // sched
    if (sub.empty() || sub == "list") return sched_.list();
    if (sub == "add") {
        ScheduleEntry e;
        std::string err = Schedule::parseEntry(a, 2, &e);
        if (err.empty()) err = sched_.add(e);
        if (!err.empty()) return err;
        saveSchedule();
        return "added: " + Schedule::formatEntry(e);
    }
    if (sub == "del") {
        unsigned long n = a.size() > 2 ? std::strtoul(a[2].c_str(), nullptr, 10) : 0;
        if (n < 1 || n > sched_.entries.size()) return "usage: sched del N (the number from \"sched\")";
        std::string gone = Schedule::formatEntry(sched_.entries[n - 1]);
        sched_.entries.erase(sched_.entries.begin() + long(n - 1));
        saveSchedule();
        return "deleted: " + gone;
    }
    if (sub == "clear") {
        sched_.entries.clear();
        saveSchedule();
        return "no timed events";
    }
    if (sub == "set") {  // the console pushes the whole table at once (as "sched get" prints it)
        std::string s;
        for (size_t i = 2; i < a.size(); i++) s += (i > 2 ? " " : "") + a[i];
        Schedule n;
        if (!n.fromString(s)) return "bad schedule";
        sched_ = n;
        saveSchedule();
        return "ok, " + std::to_string(sched_.entries.size()) + " timed events";
    }
    if (sub == "get") return sched_.toString();
    return "usage: sched [list | add HH:MM DAYS SOUND [room N|floor N|group N] | del N | clear | get | set ...]";
}

void Intercom::checkSchedule() {
    int64_t now = utcNow();
    if (!now) return;
    int64_t minute = now / 60;
    if (minute == lastSchedMinute_) return;
    bool first = lastSchedMinute_ == 0;
    lastSchedMinute_ = minute;
    if (emerg_ != Emergency::None || dnd()) return;  // no bells during an emergency or do-not-disturb
    LocalTime lt = sched_.local(now);
    if (first && lt.second > 20) return;  // just started mid-minute: don't play an event late
    if (sched_.isDayOff(lt)) return;
    for (const ScheduleEntry& e : sched_.entries) {
        if (e.minute != lt.hour * 60 + lt.minute || !(e.days & (1u << lt.wday)) || !sched_.appliesTo(e, unit_.room, group())) continue;
        logf("timed event %s", Schedule::formatEntry(e).c_str());
        pendingSound_ = true;
        pending_ = e.sound;
        pendingSince_ = frame_;
        break;  // one sound a minute
    }
}

// Attention lights for a call to this room: the white lamp, the RGB LED (blue) and the misc output J11
// (an extra light can be wired there) blink together, 0.6 s on / 0.6 s off, until TALK answers. Once
// answered only the RGB stays on (steady) while the conversation is open. A missed call: a short blink
// of all three every 2 s until a button is pressed. J11 is steady during an office all-call. IR stays
// off unless set by hand (R3: night vision).
void Intercom::updateLamps() {
    uint8_t want[int(Lamp::Count)] = {0, 0, 0};
    uint32_t rgb = 0;
    char t[3];
    uint8_t d;
    if (bootT0_ && bootTestStep(t, &d, want, &rgb) >= 0) {
        // power-up light test drives the lamps and the RGB LED
    } else if (emerg_ != Emergency::None) {  // emergency: the alarm light (J11) flashes fast, the RGB LED is red
        want[int(Lamp::Other)] = (frame_ / 12) % 2 ? 100 : 0;
        rgb = kEmergencyRgb;
    } else if (ofcAlertUntil_ && !transmitting_ && !canAnswer()) {  // an office unit: a room is calling, fast flash
        bool on = (frame_ / 15) % 2 == 0;
        want[int(Lamp::Other)] = want[int(Lamp::White)] = on ? 100 : 0;
        rgb = on ? kCallRgb : 0;
    } else if (canAnswer() && !transmitting_) {
        bool on = (frame_ / 30) % 2 == 0 && !callAnswered_;
        want[int(Lamp::Other)] = want[int(Lamp::White)] = on ? 100 : 0;
        rgb = (on || callAnswered_) ? kCallRgb : 0;
    } else if (frame_ < allCallUntil_) {
        want[int(Lamp::Other)] = 100;
    } else if (missedCall()) {  // nobody answered: a short blink every 2 s until a button is pressed
        bool on = frame_ % 100 < 12;
        want[int(Lamp::Other)] = want[int(Lamp::White)] = on ? 100 : 0;
        rgb = on ? kCallRgb : 0;
    }
    for (int i = 0; i < int(Lamp::Count); i++) {
        uint8_t v = lampManual_[i] == 255 ? want[i] : lampManual_[i];
        if (v == lamp_[i]) continue;
        lamp_[i] = v;
        if (hal_.lamps) hal_.lamps->set(Lamp(i), v);
    }
    if (rgb != rgb_) {
        rgb_ = rgb;
        if (hal_.lamps) hal_.lamps->setRgb(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb));
    }
    // Camera shutter: out as a flag in an emergency (fast), open but hidden in a silent help call (slow and quiet),
    // closed otherwise (slow). The first frame sends the closed position, wherever the paddle was left.
    const Shutter sh = shutterManual_ >= 0 ? Shutter(shutterManual_) : emerg_ != Emergency::None ? Shutter::Flag : silentHelp() ? Shutter::Open : Shutter::Closed;
    if (sh != shutter_ || !shutterSent_) {
        if (shutterSent_) event(EventKind::Info, unit_.room, "room %u: camera shutter %s", unsigned(unit_.room), sh == Shutter::Flag ? "open, flag out" : sh == Shutter::Open ? "open" : "closed");
        shutter_ = sh;
        shutterSent_ = true;
        if (hal_.lamps) hal_.lamps->setShutter(shutterUs_[int(sh)], sh != Shutter::Flag);
    }
}

// Power-up light test, so a dead segment, lamp or LED shows at a glance:
//   segments a..g one at a time on all three digits, then the dots   (8 x 0.16 s)
//   everything on, "8.8.8."                                           (0.5 s)
//   L1 white lamp, L2 IR lamp, L3 call light (J11)                     (0.4 s each)
//   L4 / L5 / L6: RGB LED red, green, blue                             (0.3 s each)
// MISC outputs (one may carry the laser) and servo power are left out on purpose.
// Returns the step number, or -1 when the test is over.
int Intercom::bootTestStep(char text[3], uint8_t* dots, uint8_t lamps[], uint32_t* rgb) const {
    if (!bootT0_ || frame_ < bootT0_) return -1;
    uint32_t t = frame_ - bootT0_;
    text[0] = text[1] = text[2] = ' ';
    *dots = 0;
    *rgb = 0;
    for (int i = 0; i < int(Lamp::Count); i++) lamps[i] = 0;
    if (t < 64) {  // one segment at a time
        uint32_t s = t / 8;
        if (s < 7) text[0] = text[1] = text[2] = char(0x80 | (1u << s));
        else *dots = 0x07;
        return int(s);
    }
    if (t < 89) {
        text[0] = text[1] = text[2] = char(0x80 | 0x7f);
        *dots = 0x07;
        return 8;
    }
    if (t < 149) {
        int n = int((t - 89) / 20);  // 0 white, 1 IR, 2 other
        text[0] = 'L';
        text[1] = char('1' + n);
        lamps[n] = 100;
        return 9 + n;
    }
    if (t < 194) {
        int n = int((t - 149) / 15);
        static const uint32_t colours[] = {0xff0000, 0x00ff00, 0x0000ff};
        text[0] = 'L';
        text[1] = char('4' + n);
        *rgb = colours[n];
        return 12 + n;
    }
    return -1;
}

bool Intercom::inCall() const { return callLast_ && frame_ - callLast_ < kCallFrames; }
bool Intercom::canAnswer() const { return callLast_ && frame_ - callLast_ < kAnswerFrames; }

void Intercom::setCallingOffice(bool on, const char* why) {
    if (on == callingOffice_) return;
    callingOffice_ = on;
    callingSince_ = frame_;
    ofcRingUntil_ = on ? frame_ + kOfficeRingFrames : 0;
    ofcRingNext_ = frame_;
    if (!on) urgent_ = false;
    if (on && urgent_) event(EventKind::Fault, unit_.room, "room %u: HELP NEEDED (%s)", unsigned(unit_.room), why);
    else if (on) event(EventKind::Call, unit_.room, "room %u is calling the office (%s)", unsigned(unit_.room), why);
    else logf("call to the office ended (%s)", why);
    sendStatus();
}

size_t Intercom::webTalk(const int16_t* pcm, size_t samples) {
    const size_t kMax = kSampleRate;  // 1 s: more means the page sends faster than real time; drop the oldest
    webBuf_.insert(webBuf_.end(), pcm, pcm + samples);
    if (webBuf_.size() > kMax) webBuf_.erase(webBuf_.begin(), webBuf_.end() - long(kMax));
    webLast_ = frame_;
    if (webState_ == 0) {
        webState_ = 1;
        logf("web console talk started");
    }
    return webBuf_.size();
}

float Intercom::coupling(uint16_t room) const {
    // Louder of the doors-closed and doors-open results: doors are often left open.
    float best = -99;
    for (const auto* m : {&couplings_, &couplingsOpen_})
        for (const auto& r : *m)
            if (r.room == room && r.heard) best = std::max(best, r.db);
    return best;
}

void Intercom::finishCouple() {
    logf("%s%s", couple_.summary().c_str(), coupleOpen_ ? " (doors open)" : "");
    if (!couple_.ok()) return;  // keep the old map
    if (coupleOpen_) {  // doors open: kept apart, used for ducking only (room groups come from doors closed)
        couplingsOpen_ = couple_.results();
        hal_.store->saveText("couple_open_map", CouplingTest::toString(couplingsOpen_));
        return;
    }
    couplings_ = couple_.results();
    hal_.store->saveText("couple_map", CouplingTest::toString(couplings_));
    if (couple_.autoGroup()) {
        // Units that hear each other about as loud as their own speaker are one room (the gym):
        // the group is the highest room number among them, which every member works out the same way.
        uint16_t g = unit_.room;
        bool shared = false;
        for (const auto& r : couplings_)
            if (r.heard && r.db >= CouplingTest::kSameRoomDb) {
                shared = true;
                g = std::max(g, r.room);
            }
        // group_ 0 means "my own room number": the unit with the highest number keeps 0, the others point at it.
        uint16_t want = (shared && g != unit_.room) ? g : 0;
        if (want != group_) {
            group_ = want;
            hal_.store->saveText(kGroupKey, group_ ? std::to_string(group_) : "");
            logf("room group set from the coupling test: %u", unsigned(group()));
            sendStatus();
        }
    }
}

bool Intercom::sameGroup(const UnitId& from) const {
    if (from.room == unit_.room) return true;
    uint16_t theirs = from.room;
    for (const auto& p : peers_)
        if (p.status.from == from && p.status.group) theirs = p.status.group;
    return theirs == group();
}

void Intercom::refreshPower() {
    power_ = hal_.power->read();
    const bool low = power_.source == PowerSource::Battery && power_.hasBattery && power_.batteryPct < kLowBatteryPct;
    if (powerKnown_ && power_.source != lastSource_) {
        if (power_.source == PowerSource::Battery)
            event(EventKind::Fault, unit_.room, "room %u on battery (%u %%)", unsigned(unit_.room), unsigned(power_.batteryPct));
        else event(EventKind::Info, unit_.room, "room %u back on PoE", unsigned(unit_.room));
    }
    if (powerKnown_ && low && !wasLow_) event(EventKind::Fault, unit_.room, "room %u battery low (%u %%)", unsigned(unit_.room), unsigned(power_.batteryPct));
    powerKnown_ = true;
    lastSource_ = power_.source;
    wasLow_ = low;
    uint8_t of = hal_.diag ? hal_.diag->outputFaults() : 0;
    if (of != outFaults_) {
        static const char* names[] = {"white lamp (J13)", "IR lamp (J12)", "call light (J11)", "MISC1 (J17)",
                                      "MISC2 (J18)",      "MISC3 (J19)",   "servo power"};
        for (int i = 0; i < 7; i++)
            if ((of & ~outFaults_) & (1u << i))
                event(EventKind::Fault, unit_.room, "room %u OUTPUT OVERLOAD: %s shorted or overloaded, switched off (check its cable)",
                      unsigned(unit_.room), names[i]);
        outFaults_ = of;
    }
}

AudioStats Intercom::audioStats() const {
    AudioStats s = retired_;
    for (const auto& t : talkers_) {
        s.concealed += t->jb.concealed();
        s.lateDrops += t->jb.lateDrops();
    }
    return s;
}

// ---- emergency messages (R3.1) -------------------------------------------------------------
// The signal sounds for a while (1 min; evacuate 5 min), then the room is left quiet; the display and the
// alarm light stay on until "emergency clear". A live voice (the office) always comes before the signal.
void Intercom::setEmergency(Emergency e, const char* why) {
    if (e == emerg_) return;
    if (e == Emergency::None) {
        event(EventKind::Info, unit_.room, "room %u: emergency cleared (%s)", unsigned(unit_.room), why);
        emergMine_ = false;
        startSound(Sound::AllClear);
    } else {
        event(EventKind::Fault, unit_.room, "room %u: EMERGENCY %s (%s)", unsigned(unit_.room), kEmergencyNames[int(e)], why);
        emergSince_ = frame_;
        emergPos_ = 0;
        emergSoundUntil_ = frame_ + (e == Emergency::Evacuate ? kEvacuateSoundFrames : kEmergencySoundFrames);
        pendingSound_ = false;
        if (soundActive_ && sound_ != Sound::Chime) soundActive_ = false;
    }
    emerg_ = e;
    sendStatus();
}

void Intercom::noteCameraView() {
    if (camNotedAt_ && frame_ - camNotedAt_ < 3000) return;
    camNotedAt_ = frame_ ? frame_ : 1;
    event(EventKind::Info, unit_.room, "room %u: camera viewed from the web page", unsigned(unit_.room));
}

// ---- settings backup (R3.1) ----------------------------------------------------------------
namespace {
const char* const kBackupKeys[] = {"group", "tune", "sched", "ntp", "boottest", "selfcheck", "conwatch", "couple_map", "couple_open_map",
                                   "features", "office", "zones", "quiet"};
}

std::string Intercom::backupText() const {
    std::string out = "# intercom settings, room " + std::to_string(unit_.room) + ", firmware " + kFirmwareVersion +
                      "\n# passwords and keys are not in this file: set them over USB\n";
    out += "unit=" + std::to_string(unit_.room) + "\n";
    for (const char* k : kBackupKeys) {
        std::string v = hal_.store->loadText(k);
        if (!v.empty() && v.find('\n') == std::string::npos) out += std::string(k) + "=" + v + "\n";
    }
    return out;
}

std::string Intercom::restoreLine(const std::string& lineIn) {
    size_t b = lineIn.find_first_not_of(" \t\r\n"), e = lineIn.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    std::string line = lineIn.substr(b, e - b + 1);
    if (line[0] == '#') return "";
    size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0) return "not a key=value line: " + line.substr(0, 40);
    const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (v.size() > 3500 || v.find('\n') != std::string::npos) return k + ": value too long";
    if (k == "unit") {
        uint16_t room;
        uint8_t sub;
        if (!parseTarget(v, &room, &sub) || room == kTargetAllRooms || sub != kTargetAnySub || room == 0) return "unit: not a room number";
        UnitId id;
        id.room = room;
        setUnit(id);
        return "";
    }
    if (k == "group") {
        unsigned long g = std::strtoul(v.c_str(), nullptr, 10);
        if (g > 999) return "group: not a room number";
        group_ = uint16_t(g == unit_.room ? 0 : g);
        hal_.store->saveText(kGroupKey, group_ ? std::to_string(group_) : "");
        sendStatus();
        return "";
    }
    if (k == "tune") {
        TuneParams p;
        p.fromString(v);
        params_ = p;
        applyParams();  // limits the filter length to what this unit can run
        hal_.store->saveText(kTuneKey, params_.toString());
        return "";
    }
    if (k == "sched") {
        Schedule n;
        if (!n.fromString(v)) return "sched: not a schedule";
        sched_ = n;
        saveSchedule();
        return "";
    }
    if (k == "couple_map" || k == "couple_open_map") {
        auto m = CouplingTest::fromString(v);
        hal_.store->saveText(k.c_str(), CouplingTest::toString(m));
        (k == "couple_map" ? couplings_ : couplingsOpen_) = m;
        return "";
    }
    if (k == "conwatch") {
        const int n = std::atoi(v.c_str());
        if (n < 1 || n > 60) return "conwatch: not 1 to 60 minutes";
        hal_.store->saveText("conwatch", std::to_string(n));
        loadSelfCheck();
        return "";
    }
    if (k == "selfcheck") {
        unsigned h = 0, m = 0;
        if (std::sscanf(v.c_str(), "%u:%u", &h, &m) != 2 || h > 23 || m > 59) return "selfcheck: not a time";
        hal_.store->saveText("selfcheck", v);
        loadSelfCheck();
        return "";
    }
    if (k == "features" || k == "office" || k == "zones" || k == "quiet") {
        for (char ch : v)
            if (ch < ' ' || ch > '~') return k + ": bad character";
        hal_.store->saveText(k.c_str(), v);
        loadFeatures();
        return "";
    }
    if (k == "ntp" || k == "boottest") {
        for (char ch : v)
            if (ch <= ' ' || ch > '~') return k + ": bad character";
        hal_.store->saveText(k.c_str(), v);
        return "";
    }
    return "unknown setting: " + k.substr(0, 40);
}

// ---- the frame -------------------------------------------------------------------------
void Intercom::processFrame(const int16_t* mic, int16_t* speaker) {
    frame_++;
    if (restartAt_ && frame_ >= restartAt_) {
        restartAt_ = 0;
        opt_.restart(restartDownload_);  // on the board this does not return
    }
    // Fall-back (R3.1): firmware from an update is on trial until it has run for a minute with a network
    // link. A crash or a hang before that makes the bootloader start the old firmware again; 5 minutes
    // without a link and the unit goes back by itself (it could not be reached to fix it otherwise).
    if (hal_.fw && !fwChecked_) {
        if (frame_ >= kFwConfirmFrames && link_ != LinkType::None) {
            fwChecked_ = true;
            if (hal_.fw->pending()) event(EventKind::Info, unit_.room, "room %u: new firmware %s confirmed", unsigned(unit_.room), kFirmwareVersion);
            hal_.fw->confirm();
        } else if (frame_ >= kFwGiveUpFrames) {
            fwChecked_ = true;
            if (hal_.fw->pending()) {
                logf("new firmware %s: no network in 5 minutes, going back to the old firmware", kFirmwareVersion);
                hal_.fw->reject();  // on the board this does not return
            }
        }
    }
    if (emerg_ != Emergency::None && frame_ - emergSince_ > kEmergencyMaxFrames) setEmergency(Emergency::None, "nobody cleared it in 4 hours");
    // "emergency ... all": the first ten go out 2 s apart, so one lost packet can't leave a room out, then
    // one every 30 s for units that start later. It stops when this unit's state changes; "clear" after 5 min.
    if (emergTxKind_ >= 0 && frame_ >= emergTxNext_) {
        if (int(emerg_) != emergTxKind_ || (emergTxKind_ == 0 && emergTxCount_ >= 20)) {
            emergTxKind_ = -1;
        } else {
            sendSigned("all", std::string("emergency ") + kEmergencyNames[emergTxKind_]);
            emergTxNext_ = frame_ + (++emergTxCount_ < 10 ? 100u : 1500u);
        }
    }
    micLevel_ = frameLevel(mic, kFrameSamples);
    uint8_t held = hal_.panel->buttons();
    const uint8_t talkBit = 1u << kBtnTalk;
    const uint8_t combo = (1u << kBtnMode) | (1u << kBtnUp);

    // Auto-tune from the panel: MODE + UP held 3 s, or UP alone held 5 s. (Both work on boards
    // from 75d75fb, where the buttons are on their own expander; the older TM1637 key scan saw one key.)
    const bool comboHeld = (held & combo) == combo;
    const bool upHeld = held == (1u << kBtnUp);
    if ((comboHeld || upHeld) && !setup_.active()) {
        if (!comboSince_) comboSince_ = frame_;
        else if (frame_ - comboSince_ == (comboHeld ? kComboFrames : kUpHoldFrames)) startCal("panel");
    } else {
        comboSince_ = 0;
    }

    if (cal_.analyzing() && cal_.poll()) finishCal();
    if (testPhase_) {
        selfTestFrame(mic, speaker);
        spkLevel_ = frameLevel(speaker, kFrameSamples);
        transmitting_ = false;
        sentLevel_ = playing_ = 0;
        aec_.playback(speaker);
        updateLink();
        receive();
        if (frame_ % kStatusEveryFrames == 0) sendStatus();
        updatePanel();
        return;
    }
    if (cal_.measuring()) {
        if (held & talkBit) {
            cal_.abort();
            logf("auto-tune cancelled (TALK pressed)");
        } else {
            transmitting_ = false;
            sentLevel_ = playing_ = 0;
            if (cal_.frame(mic, speaker)) finishCal();
            spkLevel_ = frameLevel(speaker, kFrameSamples);
            updateLink();
            receive();
            if (frame_ % kStatusEveryFrames == 0) sendStatus();
            updatePanel();
            return;
        }
    }

    if (couple_.active()) {  // coupling test: nothing else plays or sends while the sweeps run
        transmitting_ = false;
        sentLevel_ = playing_ = 0;
        std::memset(speaker, 0, sizeof(int16_t) * kFrameSamples);
        if (couple_.frame(frame_, mic, speaker, kFrameSamples)) finishCouple();
        spkLevel_ = frameLevel(speaker, kFrameSamples);
        aec_.playback(speaker);
        updateLink();
        receive();
        if (frame_ % kStatusEveryFrames == 0) sendStatus();
        updatePanel();
        return;
    }

    int16_t clean[kFrameSamples];
    aec_.capture(mic, clean);

    // Panel: room-number setup and TALK. (Hidden SETUP held 3 s = setup; MODE + UP or UP held = auto-tune.)
    UnitId id = unit_;
    uint8_t setupHeld = comboSince_ ? uint8_t(held & ~(1u << kBtnMode)) : held;
    if (setup_.update(setupHeld, frame_, &id) && id != unit_) setUnit(id);
    if (talkFrames_ && --talkFrames_ == 0) talkTarget_ = TalkTarget();  // "talk ... room N" is for that once
    if (muteUntil_ && frame_ >= muteUntil_) {
        muteUntil_ = 0;
        logf("mute ended");
        sendStatus();
    }
    // Web console talk: start once ~120 ms is buffered (the page posts every ~100 ms), stop 0.3 s after
    // the audio stops coming and the buffer is empty.
    if (webState_ == 1 && webBuf_.size() >= size_t(6 * kFrameSamples)) webState_ = 2;
    if (webState_ && frame_ - webLast_ > 15 && webBuf_.size() < size_t(kFrameSamples)) {
        webState_ = 0;
        webBuf_.clear();
        logf("web console talk ended");
    }
    const bool web = webState_ == 2;
    // Silent help call: this room's microphone goes to the acting office unit, as an answer only it plays.
    const bool silentTx = silentTxUntil_ && frame_ < silentTxUntil_ && actingOffice() != 0 && actingOffice() != unit_.room;
    if (!(held & talkBit)) talkBtnSince_ = 0;
    else if (!talkBtnSince_) talkBtnSince_ = frame_ ? frame_ : 1;
    const bool talkBtn = talkBtnSince_ && frame_ - talkBtnSince_ >= kPressDelayFrames;
    bool talk = ((talkBtn || talkFrames_) && !setup_.active() && !muted()) || web || silentTx;
    // OFFICE (front): held while a call or ring waits = answer it (as TALK). Otherwise one press calls the office (it
    // rings 10 s); a second press while it rings, or holding it 1.5 s, makes it a call for help, which stays.
    // TALK and OFFICE held together 1 s (feature "silent"): silent help call. Until both are let go neither button
    // does its own job, and each waits 0.16 s before it acts, so the two fingers need not land together.
    const uint8_t bothBits = talkBit | (1u << kBtnOffice);
    if ((held & bothBits) == bothBits && !setup_.active()) {
        bothLatch_ = true;
        if (!bothSince_) bothSince_ = frame_ ? frame_ : 1;
        if (frame_ - bothSince_ == kSilentBothFrames && feature(kFeatSilent) && !silentHelp()) setSilent(true, "TALK and OFFICE held");
    } else {
        bothSince_ = 0;
        if (!(held & bothBits)) bothLatch_ = false;
    }
    if (bothLatch_) talk = web || silentTx;
    // The red button (feature "lockdown"): lockdown on every unit at once. Only the office page ends it.
    const bool redBtn = (held & (1u << kBtnRed)) && !setup_.active();
    if (redBtn && !redDown_ && feature(kFeatLockdown) && emerg_ == Emergency::None) {
        emergMine_ = true;
        setEmergency(Emergency::Lockdown, "red button");
        logf("%s", command("emergency lockdown all").c_str());
    }
    redDown_ = redBtn;
    const bool ofcRaw = (held & (1u << kBtnOffice)) && !setup_.active();
    const bool office = ofcRaw && !bothLatch_;
    if (bothLatch_) {  // both buttons: the OFFICE press that was waiting is dropped
        officeSince_ = 0;
        officeActed_ = true;
    }
    if (office && canAnswer()) {
        if (!muted()) talk = true;
        officeActed_ = true;
    } else {
        if (office && !officeDown_) {
            officeSince_ = frame_ ? frame_ : 1;
            officeActed_ = false;
        }
        if (officeSince_ && !officeActed_ && (office ? frame_ - officeSince_ >= kPressDelayFrames : !ofcRaw)) {
            officeActed_ = true;
            if (silentHelp()) {
                setSilent(false, "OFFICE pressed");
            } else if (!callingOffice_) {
                setCallingOffice(true, "OFFICE");
            } else if (!urgent_ && feature(kFeatReason)) {  // pressed again while it rings: help needed
                urgent_ = true;
                event(EventKind::Fault, unit_.room, "room %u: HELP NEEDED (OFFICE pressed again)", unsigned(unit_.room));
                sendStatus();
            }
        } else if (office && officeSince_ && frame_ - officeSince_ == kUrgentHoldFrames && feature(kFeatReason) && !urgent_) {
            // held 1.5 s: "help needed" (one press is a question)
            urgent_ = true;
            if (callingOffice_) {
                event(EventKind::Fault, unit_.room, "room %u: HELP NEEDED (OFFICE held)", unsigned(unit_.room));
                sendStatus();
            } else {
                setCallingOffice(true, "OFFICE held");
            }
        }
    }
    if (!ofcRaw) officeSince_ = 0;
    officeDown_ = office;
    // DOWN held 2 s (outside setup) = call the office too (under the cover); again = cancel. Gives up after 5 min.
    if (held == (1u << kBtnDown) && !setup_.active()) {
        if (!downSince_) downSince_ = frame_;
        else if (frame_ - downSince_ == kCallHoldFrames) setCallingOffice(!callingOffice_, "DOWN");
    } else {
        downSince_ = 0;
    }
    // A plain call rings for 10 s and then gives up, so the room goes back to normal. A call for help waits 5 min.
    if (callingOffice_ && frame_ - callingSince_ >= (urgent_ ? kCallingTimeoutFrames : kOfficeRingFrames)) {
        if (!urgent_) event(EventKind::Call, unit_.room, "room %u called the office: no answer", unsigned(unit_.room));
        setCallingOffice(false, urgent_ ? "no answer in 5 min" : "no answer in 10 s");
    }
    if (talk != transmitting_) {
        if (!talk && talkSince_)  // this unit's own transmission, for the call log
            event(EventKind::Call, unit_.room, "%s to %s, %u s", talkerText(webState_ ? kAudioFromConsole : 0, unit_.room).c_str(),
                  targetText(txFlags_, txTarget_).c_str(), unsigned((frame_ - talkSince_) / 50 + 1));
        transmitting_ = talk;
        talkSince_ = talk ? frame_ : 0;
        if (talk) {  // who this transmission goes to
            if (silentTx && !web && (bothLatch_ || !(held & talkBit))) {
                txTarget_ = actingOffice();
                txFlags_ = kAudioReply;
            } else if (web) {  // web console: the target picked on the school page
                static const uint8_t wflags[] = {kAudioAllCall, 0, kAudioFloor, kAudioGroupCall, kAudioZone};
                txTarget_ = webTarget_.kind == TalkTarget::All ? 0 : webTarget_.n;
                txFlags_ = wflags[webTarget_.kind];
            } else if (canAnswer()) {
                txTarget_ = callFrom_.room;  // 0 = the office console
                txFlags_ = kAudioReply;
            } else {
                static const uint8_t flags[] = {kAudioAllCall, 0, kAudioFloor, kAudioGroupCall, kAudioZone};
                txTarget_ = talkTarget_.kind == TalkTarget::All ? 0 : talkTarget_.n;
                txFlags_ = flags[talkTarget_.kind];
                // The acting office unit, with rooms waiting: TALK answers the first in the queue.
                if (feature(kFeatQueue) && talkTarget_.kind == TalkTarget::All && office_ && !waiting_.empty() && actingOffice() == unit_.room) {
                    txTarget_ = callQueue()[0].room;
                    txFlags_ = 0;
                }
            }
        }
        sendStatus();
    }
    if (talk && txFlags_ == kAudioReply && !silentTx) {  // keep the conversation open while answering
        callLast_ = frame_;
        callAnswered_ = true;
        replied_ = true;
        closing_ = false;
    }
    if (closing_ && callLast_ && frame_ - callLast_ >= kCloseFrames) {  // back to normal: TALK is an all-call again
        callLast_ = 0;
        replied_ = closing_ = false;
        logf("conversation with the office ended");
    }
    if (callLast_ && !callAnswered_ && frame_ - callLast_ == kAnswerFrames) {  // nobody answered in time
        missedUntil_ = frame_ + kMissedFrames;
        callAnswered_ = true;
        event(EventKind::Call, unit_.room, "room %u missed a call from %s", unsigned(unit_.room),
              callFrom_.room ? ("room " + std::to_string(callFrom_.room)).c_str() : "the office");
    }
    if (held && missedUntil_) missedUntil_ = 0;  // any button: the missed call was seen
    if (ofcRingUntil_) {  // calling the office: this unit rings every 2 s while it waits
        if (!callingOffice_ || frame_ >= ofcRingUntil_) {
            ofcRingUntil_ = 0;
        } else if (frame_ >= ofcRingNext_ && !soundActive_ && !transmitting_) {
            startSound(Sound::Ring);
            ofcRingNext_ = frame_ + 100;
        }
    }
    // An office unit: a call for help that nobody has answered rings again every 30 s.
    if (!ofcAlertUntil_ && frame_ >= helpAlertNext_ && office_ && (isOffice(unit_.room) || group() == office_)) {
        for (const auto& w : waiting_) {
            if (!w.urgent && !w.silent) continue;
            ofcAlertRoom_ = w.room;
            ofcAlertUntil_ = frame_ + kOfficeRingFrames;
            ofcAlertNext_ = frame_;
            helpAlertNext_ = frame_ + kHelpAgainFrames;
            break;
        }
    }
    if (ofcAlertUntil_) {  // an office unit: a room is calling, ring every 2 s for 10 s or until it stops waiting
        bool still = false;
        for (const auto& w : waiting_) still = still || w.room == ofcAlertRoom_;
        if (!still || frame_ >= ofcAlertUntil_) {
            ofcAlertUntil_ = 0;
        } else if (frame_ >= ofcAlertNext_ && !soundActive_ && !transmitting_ && !inCall()) {
            startSound(Sound::Ring);
            ofcAlertNext_ = frame_ + 100;
        }
    }
    // Console watch: the acting office unit chirps every 30 s while no office page reads it. Never during an emergency or
    // quiet hours, and never over a call.
    if (conWatchMin_ > 0 && unit_.room != 0 && unit_.room == actingOffice()) {
        const bool miss = frame_ - conSeen_ >= uint32_t(conWatchMin_) * 60000u / kFrameMs;
        if (miss != conMissing_) {
            conMissing_ = miss;
            conChirpNext_ = frame_;
            event(EventKind::Fault, unit_.room, miss ? "office unit %u: the office page is not open on any PC" : "office unit %u: the office page is open again", unsigned(unit_.room));
        }
        if (miss && frame_ >= conChirpNext_ && !soundActive_ && !transmitting_ && !inCall() && emerg_ == Emergency::None && quietNowDb_ == 0) {
            startSound(Sound::Chirp);
            conChirpNext_ = frame_ + 30000 / kFrameMs;
        }
    } else {
        conMissing_ = false;
    }
    if (ringUntil_) {  // "ring": chime again every 2 s until answered or the 10 s are up
        if (callAnswered_ || frame_ >= ringUntil_) ringUntil_ = 0;
        else if ((ringUntil_ - frame_) % 100 == 0 && !soundActive_) startSound(Sound::Chime);
    }
    if (talkSince_ && frame_ - talkSince_ == kTalkHeldFrames) {
        logf("TALK held for over 2 minutes (stuck button?)");
        sendStatus();
    }

    updateLink();

    bool webFrame = false;
    if (web && webBuf_.size() >= size_t(kFrameSamples)) {  // the office PC's voice replaces this unit's mic
        std::copy(webBuf_.begin(), webBuf_.begin() + kFrameSamples, clean);
        webBuf_.erase(webBuf_.begin(), webBuf_.begin() + kFrameSamples);
        webFrame = true;
    }
    if (transmitting_ && link_ != LinkType::None && (!web || webFrame)) {  // an empty web buffer skips a frame (receivers conceal it)
        if (params_.micGainDb != 0 && !webFrame) {
            float g = std::pow(10.0f, params_.micGainDb / 20.0f);
            for (int i = 0; i < kFrameSamples; i++) {
                float v = clean[i] * g;
                clean[i] = int16_t(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
            }
        }
        AudioHeader h;
        h.from = unit_;
        h.flags = txFlags_;
        h.target = txTarget_;
        if (webFrame) h.flags = uint8_t(h.flags | kAudioFromConsole);
        h.seq = seq_++;
        h.level = sentLevel_ = frameLevel(clean, kFrameSamples);
        uint8_t coded[kMaxCodedBytes], buf[kMaxPacketBytes];
        size_t clen = encoder_ ? encoder_->encode(clean, coded, sizeof(coded)) : 0;
        if (clen) {
            h.codec = encoder_->codec();
            hal_.net->send(buf, encodeAudioData(h, coded, clen, buf));
        } else {
            hal_.net->send(buf, encodeAudio(h, clean, buf));  // encoder failed: send this frame raw
        }
    } else {
        sentLevel_ = 0;
    }

    receive();

    // Mix everyone talking.
    int16_t frames[kMaxTalkers][kFrameSamples];
    MixInput in[kMaxTalkers];
    int n = 0;
    for (size_t i = 0; i < talkers_.size();) {
        Talker& t = *talkers_[i];
        uint8_t lvl;
        if (t.jb.pop(frames[n], &lvl)) {
            // A talker this room also hears through a wall / door plays a little quieter, so less of
            // it leaks back into the talker's own room (coupling test, couple_duck_db).
            float c = coupling(t.id.room);
            if (c > CouplingTest::kCoupledDb && c < CouplingTest::kSameRoomDb && params_.coupleDuckDb < 0) {
                float g = std::pow(10.0f, params_.coupleDuckDb / 20.0f);
                for (int s = 0; s < kFrameSamples; s++) frames[n][s] = int16_t(frames[n][s] * g);
            }
            in[n].pcm = frames[n];
            in[n].level = lvl;
            n++;
            retired_.framesPlayed++;
            bool found = false;
            for (auto& h : heard_)
                if (h.first == t.id.key()) { h.second++; found = true; }
            if (!found) heard_.push_back({t.id.key(), 1});
        }
        if (!t.jb.playing() && frame_ - t.lastFrame > uint32_t(kTalkerIdleFrames)) {
            retired_.concealed += t.jb.concealed();
            retired_.lateDrops += t.jb.lateDrops();
            talkers_.erase(talkers_.begin() + long(i));
        } else {
            i++;
        }
    }
    playing_ = n;
    float gainDb = params_.volumeDb + ((transmitting_ && params_.duck) ? params_.duckDb : 0.0f) +
                   (emerg_ == Emergency::None ? quietNowDb_ : 0.0f);  // quiet hours, never during an emergency
    if (groupTalking() && !transmitting_) n = 0;  // another unit in this room is talking (see receive())
    if (n) mixer_.mix(in, n, std::pow(10.0f, gainDb / 20.0f), speaker);
    else std::memset(speaker, 0, sizeof(int16_t) * kFrameSamples);
    if (toneFrames_) {  // test tone, -12 dBFS, mixed in before the echo canceller's reference
        toneFrames_--;
        const float w = 2.0f * 3.14159265f * 1000.0f / kSampleRate;
        for (int i = 0; i < kFrameSamples; i++, tonePhase_++) {
            int v = speaker[i] + int(8200.0f * std::sin(w * float(tonePhase_ % kSampleRate)));
            speaker[i] = int16_t(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
    }
    // Timed events: a waiting chime plays once nobody is talking (up to 60 s late); a call or all-call cuts one.
    if (frame_ % kStatusEveryFrames == 0) everySecond();
    if (frame_ % kStatusEveryFrames == 0) checkSchedule();
    const bool busy = n > 0 || transmitting_ || inCall();
    if (frame_ % kStatusEveryFrames == 0) checkSelfCheck(busy);
    if (soundActive_ && sound_ != Sound::Chime && busy) {
        soundActive_ = false;
        logf("timed event cut short by a call");
    }
    if (pendingSound_ && !busy && !soundActive_) {
        pendingSound_ = false;
        startSound(pending_);
    } else if (pendingSound_ && frame_ - pendingSince_ > kPendingSoundFrames) {
        pendingSound_ = false;
        logf("timed event skipped: a call lasted over a minute");
    }
    if (soundActive_) {  // the call chime or a timed sound, mixed in before the echo canceller's reference
        if (quietNowDb_ < 0) {  // quiet hours turn chimes and bells down too (the emergency signals never)
            int16_t tmp[kFrameSamples] = {};
            soundActive_ = renderSound(sound_, soundPos_, tmp, kFrameSamples);
            const float g = std::pow(10.0f, quietNowDb_ / 20.0f);
            for (int i = 0; i < kFrameSamples; i++) {
                int v = speaker[i] + int(float(tmp[i]) * g);
                speaker[i] = int16_t(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
            }
        } else {
            soundActive_ = renderSound(sound_, soundPos_, speaker, kFrameSamples);
        }
        soundPos_ += kFrameSamples;
    }
    if (emerg_ != Emergency::None && frame_ < emergSoundUntil_) {
        if (n > 0 || transmitting_) {
            emergPos_ = 0;  // a live voice comes first; the signal starts over when it stops
        } else {
            static const Sound kSignal[] = {Sound::Chime, Sound::Lockdown, Sound::Evacuate, Sound::Shelter};
            const Sound s = kSignal[int(emerg_)];
            size_t cn = 0;
            const int16_t* clip = hal_.clips ? hal_.clips->clip(kEmergencyNames[int(emerg_)], &cn) : nullptr;
            if (!clip) cn = 0;
            // signal, then the recorded message and a second of quiet (if there is a message), and again
            const uint32_t a = soundSamples(s), total = a + uint32_t(cn) + (cn ? uint32_t(kSampleRate) : 0u);
            const uint32_t p = emergPos_ % total;
            if (p < a) {
                renderSound(s, p, speaker, kFrameSamples);
            } else {
                for (uint32_t i = 0; i < uint32_t(kFrameSamples) && p - a + i < cn; i++) {
                    int v = speaker[i] + clip[p - a + i];
                    speaker[i] = int16_t(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
                }
            }
            emergPos_ += kFrameSamples;
        }
    }
    spkLevel_ = frameLevel(speaker, kFrameSamples);
    aec_.playback(speaker);

    if (frame_ % kStatusEveryFrames == 0) sendStatus();
    updatePanel();
    updateLamps();
}

}  // namespace ic
