#include "Startup.hpp"

#include <windows.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

void ApplyStartupRegistry(bool enable) {
    const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* kValue  = L"WorldClockGadget";

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE | KEY_QUERY_VALUE,
                      &hKey) != ERROR_SUCCESS)
        return;

    if (enable) {
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            std::wstring quoted = L"\"" + std::wstring(path) + L"\"";
            RegSetValueExW(hKey, kValue, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(quoted.c_str()),
                           (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
        }
    } else {
        RegDeleteValueW(hKey, kValue); // ERROR_FILE_NOT_FOUND is fine
    }
    RegCloseKey(hKey);
}
