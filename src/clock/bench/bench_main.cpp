// WorldClockBench -- measures the render path and checks its output.
//
//   WorldClockBench [--frames N]            per-phase timing table
//   WorldClockBench --write-golden [--golden-dir DIR]
//   WorldClockBench --check-golden [--golden-dir DIR]
//
// The gadget draws at 1 Hz, so its process CPU time over a few minutes is
// noise. This drives the real Renderer against a hidden layered window with
// synthetic time, as fast as it will go, and times each phase of a frame with
// QPC through the WORLDCLOCK_BENCH hooks in Renderer.cpp.
//
// Goldens are the presented DIB (premultiplied BGRA, alpha included) for a
// fixed time and config. A screen capture cannot see alpha; these can. They
// depend on the installed fonts and DirectWrite, so they are generated
// locally from main and checked on a branch rather than committed.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "BenchStats.hpp"
#include "Config.hpp"
#include "Renderer.hpp"

using namespace std::chrono;

// ---- heap allocation counting ----------------------------------------------
// Replacing the global operator new counts every allocation made through it in
// this exe: ours, std containers', the CRT's. Direct2D and DirectWrite live in
// their own DLLs with their own heaps and are not counted -- which is the
// point: the number is the allocations our code could avoid.

namespace {
std::atomic<unsigned long long> g_allocs{ 0 };

void* CountedMalloc(size_t n) {
    ++g_allocs;
    return std::malloc(n ? n : 1);
}
void* CountedAlignedMalloc(size_t n, std::align_val_t a) {
    ++g_allocs;
    return _aligned_malloc(n ? n : 1, static_cast<size_t>(a));
}
} // namespace

void* operator new(size_t n) {
    if (void* p = CountedMalloc(n)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) {
    if (void* p = CountedMalloc(n)) return p;
    throw std::bad_alloc();
}
void* operator new(size_t n, const std::nothrow_t&) noexcept   { return CountedMalloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return CountedMalloc(n); }
void operator delete(void* p) noexcept                           { std::free(p); }
void operator delete[](void* p) noexcept                         { std::free(p); }
void operator delete(void* p, size_t) noexcept                   { std::free(p); }
void operator delete[](void* p, size_t) noexcept                 { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept    { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept  { std::free(p); }

void* operator new(size_t n, std::align_val_t a) {
    if (void* p = CountedAlignedMalloc(n, a)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n, std::align_val_t a) {
    if (void* p = CountedAlignedMalloc(n, a)) return p;
    throw std::bad_alloc();
}
void* operator new(size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return CountedAlignedMalloc(n, a);
}
void* operator new[](size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return CountedAlignedMalloc(n, a);
}
void operator delete(void* p, std::align_val_t) noexcept                 { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept               { _aligned_free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept         { _aligned_free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept       { _aligned_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept   { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }

namespace {

// ---- configurations ---------------------------------------------------------

// 03:29:47 UTC mixes AM and PM cities, so both analog palettes are drawn, and
// no city crosses midnight for several minutes (Sao Paulo is the closest, at
// 00:29), so the date line stays put while a row is being timed. A non-zero
// second puts the second hand somewhere other than under the minute hand.
const system_clock::time_point kStart =
    sys_days{ year{ 2026 } / January / 15 } + hours{ 3 } + minutes{ 29 } + seconds{ 47 };

constexpr int kWarmupFrames = 5;   // the first frame also creates the surface

std::vector<CityEntry> Cities(int count) {
    // DefaultConfig's four, then four more spread across the day.
    std::vector<CityEntry> c = DefaultConfig().cities;
    const CityEntry more[] = {
        { "Sydney",    "Australia/Sydney" },
        { "Dubai",     "Asia/Dubai" },
        { "São Paulo", "America/Sao_Paulo" },
        { "Kolkata",   "Asia/Kolkata" },
    };
    for (const CityEntry& e : more) c.push_back(e);
    c.resize((size_t)count);
    return c;
}

struct Case {
    DisplayMode mode;
    LayoutDir   layout;
    bool        showSeconds;
    SizeClass   size;
    UINT        dpi;
    int         cities;
    int         hourFormat;
};

const char* ModeName(DisplayMode m)   { return m == DisplayMode::Analog ? "analog" : "digital"; }
const char* LayoutName(LayoutDir l)   { return l == LayoutDir::Horizontal ? "horizontal" : "vertical"; }
const char* SizeName(SizeClass s) {
    switch (s) {
        case SizeClass::Small: return "small";
        case SizeClass::Large: return "large";
        default:               return "medium";
    }
}

Config MakeConfig(const Case& c) {
    Config cfg = DefaultConfig();
    cfg.cities      = Cities(c.cities);
    cfg.displayMode = c.mode;
    cfg.layout      = c.layout;
    cfg.showSeconds = c.showSeconds;
    cfg.size        = c.size;
    cfg.hourFormat  = c.hourFormat;
    return cfg;
}

const DisplayMode kModes[]   = { DisplayMode::Digital, DisplayMode::Analog };
const LayoutDir   kLayouts[] = { LayoutDir::Vertical, LayoutDir::Horizontal };
const bool        kSeconds[] = { false, true };
const SizeClass   kSizes[]   = { SizeClass::Small, SizeClass::Medium, SizeClass::Large };
const UINT        kDpis[]    = { 96, 144, 192 };

// The timing matrix: every display option that changes the work per frame.
std::vector<Case> TimingCases() {
    std::vector<Case> out;
    for (DisplayMode m : kModes)
        for (LayoutDir l : kLayouts)
            for (bool s : kSeconds)
                for (SizeClass z : kSizes)
                    for (UINT d : kDpis)
                        for (int n : { 4, 8 })
                            out.push_back({ m, l, s, z, d, n, 24 });
    return out;
}

// The golden set: four cities only (eight adds bytes, not code paths), but
// both clock formats for digital, since 12-hour text takes its own path.
std::vector<Case> GoldenCases() {
    std::vector<Case> out;
    for (DisplayMode m : kModes)
        for (LayoutDir l : kLayouts)
            for (bool s : kSeconds)
                for (SizeClass z : kSizes)
                    for (UINT d : kDpis) {
                        out.push_back({ m, l, s, z, d, 4, 24 });
                        if (m == DisplayMode::Digital) out.push_back({ m, l, s, z, d, 4, 12 });
                    }
    return out;
}

std::string GoldenName(const Case& c) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s-%s-%s-%s-%udpi", ModeName(c.mode), LayoutName(c.layout),
                  c.showSeconds ? "sec" : "nosec", SizeName(c.size), c.dpi);
    std::string name = buf;
    if (c.mode == DisplayMode::Digital) name += c.hourFormat == 12 ? "-12h" : "-24h";
    return name;
}

// ---- driving the renderer ---------------------------------------------------

HWND CreateHiddenLayeredWindow() {
    const wchar_t kClass[] = L"WorldClockBenchWindow";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    // Same styles as the gadget, never shown.
    return CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass,
                           L"WorldClockBench", WS_POPUP, 0, 0, 240, 200,
                           nullptr, nullptr, wc.hInstance, nullptr);
}

// A fresh Renderer per case, so no case's output depends on the one before.
bool Setup(Renderer& r, HWND hwnd, const Case& c) {
    if (!r.Init(hwnd)) return false;
    r.SetConfig(MakeConfig(c));
    r.OnDpiChanged(c.dpi);
    return true;
}

bool FrameComplete(const Renderer::BenchStamps& s) {
    for (LONGLONG t : s.t)
        if (t == 0) return false;
    return true;
}

struct PhaseSamples {
    std::vector<double> update, draw, copy, present, total, allocs;
};

void PrintHeader(int frames) {
    std::printf("WorldClockBench: %d frames per row after %d warm-up frames, 1 s apart from "
                "2026-01-15 03:29:47 UTC\n", frames, kWarmupFrames);
    std::printf("Times in microseconds, median / p95. Allocations: operator new calls per frame, "
                "median / max.\n\n");
    std::printf("%-7s %-10s %-5s %-6s %3s %3s %11s %6s | %15s %15s %15s %15s %15s | %9s\n",
                "mode", "layout", "sec", "size", "dpi", "cty", "surface", "KiB",
                "UpdateFrame", "Begin..EndDraw", "CopyPixels", "Present", "total", "allocs");
}

int RunTiming(HWND hwnd, int frames) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const double usPerTick = 1e6 / (double)freq.QuadPart;

    PrintHeader(frames);
    int failures = 0;
    for (const Case& c : TimingCases()) {
        Renderer r;
        if (!Setup(r, hwnd, c)) {
            std::printf("%-7s %-10s: renderer init failed\n", ModeName(c.mode), LayoutName(c.layout));
            ++failures;
            continue;
        }

        system_clock::time_point now = kStart;
        for (int i = 0; i < kWarmupFrames; ++i, now += seconds{ 1 }) r.Render(now);

        PhaseSamples ps;
        for (auto* v : { &ps.update, &ps.draw, &ps.copy, &ps.present, &ps.total, &ps.allocs })
            v->reserve((size_t)frames);   // so the bench's own bookkeeping is not counted

        int incomplete = 0;
        for (int i = 0; i < frames; ++i, now += seconds{ 1 }) {
            const unsigned long long before = g_allocs.load();
            r.Render(now);
            const unsigned long long after = g_allocs.load();

            const Renderer::BenchStamps& s = r.BenchLastFrame();
            if (!FrameComplete(s)) { ++incomplete; continue; }
            ps.update.push_back((double)(s.t[1] - s.t[0]) * usPerTick);
            ps.draw.push_back((double)(s.t[3] - s.t[2]) * usPerTick);
            ps.copy.push_back((double)(s.t[4] - s.t[3]) * usPerTick);
            ps.present.push_back((double)(s.t[5] - s.t[4]) * usPerTick);
            ps.total.push_back((double)(s.t[5] - s.t[0]) * usPerTick);
            ps.allocs.push_back((double)(after - before));
        }

        const SIZE sz = r.BenchSurface();
        char surface[24];
        std::snprintf(surface, sizeof surface, "%ldx%ld", sz.cx, sz.cy);
        std::printf("%-7s %-10s %-5s %-6s %3u %3d %11s %6lld |",
                    ModeName(c.mode), LayoutName(c.layout), c.showSeconds ? "on" : "off",
                    SizeName(c.size), c.dpi, c.cities, surface,
                    (long long)sz.cx * sz.cy * 4 / 1024);
        for (const std::vector<double>* v : { &ps.update, &ps.draw, &ps.copy, &ps.present, &ps.total })
            std::printf(" %7.1f/%7.1f", Percentile(*v, 0.5), Percentile(*v, 0.95));
        std::printf(" | %4.0f/%4.0f", Percentile(ps.allocs, 0.5), Percentile(ps.allocs, 1.0));
        if (incomplete) {
            std::printf("  %d frame(s) returned early", incomplete);
            ++failures;
        }
        std::printf("\n");
        r.Shutdown();
    }

    if (failures) std::printf("\n%d row(s) FAILED\n", failures);
    return failures ? 1 : 0;
}

// ---- goldens ----------------------------------------------------------------

// Renders the golden frame for `c` and returns it as a whole golden file.
bool RenderGolden(HWND hwnd, const Case& c, std::vector<uint8_t>& file) {
    Renderer r;
    if (!Setup(r, hwnd, c)) return false;
    r.Render(kStart);
    r.Render(kStart);   // the frame from a surface that already exists
    const SIZE sz = r.BenchSurface();
    const bool ok = FrameComplete(r.BenchLastFrame()) && r.BenchPixels();
    if (ok) {
        GoldenHeader h{ (uint32_t)sz.cx, (uint32_t)sz.cy, (uint32_t)sz.cx * 4 };
        file = EncodeGoldenHeader(h);
        const uint8_t* px = static_cast<const uint8_t*>(r.BenchPixels());
        file.insert(file.end(), px, px + (size_t)h.stride * h.height);
    }
    r.Shutdown();
    return ok;
}

bool WriteFileBytes(const std::filesystem::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
    return (bool)f;
}

bool ReadFileBytes(const std::filesystem::path& p, std::vector<uint8_t>& bytes) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

int RunGoldens(HWND hwnd, const std::filesystem::path& dir, bool write) {
    std::error_code ec;
    if (write) std::filesystem::create_directories(dir, ec);

    int failures = 0, matched = 0;
    unsigned long long totalBytes = 0;
    for (const Case& c : GoldenCases()) {
        const std::string name = GoldenName(c);
        const std::filesystem::path path = dir / (name + ".bgra");

        std::vector<uint8_t> actual;
        if (!RenderGolden(hwnd, c, actual)) {
            std::printf("FAIL  %s: render failed\n", name.c_str());
            ++failures;
            continue;
        }
        totalBytes += actual.size();

        if (write) {
            if (!WriteFileBytes(path, actual)) {
                std::printf("FAIL  %s: could not write %ls\n", name.c_str(), path.c_str());
                ++failures;
            }
            continue;
        }

        std::vector<uint8_t> expected;
        if (!ReadFileBytes(path, expected)) {
            std::printf("FAIL  %s: no golden at %ls\n", name.c_str(), path.c_str());
            ++failures;
            continue;
        }
        const auto eh = DecodeGoldenHeader(expected.data(), expected.size());
        const auto ah = DecodeGoldenHeader(actual.data(), actual.size());
        const std::filesystem::path actualPath = dir / (name + ".actual.bgra");
        if (!eh) {
            std::printf("FAIL  %s: golden file is malformed\n", name.c_str());
            ++failures;
        } else if (eh->width != ah->width || eh->height != ah->height) {
            std::printf("FAIL  %s: size %ux%u, golden %ux%u\n", name.c_str(),
                        ah->width, ah->height, eh->width, eh->height);
            WriteFileBytes(actualPath, actual);
            ++failures;
        } else {
            const BufferDiff d = CompareBuffers(expected.data() + kGoldenHeaderBytes,
                                                actual.data() + kGoldenHeaderBytes,
                                                actual.size() - kGoldenHeaderBytes);
            if (d.differingBytes == 0) {
                ++matched;
                std::filesystem::remove(actualPath, ec);   // stale from an earlier failure
                continue;
            }
            std::printf("FAIL  %s: %zu byte(s) differ, first at pixel (%zu, %zu) channel %c\n",
                        name.c_str(), d.differingBytes,
                        (d.firstOffset % ah->stride) / 4, d.firstOffset / ah->stride,
                        "BGRA"[d.firstOffset % 4]);
            WriteFileBytes(actualPath, actual);
            ++failures;
        }
    }

    const size_t count = GoldenCases().size();
    if (write)
        std::printf("Wrote %zu golden(s), %.1f MiB, to %ls\n", count - (size_t)failures,
                    (double)totalBytes / (1024.0 * 1024.0), dir.c_str());
    else
        std::printf("%d of %zu golden(s) match byte for byte (%ls)\n", matched, count, dir.c_str());
    if (failures) std::printf("%d FAILED\n", failures);
    return failures ? 1 : 0;
}

int Usage() {
    std::fprintf(stderr,
        "usage: WorldClockBench [--frames N]\n"
        "       WorldClockBench --write-golden [--golden-dir DIR]\n"
        "       WorldClockBench --check-golden [--golden-dir DIR]\n");
    return 2;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    int frames = 100;
    enum class Mode { Timing, WriteGolden, CheckGolden } mode = Mode::Timing;
    std::filesystem::path goldenDir = L"golden";

    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--frames" && i + 1 < argc) {
            frames = _wtoi(argv[++i]);
            if (frames < 1) return Usage();
        } else if (a == L"--golden-dir" && i + 1 < argc) {
            goldenDir = argv[++i];
        } else if (a == L"--write-golden") {
            mode = Mode::WriteGolden;
        } else if (a == L"--check-golden") {
            mode = Mode::CheckGolden;
        } else {
            return Usage();
        }
    }

    // Same process setup as the gadget's wWinMain.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // for WIC

    HWND hwnd = CreateHiddenLayeredWindow();
    if (!hwnd) {
        std::fprintf(stderr, "could not create the layered window\n");
        return 1;
    }

    int rc = 0;
    switch (mode) {
        case Mode::Timing:      rc = RunTiming(hwnd, frames); break;
        case Mode::WriteGolden: rc = RunGoldens(hwnd, goldenDir, true); break;
        case Mode::CheckGolden: rc = RunGoldens(hwnd, goldenDir, false); break;
    }

    DestroyWindow(hwnd);
    CoUninitialize();
    return rc;
}
