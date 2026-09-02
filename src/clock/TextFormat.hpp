// Pure text formatting for the gadget panel: clock strings and the date
// pattern language. Kept free of Direct2D / DirectWrite so it can be unit
// tested without a render target.
#pragma once
#include <string>
#include <vector>

#include "Config.hpp"
#include "TimeEngine.hpp"

// "9:05", "9:05:30", "9:05 AM" -- per cfg.hourFormat / cfg.showSeconds.
// Returns L"--:--" when the fields are invalid.
std::wstring FormatTime(const LocalTimeFields& f, const Config& cfg);

// Format a date by a token pattern. Tokens (run-length sensitive, .NET-like):
//   yyyy/yy            year (4 / 2 digit)
//   MMMM/MMM/MM/M      month long name / short name / 2-digit / number
//   dddd/ddd/dd/d      weekday long / weekday short / 2-digit day / day number
//   aaaa/aaa..a        Japanese weekday: "日曜日" / "日"
// Any other character is emitted literally (so "年", "月", "/", "-", spaces
// pass through).
std::wstring FormatDatePattern(const LocalTimeFields& f, const std::wstring& pat);

// Substitute every digit with `wide` so a measurement reflects the worst-case
// width for that pattern. Digits in Segoe UI do not share an advance width, so
// measuring the live string would resize the panel every second.
std::wstring WidenDigits(std::wstring s, wchar_t wide);

// ---- worst-case strings for sizing -----------------------------------------
// The panel is sized from what a block *could* hold, not from what it holds
// right now. Sizing from the live string makes the panel grow and shrink as
// "9:59" becomes "10:00" or "Jun 9" becomes "Jun 10", which in horizontal
// layout pushes every later city sideways.
//
// These return every string the corresponding formatter could produce whose
// width might be the maximum; the caller measures them and keeps the widest.
// Both are cheap but not free (the date list is up to 84 entries), so measure
// them when the config or DPI changes, not per frame.

// Clock strings with a two-digit hour, and both AM and PM in 12-hour mode.
std::vector<std::wstring> MeasurementTimeCandidates(const Config& cfg);

// The pattern rendered across every month and weekday it is sensitive to,
// with a two-digit day and a four-digit year.
std::vector<std::wstring> MeasurementDateCandidates(const std::wstring& pat);
