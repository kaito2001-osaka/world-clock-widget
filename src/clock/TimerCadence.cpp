#include "TimerCadence.hpp"

using namespace std::chrono;

UINT MillisecondsToNextBoundary(system_clock::time_point now, bool secondsResolution) {
    const long long period = secondsResolution ? 1000 : 60000;

    // Milliseconds since the epoch, floored -- system_clock's epoch is a whole
    // minute, so "since the epoch modulo the period" is the offset into the
    // current second / minute.
    const long long ms = duration_cast<milliseconds>(now.time_since_epoch()).count();

    // Euclidean remainder: pre-epoch instants must still land in [0, period).
    long long into = ms % period;
    if (into < 0) into += period;

    // Exactly on a boundary yields the full period, never 0: arming a timer
    // with 0 would fire immediately and spin.
    return static_cast<UINT>(period - into);
}

UINT MillisecondsToNextTick(system_clock::time_point now, const Config& cfg) {
    return MillisecondsToNextBoundary(now, cfg.showSeconds);
}

system_clock::time_point DisplayedInstant(system_clock::time_point now, bool secondsResolution) {
    // floor, not duration_cast: pre-epoch instants must round down too.
    return secondsResolution ? system_clock::time_point{ floor<seconds>(now) }
                             : system_clock::time_point{ floor<minutes>(now) };
}

bool NeedsRedraw(std::optional<system_clock::time_point> lastShown,
                 system_clock::time_point now, bool secondsResolution) {
    return !lastShown || *lastShown != DisplayedInstant(now, secondsResolution);
}
