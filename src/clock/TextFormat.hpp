// Pure text formatting for the gadget panel: clock strings and the date
// pattern language. Kept free of Direct2D / DirectWrite so it can be unit
// tested without a render target.
#pragma once
#include <string>

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
