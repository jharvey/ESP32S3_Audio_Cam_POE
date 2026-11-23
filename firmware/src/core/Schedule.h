// Timed events (R2.1): local time from UTC, the weekly chime table, days off.
// Every unit keeps the whole table and plays the entries meant for it, so chimes still
// work with the office PC off. The table is pushed from the console as signed commands.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Packet.h"
#include "Sounds.h"

namespace ic {

struct LocalTime {
    int year = 1970, month = 1, day = 1;
    int wday = 3;  // 0 = Monday ... 6 = Sunday
    int hour = 0, minute = 0, second = 0;
    bool dst = false;
    uint32_t ymd() const { return uint32_t(year * 10000 + month * 100 + day); }
};

struct ScheduleEntry {
    uint16_t minute = 0;  // minute of the day, 0..1439
    uint8_t days = 0x1f;  // bit 0 = Monday ... bit 6 = Sunday
    Sound sound = Sound::Bell;
    TalkTarget target;    // who plays it (All, Room, Floor, Group)
};

enum class DstRule : uint8_t { None = 0, Us = 1, Eu = 2 };

class Schedule {
public:
    static constexpr size_t kMaxEntries = 40;
    static constexpr size_t kMaxDaysOff = 60;

    std::vector<ScheduleEntry> entries;  // kept sorted by time
    std::vector<uint32_t> daysOff;       // yyyymmdd
    uint32_t skipDay = 0;                // yyyymmdd: "skip today"
    int tzMinutes = 0;                   // standard time offset from UTC (US Eastern: -300)
    DstRule dst = DstRule::None;

    LocalTime local(int64_t utc) const;
    bool isDayOff(const LocalTime& t) const;
    bool appliesTo(const ScheduleEntry& e, uint16_t room, uint16_t group) const;

    // "11:30 mon-fri lunch [room N | floor N | group N]", tokens from a[from]. Returns an error or "".
    static std::string parseEntry(const std::vector<std::string>& a, size_t from, ScheduleEntry* e);
    static std::string formatEntry(const ScheduleEntry& e);
    static bool parseDate(const std::string& s, uint32_t* ymd);  // "2026-12-24"
    static bool parseDays(const std::string& s, uint8_t* mask);  // "mon-fri", "daily", "mon,wed,fri", "weekend"
    std::string add(const ScheduleEntry& e);                     // error or ""

    std::string list() const;      // numbered, for people
    std::string toString() const;  // compact, for flash and for "sched set"
    bool fromString(const std::string& s);
};

// Days since 1970-01-01 for a civil date, and back (proleptic Gregorian).
int64_t daysFromCivil(int y, int m, int d);
void civilFromDays(int64_t z, int* y, int* m, int* d);

}  // namespace ic
