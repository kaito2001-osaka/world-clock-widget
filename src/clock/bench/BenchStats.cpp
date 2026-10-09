#include "BenchStats.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr uint8_t kMagic[4] = { 'W', 'C', 'G', '1' };

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

uint32_t GetU32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

} // namespace

double Percentile(std::vector<double> samples, double p) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    p = std::clamp(p, 0.0, 1.0);
    // Nearest rank: the smallest value with at least p of the sample at or
    // below it. Rank 1 for p = 0, so the result is always a real sample.
    size_t rank = (size_t)std::ceil(p * (double)samples.size());
    if (rank < 1) rank = 1;
    return samples[rank - 1];
}

std::vector<uint8_t> EncodeGoldenHeader(const GoldenHeader& h) {
    std::vector<uint8_t> out(kMagic, kMagic + 4);
    PutU32(out, h.width);
    PutU32(out, h.height);
    PutU32(out, h.stride);
    return out;
}

std::optional<GoldenHeader> DecodeGoldenHeader(const uint8_t* data, size_t size) {
    if (size < kGoldenHeaderBytes || std::memcmp(data, kMagic, 4) != 0) return std::nullopt;
    GoldenHeader h;
    h.width  = GetU32(data + 4);
    h.height = GetU32(data + 8);
    h.stride = GetU32(data + 12);
    if (h.stride < uint64_t(h.width) * 4) return std::nullopt;
    if (uint64_t(h.stride) * h.height != size - kGoldenHeaderBytes) return std::nullopt;
    return h;
}

BufferDiff CompareBuffers(const uint8_t* a, const uint8_t* b, size_t bytes) {
    BufferDiff d;
    for (size_t i = 0; i < bytes; ++i) {
        if (a[i] == b[i]) continue;
        if (d.differingBytes == 0) d.firstOffset = i;
        ++d.differingBytes;
    }
    return d;
}
