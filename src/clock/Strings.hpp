// User-visible text of the gadget, in Japanese and English.
//
// The language follows the Windows display language: Japanese when it is
// Japanese, English otherwise. It is picked once, on first use; there is no
// setting for it. Both tables live in the exe, so no resource DLL is loaded.
#pragma once
#include <windows.h>

struct UiStrings {
    const wchar_t* d2dInitFailed;

    // Reported once a config reload has kept failing.
    const wchar_t* configParseFailed;
    const wchar_t* configUnreadable;
    const wchar_t* configKeepingLast;

    const wchar_t* settingsAppMissing;

    // A menu toggle could not be written to config.json.
    const wchar_t* saveParseFailed;
    const wchar_t* saveUnreadable;
    const wchar_t* saveWriteFailed;

    const wchar_t* menuSettings;
    const wchar_t* menuTopmost;
    const wchar_t* menuLock;
    const wchar_t* menuExit;
};

extern const UiStrings kUiJa;
extern const UiStrings kUiEn;

bool IsJapaneseUiLang(LANGID lang);

// The table for the current user's display language.
const UiStrings& Ui();
