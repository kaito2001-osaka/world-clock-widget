// Deciding whether a directory-change notification concerns config.json.
//
// Both writers replace the file atomically through a temp sibling
// ("config.json.gadget.tmp" / "config.json.settings.tmp"), so a substring test matches the temp file's write *and* its rename
// as well as the real thing -- turning one save into several reloads, one of
// which reads the file before the replace has landed. The name has to match
// exactly.
//
// Split out of the watcher thread so the matching and the record walk can be
// unit tested against synthesised buffers.
#pragma once
#include <windows.h>
#include <string>

// The config file this gadget watches for, relative to the watched directory.
inline constexpr const wchar_t* kConfigFileName = L"config.json";

// Exact, case-insensitive match against kConfigFileName. NTFS is
// case-insensitive, so "Config.JSON" counts; "config.json.gadget.tmp" does not.
bool IsConfigFileName(const std::wstring& name);

// Walk a FILE_NOTIFY_INFORMATION chain and report whether any record names the
// config file. `transferred` bounds the walk: a record whose header or name
// would run past it is treated as the end of the buffer rather than read.
// A zero `transferred` is the kernel saying it overflowed and dropped the
// batch; that counts as a change, since the dropped records may have named
// the config file. The reload it causes is a no-op if nothing changed.
bool ContainsConfigChange(const BYTE* buffer, DWORD transferred);

// Whether a failed ReadDirectoryChangesW / GetOverlappedResult is the
// overflow case (ERROR_NOTIFY_ENUM_DIR) -- the handle is still good, so the
// watcher reloads and re-arms. Any other error means the handle has to be
// reopened.
bool IsOverflowError(DWORD err);

// How long the watcher waits before reopening the directory after its
// `failures`-th consecutive failure (0-based): doubling from the first delay
// up to the cap, so a directory that is gone for good costs one open attempt
// every half minute rather than a busy loop.
inline constexpr DWORD kWatchRetryFirstMs = 250;
inline constexpr DWORD kWatchRetryMaxMs   = 30000;
DWORD WatchRetryDelayMs(int failures);
