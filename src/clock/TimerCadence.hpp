// Scheduling the redraw tick.
//
// WM_TIMER is a low-priority synthesised message: it is delivered only when the
// queue is empty, consecutive fires are coalesced, and the effective period is
// always >= the requested one. A fixed 1000 ms timer therefore drifts, and a
// redraw gated on "the second changed" ends up skipping ticks -- a second is
// displayed twice, then the display jumps by two.
//
// Re-arming a one-shot to the *next boundary* instead keeps every fire just
// after a real second (or minute) rollover, which fixes the stutter and lets
// the gadget sleep a full minute when it is not showing seconds.
#pragma once
#include <windows.h>
#include <chrono>

#include "Config.hpp"

// Milliseconds from `now` until the next second boundary (secondsResolution),
// or the next minute boundary. Always in (0, 1000] / (0, 60000]: exactly on a
// boundary it returns the full period rather than 0, so SetTimer can never be
// armed with a zero delay and spin.
UINT MillisecondsToNextBoundary(std::chrono::system_clock::time_point now,
                                bool secondsResolution);

// The interval the gadget should actually sleep for: the next second boundary
// when seconds are on display, otherwise the next minute boundary. Waking once
// a second to redraw a minute hand is 59 wasted wakeups out of every 60 on a
// process that never exits.
UINT MillisecondsToNextTick(std::chrono::system_clock::time_point now,
                            const Config& cfg);
