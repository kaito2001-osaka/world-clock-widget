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

// True if the std::chrono tz database is usable on this machine.
bool TzDatabaseAvailable();
