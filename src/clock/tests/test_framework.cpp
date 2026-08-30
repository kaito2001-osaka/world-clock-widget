#include "test_framework.hpp"

#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

namespace t {
namespace {

struct Case { const char* name; Fn fn; };

// Function-local static: the registry is built by static initialisers across
// translation units, so it must exist the first time any of them runs.
std::vector<Case>& Registry() {
    static std::vector<Case> r;
    return r;
}

int g_checks = 0;
int g_failures = 0;
const char* g_current = "";

} // namespace

bool Add(const char* name, Fn fn) {
    Registry().push_back({ name, fn });
    return true;
}

void Fail(const char* expr, const char* file, int line, const std::string& detail) {
    ++g_failures;
    std::printf("FAIL %s\n  %s:%d\n  %s\n", g_current, file, line, expr);
    if (!detail.empty()) std::printf("%s\n", detail.c_str());
}

void CountCheck() { ++g_checks; }

void CheckTrue(bool ok, const char* expr, const char* file, int line) {
    CountCheck();
    if (!ok) Fail(expr, file, line, {});
}

std::string Show(const std::string& v) { return "\"" + v + "\""; }
std::string Show(const char* v)        { return Show(std::string(v ? v : "")); }
std::string Show(bool v)               { return v ? "true" : "false"; }
std::string Show(long long v)          { return std::to_string(v); }

std::string Show(const std::wstring& v) {
    if (v.empty()) return "\"\"";
    int n = WideCharToMultiByte(CP_UTF8, 0, v.data(), (int)v.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, v.data(), (int)v.size(), s.data(), n, nullptr, nullptr);
    return "\"" + s + "\"";
}
std::string Show(const wchar_t* v) { return Show(std::wstring(v ? v : L"")); }

int RunAll() {
    // UTF-8 console so Japanese expectations print readably on failure.
    SetConsoleOutputCP(CP_UTF8);

    for (const Case& c : Registry()) {
        g_current = c.name;
        const int before = g_failures;
        try {
            c.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::printf("FAIL %s\n  uncaught exception: %s\n", c.name, e.what());
        } catch (...) {
            ++g_failures;
            std::printf("FAIL %s\n  uncaught exception\n", c.name);
        }
        if (g_failures == before) std::printf("ok   %s\n", c.name);
    }

    std::printf("\n%d checks in %d tests, %d failure(s)\n",
                g_checks, (int)Registry().size(), g_failures);
    return g_failures == 0 ? 0 : 1;
}

} // namespace t

int main() { return t::RunAll(); }
