// Time computation: maps UTC -> each city's local wall-clock time using
// IANA time zones via C++20 std::chrono (DST-correct, recomputed every tick).
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

// Compute the local fields for an IANA tz id at the given UTC instant.
LocalTimeFields ComputeLocal(const std::string& ianaTz,
                             std::chrono::system_clock::time_point utc);

// ComputeLocal for one zone, asked again and again with a moving instant.
//
// Every ComputeLocal asks the tz database for the UTC offset, and MSVC's
// database allocates several times per question. The offset only changes at a
// DST or rule transition, so this keeps the period the last answer is valid
// for, [begin, end), and asks again only once the instant leaves it -- forward
// past a transition, or backwards after a clock change. Results are identical
// to ComputeLocal; the gain is that a steady-state frame never allocates.
// (#16 tried this for speed and dropped it at 1.02x; the reason here is the
// allocations.)
class ZoneClock {
public:
    explicit ZoneClock(const std::string& ianaTz);   // resolves the zone once
    LocalTimeFields At(std::chrono::system_clock::time_point utc);

private:
    const std::chrono::time_zone* zone_ = nullptr;   // null: unknown id
    // Where offset_ holds. Empty until the first lookup, so that one asks.
    std::chrono::sys_seconds begin_{};
    std::chrono::sys_seconds end_{};
    std::chrono::seconds     offset_{};
};

// True if the std::chrono tz database is usable on this machine.
bool TzDatabaseAvailable();
