// Tests for the benchmark's statistics and golden-buffer helpers
// (bench/BenchStats.hpp).
#include "test_framework.hpp"
#include "../bench/BenchStats.hpp"

#include <numeric>

namespace {

// A whole golden file: header plus `stride * height` pixel bytes.
std::vector<uint8_t> GoldenFile(uint32_t w, uint32_t h, uint32_t stride, size_t pixelBytes) {
    std::vector<uint8_t> f = EncodeGoldenHeader({ w, h, stride });
    f.resize(f.size() + pixelBytes, 0x7F);
    return f;
}

} // namespace

// ---- Percentile -------------------------------------------------------------

TEST(percentile_empty_is_zero) {
    CHECK(Percentile({}, 0.5) == 0.0);
    CHECK(Percentile({}, 0.95) == 0.0);
}

TEST(percentile_single_sample_is_that_sample) {
    CHECK(Percentile({ 7.0 }, 0.0) == 7.0);
    CHECK(Percentile({ 7.0 }, 0.5) == 7.0);
    CHECK(Percentile({ 7.0 }, 1.0) == 7.0);
}

TEST(percentile_two_samples_nearest_rank) {
    // Nearest rank never interpolates: the median of two is the lower one.
    CHECK(Percentile({ 2.0, 1.0 }, 0.5) == 1.0);
    CHECK(Percentile({ 2.0, 1.0 }, 0.95) == 2.0);
    CHECK(Percentile({ 2.0, 1.0 }, 0.0) == 1.0);
}

TEST(percentile_hundred_samples) {
    std::vector<double> v(100);
    std::iota(v.begin(), v.end(), 1.0);   // 1..100
    CHECK(Percentile(v, 0.5) == 50.0);
    CHECK(Percentile(v, 0.95) == 95.0);
    CHECK(Percentile(v, 1.0) == 100.0);
}

TEST(percentile_sorts_unsorted_input) {
    CHECK(Percentile({ 9.0, 1.0, 5.0, 3.0, 7.0 }, 0.5) == 5.0);
    CHECK(Percentile({ 9.0, 1.0, 5.0, 3.0, 7.0 }, 0.95) == 9.0);
}

TEST(percentile_clamps_p) {
    CHECK(Percentile({ 1.0, 2.0, 3.0 }, -1.0) == 1.0);
    CHECK(Percentile({ 1.0, 2.0, 3.0 }, 2.0) == 3.0);
}

// ---- golden header ----------------------------------------------------------

TEST(golden_header_is_sixteen_bytes) {
    CHECK_EQ(EncodeGoldenHeader({ 3, 2, 12 }).size(), kGoldenHeaderBytes);
}

TEST(golden_header_round_trips) {
    const auto f = GoldenFile(300, 200, 1200, 1200 * 200);
    const auto h = DecodeGoldenHeader(f.data(), f.size());
    CHECK(h.has_value());
    if (!h) return;
    CHECK_EQ(h->width, 300u);
    CHECK_EQ(h->height, 200u);
    CHECK_EQ(h->stride, 1200u);
}

TEST(golden_header_too_short_is_rejected) {
    const auto f = EncodeGoldenHeader({ 0, 0, 0 });
    CHECK(!DecodeGoldenHeader(f.data(), f.size() - 1).has_value());
    CHECK(!DecodeGoldenHeader(f.data(), 0).has_value());
}

TEST(golden_header_wrong_magic_is_rejected) {
    auto f = GoldenFile(1, 1, 4, 4);
    f[3] = '2';
    CHECK(!DecodeGoldenHeader(f.data(), f.size()).has_value());
}

TEST(golden_header_pixel_count_must_match) {
    // One byte short, one byte over: a truncated or padded dump.
    auto shortF = GoldenFile(2, 2, 8, 15);
    CHECK(!DecodeGoldenHeader(shortF.data(), shortF.size()).has_value());
    auto longF = GoldenFile(2, 2, 8, 17);
    CHECK(!DecodeGoldenHeader(longF.data(), longF.size()).has_value());
}

TEST(golden_header_stride_narrower_than_row_is_rejected) {
    auto f = GoldenFile(4, 1, 15, 15);
    CHECK(!DecodeGoldenHeader(f.data(), f.size()).has_value());
}

TEST(golden_header_empty_surface_is_accepted) {
    auto f = GoldenFile(0, 0, 0, 0);
    CHECK(DecodeGoldenHeader(f.data(), f.size()).has_value());
}

// ---- CompareBuffers ---------------------------------------------------------

TEST(compare_identical_buffers) {
    const std::vector<uint8_t> a(64, 0x10);
    CHECK_EQ(CompareBuffers(a.data(), a.data(), a.size()).differingBytes, size_t(0));
}

TEST(compare_reports_first_offset_and_count) {
    std::vector<uint8_t> a(64, 0x10), b = a;
    b[37] = 0x11;
    b[50] = 0x00;
    const BufferDiff d = CompareBuffers(a.data(), b.data(), a.size());
    CHECK_EQ(d.differingBytes, size_t(2));
    CHECK_EQ(d.firstOffset, size_t(37));
}

TEST(compare_sees_alpha_only_difference) {
    // BGRA: the alpha byte of pixel 2 is offset 11. A screen-capture diff
    // cannot see this; the byte comparison must.
    std::vector<uint8_t> a(16, 0xFF), b = a;
    b[11] = 0xFE;
    const BufferDiff d = CompareBuffers(a.data(), b.data(), a.size());
    CHECK_EQ(d.differingBytes, size_t(1));
    CHECK_EQ(d.firstOffset, size_t(11));
}

TEST(compare_zero_bytes) {
    const uint8_t x = 0;
    CHECK_EQ(CompareBuffers(&x, &x, 0).differingBytes, size_t(0));
}
