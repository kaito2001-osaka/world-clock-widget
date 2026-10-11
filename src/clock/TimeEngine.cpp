#include "TimeEngine.hpp"

#include <chrono>
#include <unordered_map>
#include <mutex>

using namespace std::chrono;

namespace {
// Cache resolved time_zone* by IANA name. The zone POINTERS are stable;
// we still recompute the offset from UTC on every call, so DST stays correct.
std::mutex g_mtx;
std::unordered_map<std::string, const time_zone*> g_zones;

const time_zone* ResolveZone(const std::string& name) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (auto it = g_zones.find(name); it != g_zones.end()) return it->second;
    const time_zone* z = nullptr;
    try {
        z = locate_zone(name);
    } catch (...) {
        z = nullptr;
    }
    g_zones[name] = z;
    return z;
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

bool TzDatabaseAvailable() {
    try {
        (void)get_tzdb();
        (void)locate_zone("Etc/UTC");
        return true;
    } catch (...) {
        return false;
    }
}

LocalTimeFields ComputeLocal(const std::string& ianaTz,
                             system_clock::time_point utc) {
    LocalTimeFields f;
    const time_zone* zone = ResolveZone(ianaTz);
    if (!zone) return f; // valid stays false

    try {
        zoned_time zt{zone, utc};
        return FieldsFromLocal(zt.get_local_time());
    } catch (...) {
        return f;
    }
}

ZoneClock::ZoneClock(const std::string& ianaTz) : zone_(ResolveZone(ianaTz)) {}

LocalTimeFields ZoneClock::At(system_clock::time_point utc) {
    if (!zone_) return {};   // valid stays false
    // Outside the cached period -- including a clock that went backwards --
    // ask the database again. Inside it the offset cannot have changed.
    // Compared in seconds: a zone with no future transition ends in the year
    // 32767, which overflows system_clock's 100 ns ticks. The period's bounds
    // are whole seconds, so flooring the instant does not move it across one.
    const sys_seconds s = floor<seconds>(utc);
    if (s < begin_ || s >= end_) {
        try {
            const sys_info info = zone_->get_info(utc);
            begin_  = info.begin;
            end_    = info.end;
            offset_ = info.offset;
        } catch (...) {
            begin_ = end_ = sys_seconds{};   // empty, so the next call retries
            return {};
        }
    }
    // What zoned_time::get_local_time computes, minus the lookup.
    return FieldsFromLocal(local_time<system_clock::duration>{ utc.time_since_epoch() + offset_ });
}
