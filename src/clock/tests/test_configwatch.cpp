// Tests for the config-watcher notification matching (ConfigWatch.hpp).
#include "test_framework.hpp"
#include "ConfigWatch.hpp"

#include <climits>
#include <string>
#include <vector>

namespace {

// Build a FILE_NOTIFY_INFORMATION chain the way the kernel lays one out:
// each record is the fixed header followed by the (non NUL-terminated) name,
// with NextEntryOffset pointing at the following record and 0 on the last.
std::vector<BYTE> BuildChain(const std::vector<std::wstring>& names) {
    const size_t header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
    std::vector<BYTE> buf;
    std::vector<size_t> starts;

    for (size_t i = 0; i < names.size(); ++i) {
        starts.push_back(buf.size());
        const size_t nameBytes = names[i].size() * sizeof(wchar_t);
        // Records are DWORD-aligned in a real buffer.
        const size_t recBytes = ((header + nameBytes) + 3) & ~size_t(3);
        buf.resize(buf.size() + recBytes, 0);

        auto* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf.data() + starts[i]);
        fni->Action = FILE_ACTION_MODIFIED;
        fni->FileNameLength = (DWORD)nameBytes;
        memcpy(fni->FileName, names[i].data(), nameBytes);
        fni->NextEntryOffset = 0;
        if (i > 0) {
            auto* prev = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf.data() + starts[i - 1]);
            prev->NextEntryOffset = (DWORD)(starts[i] - starts[i - 1]);
        }
    }
    return buf;
}

bool Hit(const std::vector<std::wstring>& names) {
    std::vector<BYTE> buf = BuildChain(names);
    return ContainsConfigChange(buf.data(), (DWORD)buf.size());
}

} // namespace

// ---- name matching --------------------------------------------------------

TEST(config_name_matches_exactly) {
    CHECK_EQ(IsConfigFileName(L"config.json"), true);
}

TEST(config_name_match_is_case_insensitive) {
    // NTFS is case-insensitive, so the notification casing is not guaranteed.
    CHECK_EQ(IsConfigFileName(L"Config.JSON"), true);
    CHECK_EQ(IsConfigFileName(L"CONFIG.JSON"), true);
    CHECK_EQ(IsConfigFileName(L"cOnFiG.jSoN"), true);
}

TEST(config_name_rejects_the_atomic_write_temp_file) {
    // The bug: both writers replace through config.json.tmp, and a substring
    // match fires on the temp write *and* the rename -- several reloads per
    // save, the first reading the file before the replace has landed.
    CHECK_EQ(IsConfigFileName(L"config.json.tmp"), false);
    // Each writer now has its own temp name (#24); neither may count.
    CHECK_EQ(IsConfigFileName(L"config.json.gadget.tmp"), false);
    CHECK_EQ(IsConfigFileName(L"config.json.settings.tmp"), false);
    CHECK_EQ(IsConfigFileName(L"config.json.devbak"), false);
    CHECK_EQ(IsConfigFileName(L"config.json.bak"), false);
    CHECK_EQ(IsConfigFileName(L"config.json~"), false);
}

TEST(config_name_rejects_other_files_in_the_directory) {
    CHECK_EQ(IsConfigFileName(L"state.json"), false);   // owned by the gadget
    CHECK_EQ(IsConfigFileName(L"myconfig.json"), false);
    CHECK_EQ(IsConfigFileName(L"config.jso"), false);
    CHECK_EQ(IsConfigFileName(L"config.json.json"), false);
    CHECK_EQ(IsConfigFileName(L""), false);
    CHECK_EQ(IsConfigFileName(L"sub\\config.json"), false);  // not the watched dir
}

// ---- record walking -------------------------------------------------------

TEST(config_change_found_in_a_single_record) {
    CHECK_EQ(Hit({ L"config.json" }), true);
    CHECK_EQ(Hit({ L"state.json" }), false);
    CHECK_EQ(Hit({ L"config.json.tmp" }), false);
}

TEST(config_change_found_anywhere_in_the_chain) {
    CHECK_EQ(Hit({ L"config.json", L"state.json", L"other.txt" }), true);   // first
    CHECK_EQ(Hit({ L"state.json", L"config.json", L"other.txt" }), true);   // middle
    CHECK_EQ(Hit({ L"state.json", L"other.txt", L"config.json" }), true);   // last
    CHECK_EQ(Hit({ L"state.json", L"other.txt", L"config.json.tmp" }), false);
}

TEST(config_change_walks_a_realistic_atomic_replace) {
    // What a save actually produces: the temp file is written, then renamed
    // over the target. Only the rename onto config.json should count.
    CHECK_EQ(Hit({ L"config.json.tmp" }), false);
    CHECK_EQ(Hit({ L"config.json.tmp", L"config.json" }), true);
    CHECK_EQ(Hit({ L"config.json.gadget.tmp" }), false);
    CHECK_EQ(Hit({ L"config.json.settings.tmp" }), false);
    CHECK_EQ(Hit({ L"config.json.settings.tmp", L"config.json" }), true);
}

TEST(config_change_treats_an_overflow_as_a_change) {
    // A zero-length result means the kernel overflowed and dropped the batch.
    // It may have held the config.json save, so it has to count (#27); the
    // buffer holds nothing valid and must not be read.
    CHECK_EQ(ContainsConfigChange(nullptr, 0), true);
    std::vector<BYTE> unrelated = BuildChain({ L"state.json" });
    CHECK_EQ(ContainsConfigChange(unrelated.data(), 0), true);
}

TEST(config_change_rejects_a_missing_buffer) {
    CHECK_EQ(ContainsConfigChange(nullptr, 64), false);
}

// ---- watch failures (#27) -------------------------------------------------

TEST(watch_overflow_error_is_told_apart_from_a_broken_handle) {
    // An overflow keeps the handle; anything else means reopening it.
    CHECK_EQ(IsOverflowError(ERROR_NOTIFY_ENUM_DIR), true);
    CHECK_EQ(IsOverflowError(ERROR_ACCESS_DENIED), false);       // directory deleted
    CHECK_EQ(IsOverflowError(ERROR_INVALID_HANDLE), false);
    CHECK_EQ(IsOverflowError(ERROR_OPERATION_ABORTED), false);
    CHECK_EQ(IsOverflowError(ERROR_FILE_NOT_FOUND), false);
    CHECK_EQ(IsOverflowError(0), false);
}

TEST(watch_retry_starts_short_and_doubles) {
    CHECK_EQ(WatchRetryDelayMs(0), kWatchRetryFirstMs);
    CHECK_EQ(WatchRetryDelayMs(1), kWatchRetryFirstMs * 2);
    CHECK_EQ(WatchRetryDelayMs(2), kWatchRetryFirstMs * 4);
    for (int i = 0; i < 40; ++i)
        CHECK(WatchRetryDelayMs(i + 1) >= WatchRetryDelayMs(i));
}

TEST(watch_retry_is_capped_and_never_overflows) {
    // A directory gone for good must settle at the cap, not wrap to a tiny
    // (busy-looping) or zero delay.
    CHECK_EQ(WatchRetryDelayMs(7), kWatchRetryMaxMs);   // 250 * 128 > 30000
    CHECK_EQ(WatchRetryDelayMs(31), kWatchRetryMaxMs);
    CHECK_EQ(WatchRetryDelayMs(32), kWatchRetryMaxMs);
    CHECK_EQ(WatchRetryDelayMs(1000), kWatchRetryMaxMs);
    CHECK_EQ(WatchRetryDelayMs(INT_MAX), kWatchRetryMaxMs);
}

TEST(watch_retry_treats_a_negative_count_as_the_first_failure) {
    CHECK_EQ(WatchRetryDelayMs(-1), kWatchRetryFirstMs);
    CHECK_EQ(WatchRetryDelayMs(INT_MIN), kWatchRetryFirstMs);
}

TEST(config_change_stops_at_a_truncated_header) {
    // Fewer bytes transferred than a single header: must not read past them.
    std::vector<BYTE> buf = BuildChain({ L"config.json" });
    const DWORD header = (DWORD)offsetof(FILE_NOTIFY_INFORMATION, FileName);
    CHECK_EQ(ContainsConfigChange(buf.data(), header - 1), false);
}

TEST(config_change_stops_at_a_truncated_name) {
    // Header intact but the name runs past what was transferred.
    std::vector<BYTE> buf = BuildChain({ L"config.json" });
    const DWORD header = (DWORD)offsetof(FILE_NOTIFY_INFORMATION, FileName);
    CHECK_EQ(ContainsConfigChange(buf.data(), header + 4), false);
    // With the whole record present it is found again.
    CHECK_EQ(ContainsConfigChange(buf.data(), (DWORD)buf.size()), true);
}

TEST(config_change_ignores_records_beyond_the_transferred_bytes) {
    // The old walk trusted NextEntryOffset and ignored `transferred`, so it
    // would read a record the kernel never wrote.
    std::vector<BYTE> buf = BuildChain({ L"state.json", L"config.json" });
    auto* first = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf.data());
    const DWORD firstRecord = first->NextEntryOffset;
    CHECK(firstRecord > 0);
    // Only the first record was actually transferred.
    CHECK_EQ(ContainsConfigChange(buf.data(), firstRecord), false);
    CHECK_EQ(ContainsConfigChange(buf.data(), (DWORD)buf.size()), true);
}

TEST(config_change_survives_a_malformed_next_offset) {
    // A NextEntryOffset that would not advance, or would jump past the end,
    // must end the walk rather than loop or read out of bounds.
    std::vector<BYTE> buf = BuildChain({ L"state.json", L"config.json" });
    auto* first = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf.data());

    first->NextEntryOffset = 1;               // too small to be a record
    CHECK_EQ(ContainsConfigChange(buf.data(), (DWORD)buf.size()), false);

    first->NextEntryOffset = 0xFFFFFFFF;      // far past the buffer
    CHECK_EQ(ContainsConfigChange(buf.data(), (DWORD)buf.size()), false);
}
