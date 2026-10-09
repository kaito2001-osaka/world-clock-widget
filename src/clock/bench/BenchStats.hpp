// Pure helpers for WorldClockBench: percentiles, the golden-buffer file
// header, and byte comparison. Kept free of Win32 / Direct2D so the unit
// tests can link them.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Nearest-rank percentile, p in [0, 1]. Takes a copy because it sorts.
// Returns 0 for an empty sample.
double Percentile(std::vector<double> samples, double p);

// A golden file is this 16-byte header followed by stride * height bytes of
// premultiplied BGRA, top-down -- the DIB exactly as UpdateLayeredWindow gets
// it, alpha included. Little-endian: "WCG1", width, height, stride (uint32).
struct GoldenHeader {
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
};
constexpr size_t kGoldenHeaderBytes = 16;

std::vector<uint8_t> EncodeGoldenHeader(const GoldenHeader& h);

// The header of a whole golden file, or nullopt when the magic is wrong or
// the pixel data is not exactly stride * height bytes.
std::optional<GoldenHeader> DecodeGoldenHeader(const uint8_t* data, size_t size);

struct BufferDiff {
    size_t differingBytes = 0;
    size_t firstOffset    = 0;   // meaningful only when differingBytes > 0
};
BufferDiff CompareBuffers(const uint8_t* a, const uint8_t* b, size_t bytes);
