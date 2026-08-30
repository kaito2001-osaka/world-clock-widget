#include "ConfigWatch.hpp"

#include <cstddef>
#include <string.h>

namespace {
// Size of the fixed part of the record, i.e. everything before the variable
// length FileName. sizeof() would over-count by the declared FileName[1].
constexpr DWORD kHeaderBytes = (DWORD)offsetof(FILE_NOTIFY_INFORMATION, FileName);
} // namespace

bool IsConfigFileName(const std::wstring& name) {
    return _wcsicmp(name.c_str(), kConfigFileName) == 0;
}

bool ContainsConfigChange(const BYTE* buffer, DWORD transferred) {
    // A zero-length result means the kernel dropped notifications (buffer
    // overflow). There is nothing to parse; the caller re-arms and moves on.
    if (!buffer || transferred == 0) return false;

    DWORD offset = 0;
    for (;;) {
        if (transferred - offset < kHeaderBytes) break;
        const auto* fni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer + offset);

        // The name must fit in what was actually transferred; a truncated
        // record ends the walk rather than being read past the buffer.
        const DWORD nameBytes = fni->FileNameLength;
        if (transferred - offset - kHeaderBytes < nameBytes) break;

        std::wstring name(fni->FileName, nameBytes / sizeof(wchar_t));
        if (IsConfigFileName(name)) return true;

        const DWORD next = fni->NextEntryOffset;
        if (next == 0) break;
        // Guard against a malformed offset that would not advance or would
        // wrap past the end of the buffer.
        if (next < kHeaderBytes || transferred - offset <= next) break;
        offset += next;
    }
    return false;
}
