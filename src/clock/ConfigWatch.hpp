// Deciding whether a directory-change notification concerns config.json.
//
// Both writers replace the file atomically through a "config.json.tmp"
// sibling, so a substring test matches the temp file's write *and* its rename
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
// case-insensitive, so "Config.JSON" counts; "config.json.tmp" does not.
bool IsConfigFileName(const std::wstring& name);

// Walk a FILE_NOTIFY_INFORMATION chain and report whether any record names the
// config file. `transferred` bounds the walk: a record whose header or name
// would run past it is treated as the end of the buffer rather than read.
bool ContainsConfigChange(const BYTE* buffer, DWORD transferred);
