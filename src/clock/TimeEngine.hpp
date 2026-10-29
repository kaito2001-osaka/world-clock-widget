// Time computation: maps UTC -> each city's local wall-clock time.
//
// Cities are stored as IANA ids, but the offsets come from Windows' own time
// zones (HKLM\...\Time Zones, the data the taskbar clock uses), reached through
// CLDR's IANA -> Windows table (WindowsZoneMap). std::chrono's tz database is
// not used: MSVC builds it from the ICU data in icu.dll, which Windows has left
// at tz 2022g, so zones whose rules changed since were an hour off (#49).
#pragma once
#include <string>
#include <chrono>

struct LocalTimeFields {
    bool valid = false;     // false when the IANA tz id could not be resolved
    int  year = 0;
    unsigned month = 0;     // 1..12
    unsigned day = 0;       // 1..31
    unsigned weekday = 0;   // 0=Sun .. 6=Sat
    int  hour = 0;          // 0..23
    int  minute = 0;
    int  second = 0;
};

// When a zone enters or leaves DST, as a SYSTEMTIME in TIME_ZONE_INFORMATION.
struct TzTransition {
    int year = 0;          // 0: the day-th dayOfWeek of month (day 5 = the last
                           // one); otherwise a fixed date, month/day
    int month = 0;         // 1..12; 0 means the zone has no DST
    int dayOfWeek = 0;     // 0=Sun .. 6=Sat
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;   // Windows ends some DST at 23:59:59.999
};

// One year's rules for a zone: TIME_ZONE_INFORMATION, as
// GetTimeZoneInformationForYear returns it (dynamic DST applied).
struct TzYearRule {
    int bias = 0;                 // minutes; UTC = local time + bias
    int standardBias = 0;         // added to bias outside DST
    int daylightBias = 0;         // added to bias during DST
    TzTransition standardDate;    // DST ends; given in local daylight time
    TzTransition daylightDate;    // DST starts; given in local standard time
};

// A UTC offset and the period it holds for, [begin, end).
struct OffsetSpan {
    std::chrono::seconds offset{};   // local time - UTC
    std::chrono::sys_seconds begin{};
    std::chrono::sys_seconds end{};
};

// Which year's rules hold at `utc`, and the period that answer holds for.
// Windows picks them by the year of the local standard time, not of UTC: in
// Vancouver, 2026-01-01 03:00Z is still 2025 and keeps 2025's rules. The
// standard offset comes from `utcYearRule`, the rules for utc's own UTC year,
// and the period stays inside that UTC year so the choice is made the same
// way throughout it.
struct RuleYearSpan {
    int year = 0;
    std::chrono::sys_seconds begin{};
    std::chrono::sys_seconds end{};
};
RuleYearSpan RuleYearAt(const TzYearRule& utcYearRule, std::chrono::sys_seconds utc);

// The offset `rule`, the rules for local year `year`, gives at `utc`. The span
// never reaches outside that year (in local standard time), since the next
// year can have other rules. Pure, like RuleYearAt, so the transition
// arithmetic is unit testable without the registry.
OffsetSpan OffsetInYear(const TzYearRule& rule, int year, std::chrono::sys_seconds utc);

// Compute the local fields for an IANA tz id at the given UTC instant.
LocalTimeFields ComputeLocal(const std::string& ianaTz,
                             std::chrono::system_clock::time_point utc);

struct _TIME_DYNAMIC_ZONE_INFORMATION;   // DYNAMIC_TIME_ZONE_INFORMATION

// ComputeLocal for one zone, asked again and again with a moving instant.
//
// The offset only changes at a DST transition or a new year's rules, so this
// keeps the period the last answer is valid for, [begin, end), and asks
// Windows again only once the instant leaves it -- forward past a transition,
// or backwards after a clock change. Results are identical to ComputeLocal;
// the gain is that a steady-state frame never allocates or calls the OS.
class ZoneClock {
public:
    explicit ZoneClock(const std::string& ianaTz);   // resolves the zone once
    LocalTimeFields At(std::chrono::system_clock::time_point utc);

private:
    const _TIME_DYNAMIC_ZONE_INFORMATION* zone_ = nullptr;   // null: unknown id
    // Where offset_ holds. Empty until the first lookup, so that one asks.
    std::chrono::sys_seconds begin_{};
    std::chrono::sys_seconds end_{};
    std::chrono::seconds     offset_{};
};
