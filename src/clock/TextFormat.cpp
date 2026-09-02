#include "TextFormat.hpp"

#include <cstdio>

namespace {

const wchar_t* kWeekdayShort[7] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
const wchar_t* kWeekdayLong[7]  = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday",
                                    L"Thursday", L"Friday", L"Saturday" };
const wchar_t* kWeekdayJp[7]    = { L"日", L"月", L"火", L"水", L"木", L"金", L"土" };
const wchar_t* kMonthShort[13]  = { L"", L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                    L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
const wchar_t* kMonthLong[13]   = { L"", L"January", L"February", L"March", L"April", L"May",
                                    L"June", L"July", L"August", L"September", L"October",
                                    L"November", L"December" };

} // namespace

std::wstring FormatTime(const LocalTimeFields& f, const Config& cfg) {
    if (!f.valid) return L"--:--";
    int h = f.hour;
    const wchar_t* ampm = L"";
    if (cfg.hourFormat == 12) {
        ampm = (h < 12) ? L" AM" : L" PM";
        h = h % 12; if (h == 0) h = 12;
    }
    wchar_t buf[32];
    if (cfg.showSeconds)
        swprintf(buf, 32, L"%d:%02d:%02d%s", h, f.minute, f.second, ampm);
    else
        swprintf(buf, 32, L"%d:%02d%s", h, f.minute, ampm);
    return buf;
}

std::wstring FormatDatePattern(const LocalTimeFields& f, const std::wstring& pat) {
    if (!f.valid) return L"";
    std::wstring out;
    size_t i = 0, n = pat.size();
    wchar_t buf[16];
    auto run = [&](wchar_t ch) { size_t j = i; int c = 0; while (j < n && pat[j] == ch) { ++c; ++j; } return c; };
    unsigned mo = f.month <= 12 ? f.month : 0;
    unsigned wd = f.weekday % 7;
    while (i < n) {
        wchar_t ch = pat[i];
        switch (ch) {
            case L'y': { int r = run(L'y');
                if (r >= 4) swprintf(buf, 16, L"%04d", f.year);
                else        swprintf(buf, 16, L"%02d", ((f.year % 100) + 100) % 100);
                out += buf; i += r; break; }
            case L'M': { int r = run(L'M');
                if (r >= 4)      out += kMonthLong[mo];
                else if (r == 3) out += kMonthShort[mo];
                else { swprintf(buf, 16, r == 2 ? L"%02u" : L"%u", f.month); out += buf; }
                i += r; break; }
            case L'd': { int r = run(L'd');
                if (r >= 4)      out += kWeekdayLong[wd];
                else if (r == 3) out += kWeekdayShort[wd];
                else { swprintf(buf, 16, r == 2 ? L"%02u" : L"%u", f.day); out += buf; }
                i += r; break; }
            case L'a': { int r = run(L'a');
                out += kWeekdayJp[wd];
                if (r >= 4) out += L"曜日";
                i += r; break; }
            default: out += ch; ++i; break;
        }
    }
    return out;
}

std::wstring WidenDigits(std::wstring s, wchar_t wide) {
    for (auto& ch : s)
        if (ch >= L'0' && ch <= L'9') ch = wide;
    return s;
}

// ---- worst-case strings for sizing -----------------------------------------

std::vector<std::wstring> MeasurementTimeCandidates(const Config& cfg) {
    LocalTimeFields f;
    f.valid = true;
    f.year = 2000; f.month = 12; f.day = 28; f.weekday = 3;
    f.minute = 59; f.second = 59;

    std::vector<std::wstring> out;
    if (cfg.hourFormat == 12) {
        // 12-hour never exceeds two digits, but AM and PM are different glyphs.
        f.hour = 0;  out.push_back(FormatTime(f, cfg));   // 12:59 AM
        f.hour = 12; out.push_back(FormatTime(f, cfg));   // 12:59 PM
    } else {
        f.hour = 23; out.push_back(FormatTime(f, cfg));   // 23:59
    }
    return out;
}

std::vector<std::wstring> MeasurementDateCandidates(const std::wstring& pat) {
    // Which token families change the width? A month token is sensitive to the
    // month whether it is numeric (1 vs 12) or a name (May vs September), so a
    // run of any length counts. A `d` run is only weekday-sensitive from three
    // characters up -- `d` and `dd` are the day number, and the day is pinned
    // below. Sweeping weekdays for those would just measure duplicates.
    bool varyMonth = false, varyWeekday = false;
    for (size_t i = 0; i < pat.size();) {
        const wchar_t ch = pat[i];
        size_t run = 1;
        while (i + run < pat.size() && pat[i + run] == ch) ++run;
        if (ch == L'M') varyMonth = true;
        else if (ch == L'a') varyWeekday = true;
        else if (ch == L'd' && run >= 3) varyWeekday = true;
        i += run;
    }

    LocalTimeFields f;
    f.valid = true;
    f.year = 2000;   // four digits for yyyy, two for yy
    f.day = 28;      // two digits for d and dd

    std::vector<std::wstring> out;
    const unsigned lastMonth = varyMonth ? 12u : 1u;
    const unsigned lastWeekday = varyWeekday ? 6u : 0u;
    for (unsigned mo = 1; mo <= lastMonth; ++mo) {
        for (unsigned wd = 0; wd <= lastWeekday; ++wd) {
            f.month = mo;
            f.weekday = wd;
            out.push_back(FormatDatePattern(f, pat));
        }
    }
    return out;
}
