// Minimal unit-test harness.
//
// This project deliberately vendors nothing -- it hand-rolls its own JSON
// parser and links the CRT statically to keep the exe dependency-free. A
// ~60-line harness matches that, so no Catch2 / doctest here.
//
// Usage:
//     TEST(format_time_24h) {
//         CHECK_EQ(FormatTime(f, cfg), L"9:05");
//         CHECK(something);
//     }
#pragma once
#include <string>
#include <vector>

namespace t {

using Fn = void (*)();

// Registers a test; returns true so it can initialise a namespace-scope bool.
bool Add(const char* name, Fn fn);

void Fail(const char* expr, const char* file, int line, const std::string& detail);

// Counts one assertion. Every CHECK_* goes through this so the summary total
// reflects assertions, not just the ones that happen to be boolean.
void CountCheck();

int  RunAll();

// ---- value printing -------------------------------------------------------
std::string Show(const std::string& v);
std::string Show(const std::wstring& v);   // rendered as UTF-8
std::string Show(const char* v);
std::string Show(const wchar_t* v);
std::string Show(bool v);
std::string Show(long long v);
inline std::string Show(int v)          { return Show((long long)v); }
inline std::string Show(unsigned v)     { return Show((long long)v); }
inline std::string Show(long v)         { return Show((long long)v); }
inline std::string Show(unsigned long v){ return Show((long long)v); }
inline std::string Show(size_t v)       { return Show((long long)v); }

template <class A, class B>
void CheckEq(const A& a, const B& b, const char* expr, const char* file, int line) {
    CountCheck();
    if (!(a == b))
        Fail(expr, file, line, "  actual:   " + Show(a) + "\n  expected: " + Show(b));
}

void CheckTrue(bool ok, const char* expr, const char* file, int line);

} // namespace t

#define TEST(name)                                                     \
    static void name();                                                \
    static const bool t_reg_##name = ::t::Add(#name, &name);           \
    static void name()

#define CHECK(expr)        ::t::CheckTrue((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b)     ::t::CheckEq((a), (b), #a " == " #b, __FILE__, __LINE__)

// Asserts that `expr` throws anything at all.
#define CHECK_THROWS(expr)                                             \
    do {                                                               \
        bool threw_ = false;                                           \
        try { (void)(expr); } catch (...) { threw_ = true; }           \
        ::t::CheckTrue(threw_, #expr " throws", __FILE__, __LINE__);    \
    } while (0)
