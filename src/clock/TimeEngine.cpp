#include "TimeEngine.hpp"
#include "WindowsZoneMap.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

using namespace std::chrono;

namespace {
// Cache the zone each IANA name resolves to (null for none). Entries are never
// removed, so the pointers ZoneClock keeps stay valid; only the rules for a
// year are fetched again, so a new year's dynamic DST is picked up.
std::mutex g_mtx;
std::unordered_map<std::string, std::unique_ptr<DYNAMIC_TIME_ZONE_INFORMATION>> g_zones;

std::unique_ptr<DYNAMIC_TIME_ZONE_INFORMATION> FindWindowsZone(const wchar_t* keyName) {
    for (DWORD i = 0;; ++i) {
        auto z = std::make_unique<DYNAMIC_TIME_ZONE_INFORMATION>();
        if (EnumDynamicTimeZoneInformation(i, z.get()) != ERROR_SUCCESS) return nullptr;
        if (_wcsicmp(z->TimeZoneKeyName, keyName) == 0) return z;
    }
}

const DYNAMIC_TIME_ZONE_INFORMATION* ResolveZone(const std::string& name) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (auto it = g_zones.find(name); it != g_zones.end()) return it->second.get();
    // An IANA name with no Windows zone, or one this Windows does not have.
    const wchar_t* id = WindowsZoneId(name);
    auto& slot = g_zones[name];
    slot = id ? FindWindowsZone(id) : nullptr;
    return slot.get();
}

TzTransition FromSystemTime(const SYSTEMTIME& st) {
    return { st.wYear, st.wMonth, st.wDayOfWeek, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds };
}

bool RuleForYear(const DYNAMIC_TIME_ZONE_INFORMATION& zone, int y, TzYearRule& rule) {
    if (y < 1601 || y > 30827) return false;   // SYSTEMTIME's range
    // The API takes a non-const pointer, though it only reads the zone.
    DYNAMIC_TIME_ZONE_INFORMATION dtzi = zone;
    TIME_ZONE_INFORMATION tzi{};
    if (!GetTimeZoneInformationForYear(USHORT(y), &dtzi, &tzi)) return false;
    rule.bias         = tzi.Bias;
    rule.standardBias = tzi.StandardBias;
    rule.daylightBias = tzi.DaylightBias;
    rule.standardDate = FromSystemTime(tzi.StandardDate);
    rule.daylightDate = FromSystemTime(tzi.DaylightDate);
    return true;
}

bool IsUsable(const TzTransition& t) {
    return t.month >= 1 && t.month <= 12 && t.dayOfWeek >= 0 && t.dayOfWeek <= 6 &&
           t.day >= 1 && t.day <= 31;
}

// The local date and time `t` names in year `y`, as if it were UTC.
sys_time<milliseconds> LocalTransition(const TzTransition& t, int y) {
    const year_month ym = year_month{ std::chrono::year{ y }, month{ unsigned(t.month) } };
    sys_days date;
    if (t.year != 0) {
        // A fixed date; clamped so a day past the month's end cannot wrap.
        const unsigned lastDay = unsigned((ym / std::chrono::last).day());
        date = sys_days{ ym / day{ std::min(unsigned(t.day), lastDay) } };
    } else {
        // The day-th weekday of the month, with 5 meaning the last one.
        const weekday wd{ unsigned(t.dayOfWeek) };
        const unsigned n = unsigned(std::min(t.day, 5));
        const year_month_day_last eom = ym / std::chrono::last;
        date = sys_days{ ym / wd[1] } + weeks{ n - 1 };
        if (date > sys_days{ eom }) date -= weeks{ 1 };
    }
    return date + hours{ t.hour } + minutes{ t.minute } + seconds{ t.second } +
           milliseconds{ t.millisecond };
}

LocalTimeFields FieldsFromLocal(local_time<system_clock::duration> lt) {
    auto dp  = floor<days>(lt);
    year_month_day ymd{dp};
    weekday wd{dp};
    hh_mm_ss hms{lt - dp};

    LocalTimeFields f;
    f.valid   = true;
    f.year    = int(ymd.year());
    f.month   = unsigned(ymd.month());
    f.day     = unsigned(ymd.day());
    f.weekday = wd.c_encoding(); // 0=Sun
    f.hour    = int(hms.hours().count());
    f.minute  = int(hms.minutes().count());
    f.second  = int(hms.seconds().count());
    return f;
}
} // namespace

namespace {
sys_seconds NewYear(int y) {
    return sys_seconds{ sys_days{ std::chrono::year{ y } / January / 1 } };
}
} // namespace

RuleYearSpan RuleYearAt(const TzYearRule& utcYearRule, sys_seconds utc) {
    const int utcYear = int(year_month_day{ floor<days>(utc) }.year());
    const seconds standard = -minutes{ utcYearRule.bias + utcYearRule.standardBias };
    const int y = int(year_month_day{ floor<days>(utc + standard) }.year());
    return { y, std::max(NewYear(utcYear), NewYear(y) - standard),
                std::min(NewYear(utcYear + 1), NewYear(y + 1) - standard) };
}

OffsetSpan OffsetInYear(const TzYearRule& rule, int y, sys_seconds utc) {
    const seconds standard = -minutes{ rule.bias + rule.standardBias };
    const seconds daylight = -minutes{ rule.bias + rule.daylightBias };
    const sys_seconds yearBegin = NewYear(y) - standard;
    const sys_seconds yearEnd   = NewYear(y + 1) - standard;

    if (rule.daylightDate.month == 0 || !IsUsable(rule.daylightDate) ||
        !IsUsable(rule.standardDate))
        return { standard, yearBegin, yearEnd };

    // Each transition is written in the local time in force just before it.
    // Rounded up to whole seconds: an end at 23:59:59.999 takes effect, to a
    // clock showing seconds, at midnight.
    const sys_seconds dstStart = ceil<seconds>(LocalTransition(rule.daylightDate, y) - standard);
    const sys_seconds dstEnd   = ceil<seconds>(LocalTransition(rule.standardDate, y) - daylight);
    if (dstStart == dstEnd) return { standard, yearBegin, yearEnd };

    // DST covers [start, end), or, where it spans the new year (the southern
    // hemisphere), everything outside [end, start). Windows also writes a
    // switch to permanent DST that way: British Columbia's 2026 rule "ends"
    // DST on January 1 and starts it in March.
    const bool northern = dstStart < dstEnd;
    const bool inDst = northern ? (utc >= dstStart && utc < dstEnd)
                                : (utc >= dstStart || utc < dstEnd);

    // The offset is constant between neighbouring boundaries.
    sys_seconds begin = yearBegin, end = yearEnd;
    for (sys_seconds b : { dstStart, dstEnd }) {
        if (b <= utc) begin = std::max(begin, b);
        else          end   = std::min(end, b);
    }
    return { inDst ? daylight : standard, begin, end };
}

LocalTimeFields ComputeLocal(const std::string& ianaTz,
                             system_clock::time_point utc) {
    return ZoneClock{ ianaTz }.At(utc);
}

ZoneClock::ZoneClock(const std::string& ianaTz) : zone_(ResolveZone(ianaTz)) {}

LocalTimeFields ZoneClock::At(system_clock::time_point utc) {
    if (!zone_) return {};   // valid stays false
    // Outside the cached period -- including a clock that went backwards --
    // ask Windows again. Inside it the offset cannot have changed. The
    // period's bounds are whole seconds, so flooring the instant does not move
    // it across one.
    const sys_seconds s = floor<seconds>(utc);
    if (s < begin_ || s >= end_) {
        // The rules of the instant's UTC year say which year's rules apply.
        const int utcYear = int(year_month_day{ floor<days>(s) }.year());
        TzYearRule rule;
        if (!RuleForYear(*zone_, utcYear, rule)) {
            begin_ = end_ = sys_seconds{};   // empty, so the next call retries
            return {};
        }
        const RuleYearSpan which = RuleYearAt(rule, s);
        if (which.year != utcYear && !RuleForYear(*zone_, which.year, rule)) {
            begin_ = end_ = sys_seconds{};
            return {};
        }
        OffsetSpan span = OffsetInYear(rule, which.year, s);
        span.begin = std::max(span.begin, which.begin);
        span.end   = std::min(span.end, which.end);
        begin_  = span.begin;
        end_    = span.end;
        offset_ = span.offset;
    }
    return FieldsFromLocal(local_time<system_clock::duration>{ utc.time_since_epoch() + offset_ });
}
