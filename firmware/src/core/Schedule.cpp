#include "Schedule.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace ic {

// Howard Hinnant's civil-date algorithms.
int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * unsigned(m + (m > 2 ? -3 : 9)) + 2) / 5 + unsigned(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

void civilFromDays(int64_t z, int* y, int* m, int* d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = int(doy - (153 * mp + 2) / 5 + 1);
    *m = int(mp < 10 ? mp + 3 : mp - 9);
    *y = int(int64_t(yoe) + era * 400 + (*m <= 2));
}

namespace {
const char* const kDayNames[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

int weekday(int64_t days) { return int(((days % 7) + 7 + 3) % 7); }  // 1970-01-01 was a Thursday (3)

// The nth (1-based) Sunday of a month, or the last one (n = 0).
int64_t sunday(int y, int m, int n) {
    if (n == 0) {
        int64_t last = daysFromCivil(y, m + 1 > 12 ? 1 : m + 1, 1) - 1;
        if (m == 12) last = daysFromCivil(y + 1, 1, 1) - 1;
        return last - (weekday(last) + 1) % 7;
    }
    int64_t first = daysFromCivil(y, m, 1);
    return first + (6 - weekday(first) + 7) % 7 + 7 * (n - 1);
}

int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
}  // namespace

LocalTime Schedule::local(int64_t utc) const {
    int64_t std = utc + int64_t(tzMinutes) * 60;
    bool dstNow = false;
    int y, m, d;
    civilFromDays(floorDiv(std, 86400), &y, &m, &d);
    if (dst == DstRule::Us) {  // 2nd Sunday of March 2:00 to 1st Sunday of November 2:00 (local)
        int64_t start = sunday(y, 3, 2) * 86400 + 2 * 3600;
        int64_t end = sunday(y, 11, 1) * 86400 + 1 * 3600;  // 2:00 daylight time = 1:00 standard time
        dstNow = std >= start && std < end;
    } else if (dst == DstRule::Eu) {  // last Sunday of March to last Sunday of October, 1:00 UTC
        dstNow = utc >= sunday(y, 3, 0) * 86400 + 3600 && utc < sunday(y, 10, 0) * 86400 + 3600;
    }
    int64_t t = std + (dstNow ? 3600 : 0);
    int64_t days = floorDiv(t, 86400);
    int64_t secs = t - days * 86400;
    LocalTime lt;
    civilFromDays(days, &lt.year, &lt.month, &lt.day);
    lt.wday = weekday(days);
    lt.hour = int(secs / 3600);
    lt.minute = int(secs / 60 % 60);
    lt.second = int(secs % 60);
    lt.dst = dstNow;
    return lt;
}

bool Schedule::isDayOff(const LocalTime& t) const {
    uint32_t today = t.ymd();
    return today == skipDay || std::find(daysOff.begin(), daysOff.end(), today) != daysOff.end();
}

bool Schedule::appliesTo(const ScheduleEntry& e, uint16_t room, uint16_t group) const {
    switch (e.target.kind) {
        case TalkTarget::All: return true;
        case TalkTarget::Room: return e.target.n == room;
        case TalkTarget::Floor: return e.target.n == room / 100;
        case TalkTarget::Group: return e.target.n == group || e.target.n == room;
    }
    return false;
}

bool Schedule::parseDays(const std::string& s, uint8_t* mask) {
    if (s == "daily" || s == "all") return *mask = 0x7f, true;
    if (s == "weekdays") return *mask = 0x1f, true;
    if (s == "weekend") return *mask = 0x60, true;
    auto day = [](const std::string& n) {
        for (int i = 0; i < 7; i++)
            if (n == kDayNames[i]) return i;
        return -1;
    };
    uint8_t m = 0;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        size_t dash = part.find('-');
        int a = day(part.substr(0, dash)), b = dash == std::string::npos ? a : day(part.substr(dash + 1));
        if (a < 0 || b < 0) return false;
        for (int i = a;; i = (i + 1) % 7) {
            m |= uint8_t(1u << i);
            if (i == b) break;
        }
    }
    if (!m) return false;
    *mask = m;
    return true;
}

bool Schedule::parseDate(const std::string& s, uint32_t* ymd) {
    int y, m, d;
    if (std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || y < 2020 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31)
        return false;
    *ymd = uint32_t(y * 10000 + m * 100 + d);
    return true;
}

std::string Schedule::parseEntry(const std::vector<std::string>& a, size_t from, ScheduleEntry* e) {
    const char* usage = "usage: sched add HH:MM mon-fri|daily|weekend|mon,wed sound [room N|floor N|group N]";
    if (a.size() < from + 3) return usage;
    int h, m;
    char c;
    if (std::sscanf(a[from].c_str(), "%d%c%d", &h, &c, &m) != 3 || c != ':' || h < 0 || h > 23 || m < 0 || m > 59)
        return "bad time (24 h, e.g. 11:30)";
    ScheduleEntry r;
    r.minute = uint16_t(h * 60 + m);
    if (!parseDays(a[from + 1], &r.days)) return "bad days (mon-fri, daily, weekend, mon,wed,fri)";
    if (!soundFromName(a[from + 2], &r.sound)) return "unknown sound; sounds: " + soundList();
    if (a.size() > from + 3) {
        const std::string& k = a[from + 3];
        unsigned long n = a.size() > from + 4 ? std::strtoul(a[from + 4].c_str(), nullptr, 10) : 0;
        if (k == "room") r.target.kind = TalkTarget::Room;
        else if (k == "floor") r.target.kind = TalkTarget::Floor;
        else if (k == "group") r.target.kind = TalkTarget::Group;
        else if (k != "all") return usage;
        if (r.target.kind != TalkTarget::All && (n < 1 || n > 999)) return usage;
        r.target.n = uint16_t(n);
    }
    *e = r;
    return "";
}

std::string Schedule::formatEntry(const ScheduleEntry& e) {
    char buf[64];
    std::string days;
    if (e.days == 0x7f) days = "daily";
    else if (e.days == 0x1f) days = "mon-fri";
    else if (e.days == 0x60) days = "weekend";
    else
        for (int i = 0; i < 7; i++)
            if (e.days & (1u << i)) days += (days.empty() ? "" : ",") + std::string(kDayNames[i]);
    static const char* kinds[] = {"all", "room", "floor", "group"};
    std::snprintf(buf, sizeof(buf), "%02d:%02d %s %s %s", e.minute / 60, e.minute % 60, days.c_str(), soundName(e.sound),
                  kinds[e.target.kind]);
    std::string out = buf;
    if (e.target.kind != TalkTarget::All) out += " " + std::to_string(e.target.n);
    return out;
}

std::string Schedule::add(const ScheduleEntry& e) {
    if (entries.size() >= kMaxEntries) return "schedule full (40 entries)";
    entries.push_back(e);
    std::stable_sort(entries.begin(), entries.end(), [](const ScheduleEntry& x, const ScheduleEntry& y) { return x.minute < y.minute; });
    return "";
}

std::string Schedule::list() const {
    std::string out;
    static const char* rules[] = {"none", "us", "eu"};
    char buf[96];
    std::snprintf(buf, sizeof(buf), "time zone %+d min, daylight saving %s\n", tzMinutes, rules[int(dst)]);
    out += buf;
    if (entries.empty()) out += "no timed events\n";
    for (size_t i = 0; i < entries.size(); i++) out += std::to_string(i + 1) + ". " + formatEntry(entries[i]) + "\n";
    if (!daysOff.empty()) {
        out += "days off:";
        for (uint32_t d : daysOff) {
            std::snprintf(buf, sizeof(buf), " %04u-%02u-%02u", d / 10000, d / 100 % 100, d % 100);
            out += buf;
        }
        out += "\n";
    }
    if (skipDay) {
        std::snprintf(buf, sizeof(buf), "skipping %04u-%02u-%02u\n", skipDay / 10000, skipDay / 100 % 100, skipDay % 100);
        out += buf;
    }
    return out;
}

// tz=-300 dst=1 skip=0 off=20261224,20261225 e=690/31/2/0/0,900/31/1/2/1
std::string Schedule::toString() const {
    std::string s = "tz=" + std::to_string(tzMinutes) + " dst=" + std::to_string(int(dst)) + " skip=" + std::to_string(skipDay);
    s += " off=";
    for (size_t i = 0; i < daysOff.size(); i++) s += (i ? "," : "") + std::to_string(daysOff[i]);
    s += " e=";
    for (size_t i = 0; i < entries.size(); i++) {
        const ScheduleEntry& e = entries[i];
        s += (i ? "," : "") + std::to_string(e.minute) + "/" + std::to_string(e.days) + "/" + std::to_string(int(e.sound)) + "/" +
             std::to_string(int(e.target.kind)) + "/" + std::to_string(e.target.n);
    }
    return s;
}

bool Schedule::fromString(const std::string& str) {
    Schedule r;
    std::stringstream in(str);
    std::string tok;
    while (in >> tok) {
        size_t eq = tok.find('=');
        if (eq == std::string::npos) return false;
        std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
        if (k == "tz") {
            r.tzMinutes = std::atoi(v.c_str());
            if (r.tzMinutes < -720 || r.tzMinutes > 840) return false;
        } else if (k == "dst") {
            int d = std::atoi(v.c_str());
            if (d < 0 || d > 2) return false;
            r.dst = DstRule(d);
        } else if (k == "skip") {
            r.skipDay = uint32_t(std::strtoul(v.c_str(), nullptr, 10));
        } else if (k == "off" || k == "e") {
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                if (item.empty()) continue;
                if (k == "off") {
                    if (r.daysOff.size() < kMaxDaysOff) r.daysOff.push_back(uint32_t(std::strtoul(item.c_str(), nullptr, 10)));
                    continue;
                }
                unsigned mi, days, snd, kind, n;
                if (std::sscanf(item.c_str(), "%u/%u/%u/%u/%u", &mi, &days, &snd, &kind, &n) != 5 || mi > 1439 || days > 0x7f ||
                    snd >= unsigned(Sound::Count) || kind > 3 || n > 999)
                    return false;
                ScheduleEntry e;
                e.minute = uint16_t(mi);
                e.days = uint8_t(days);
                e.sound = Sound(snd);
                e.target.kind = TalkTarget::Kind(kind);
                e.target.n = uint16_t(n);
                if (!r.add(e).empty()) return false;
            }
        }
    }
    *this = r;
    return true;
}

}  // namespace ic
