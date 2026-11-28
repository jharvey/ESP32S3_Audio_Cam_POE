// LoRa side channel (DRAFT): where the intercom meets LoraLink. It tells LoraLink what this unit knows, and it
// writes what LoraLink heard into the event log.
//
// The rule: nothing heard by radio changes a setting, and nothing heard by radio starts, changes or clears an
// emergency on a unit that has a network link. The one case decided here: a unit with NO network link (a mobile
// unit carried outside, or a room cut off from the network) shows an alarm that an office unit sent by radio,
// signed with the network key. That alarm is marked "by radio": the radio's "all clear" ends only such an alarm,
// never one that started any other way.
#include "Intercom.h"

#include <algorithm>

namespace ic {

namespace {
constexpr uint32_t kNoLinkFrames = 10 * 1000 / kFrameMs;    // the link must be gone this long before a radio alarm is shown
constexpr uint32_t kLinkBackFrames = 90 * 1000 / kFrameMs;  // the link is back this long: the office's word over the network counts
}  // namespace

LoraLink::State Intercom::loraState() const {
    LoraLink::State st;
    st.frame = frame_;
    st.room = unit_.room;
    st.linkUp = link_ != LinkType::None;
    // Only the acting office unit sends. A stand-in without a network link cannot know whether it is the acting
    // office (it hears nobody), so it only listens; the main office unit sends even when it is cut off.
    st.sender = frame_ > 250 && office_ && actingOffice() == unit_.room && iAmOffice() && (st.linkUp || unit_.room == office_);
    if (emerg_ != Emergency::None && !emergByRadio_) {  // an alarm heard by radio is not sent on
        st.emergency = uint8_t(emerg_);
        st.emergencyS = (frame_ - emergSince_) * kFrameMs / 1000;
        if (emergMine_) st.origin = unit_.room;
        else
            for (const Peer& p : peers_)
                if (p.status.emergency && (p.status.flags2 & kStatus2Red)) st.origin = p.status.from.room;
    }
    for (const Waiting& w : callQueue()) {  // a plain call (a question) is not worth the radio
        if (!w.urgent && !w.silent) continue;
        lora::HelpEntry h;
        h.room = w.room;
        h.urgent = w.urgent;
        h.silent = w.silent;
        h.waitS = uint16_t(std::min<uint32_t>((frame_ - w.since) * kFrameMs / 1000, 65535));
        st.help.push_back(h);
    }
    st.online = uint8_t(std::min<size_t>(peers_.size() + 1, 255));
    st.offline = uint8_t(std::min<size_t>(known_.size() > peers_.size() ? known_.size() - peers_.size() : 0, 255));
    st.power = power_.source == PowerSource::Battery ? 1 : 0;
    st.battery = power_.hasBattery ? power_.batteryPct : 255;
    st.uptimeS = frame_ * kFrameMs / 1000;
    return st;
}

// Every room with a people sensor, as the "people" command lists them: several units in one room count once.
std::vector<lora::PeopleEntry> Intercom::loraPeople() const {
    struct Row {
        uint16_t room;
        uint8_t kind, n, quiet;
    };
    std::vector<Row> rows;
    auto add = [&](uint16_t room, uint8_t kind, uint8_t n, uint8_t quiet) {
        if (!kind) return;
        for (Row& r : rows)
            if (r.room == room) {
                if (n > r.n) r.n = n;
                if (kind > r.kind) r.kind = kind;
                if (quiet < r.quiet) r.quiet = quiet;
                return;
            }
        rows.push_back({room, kind, n, quiet});
    };
    add(group(), radar().kind, people(), radarQuietMin());
    for (const Peer& p : peers_) add(p.status.group ? p.status.group : p.status.from.room, p.status.radarKind, p.status.radarPeople, p.status.radarQuietMin);
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.room < b.room; });
    std::vector<lora::PeopleEntry> out;
    for (const Row& r : rows) {
        lora::PeopleEntry e;
        e.room = r.room;
        e.code = !r.n ? lora::kPeopleEmpty : r.kind == kRadarPresence ? lora::kPeopleSomeone : uint8_t(std::min<int>(r.n, lora::kPeopleThreePlus));
        e.quiet = r.n ? 0 : lora::quietCode(r.quiet);
        out.push_back(e);
    }
    return out;
}

void Intercom::loraTick() {
    if (!lora_.on()) return;
    if (emerg_ == Emergency::None) emergByRadio_ = false;
    std::vector<LoraLink::Note> notes;
    lora_.tick(loraState(), &notes);
    for (const LoraLink::Note& n : notes) {
        if (!n.text.empty()) event(n.fault ? EventKind::Fault : EventKind::Info, n.room, "%s", n.text.c_str());
        const bool noLink = link_ == LinkType::None && frame_ - linkLostAt_ >= kNoLinkFrames;
        // A call for help heard outside: one chime. TODO(LoRa): ring and blink like a call on the office unit.
        if (n.chime && noLink && !soundActive_ && emerg_ == Emergency::None) startSound(Sound::Chime);
        if (n.alarm < 0) continue;
        // A signed EMERGENCY message. It is shown only without a network link, and only from an office unit.
        if (!noLink || !isOffice(n.from)) continue;
        if (n.alarm >= 1 && n.alarm <= 3) {
            if (emerg_ == Emergency::None || (emergByRadio_ && int(emerg_) != n.alarm)) {
                setEmergency(Emergency(n.alarm), "heard by radio, no network link");
                emergByRadio_ = true;
            }
        } else if (n.alarm == 0 && emergByRadio_ && emerg_ != Emergency::None) {
            setEmergency(Emergency::None, "all clear heard by radio");
            emergByRadio_ = false;
        }
    }
    // The network link is back: from now on the office's word over the network counts.
    if (emergByRadio_ && link_ != LinkType::None) {
        if (!loraLinkAt_) loraLinkAt_ = frame_ ? frame_ : 1;
        if (frame_ - loraLinkAt_ >= kLinkBackFrames) {
            const uint16_t act = actingOffice();
            for (const Peer& p : peers_) {
                if (p.status.from.room != act) continue;
                emergByRadio_ = false;
                if (!p.status.emergency) setEmergency(Emergency::None, "the office shows no emergency");
                break;
            }
        }
    } else {
        loraLinkAt_ = 0;
    }
}

}  // namespace ic
