#include "Strings.hpp"

const UiStrings kUiJa = {
    L"Direct2D の初期化に失敗しました。",

    L"config.json に構文エラーがあるため、読み込めませんでした。\n",
    L"config.json を読み込めませんでした"
    L"（他のアプリが使用中か、アクセスが拒否されています）。\n",
    L"読み込めるようになるまで、直前に読み込めた設定"
    L"（無い場合は既定の設定）で表示します。config.json は変更しません。",

    L"設定の保存先 (AppData) フォルダが見つかりませんでした。\n"
    L"既定の設定で表示します。config.json の読み込みと保存、"
    L"位置の保存は行いません。",
    L"config.json の変更を監視できませんでした。\n"
    L"設定アプリで保存した変更は、ガジェットを再起動すると反映されます。",

    L"設定アプリ (WorldClockSettings.exe) が見つかりませんでした。\n"
    L"config.json を直接編集してください:\n"
    L"%APPDATA%\\WorldClockGadget\\config.json",

    L"config.json に構文エラーがあるため、この変更を保存できませんでした。\n"
    L"ファイルを修正するか、設定アプリから保存し直してください。",
    L"config.json を読み込めないため、この変更を保存できませんでした。\n"
    L"しばらくしてから、もう一度お試しください。",
    L"config.json に変更を保存できませんでした。\n"
    L"ファイルが読み取り専用でないか確認し、もう一度お試しください。",

    L"設定…",
    L"常に最前面",
    L"位置を固定",
    L"終了",
};

const UiStrings kUiEn = {
    L"Failed to initialize Direct2D.",

    L"config.json could not be loaded because it has a syntax error.\n",
    L"config.json could not be loaded"
    L" (another app is using it, or access was denied).\n",
    L"Until it can be loaded, the last settings that loaded"
    L" (or the defaults, if none did) are shown. config.json is not changed.",

    L"The settings folder (AppData) could not be found.\n"
    L"The default settings are shown. config.json is not read or saved,"
    L" and the window position is not saved.",
    L"Changes to config.json could not be watched.\n"
    L"Changes saved in the settings app take effect when the gadget restarts.",

    L"The settings app (WorldClockSettings.exe) was not found.\n"
    L"Edit config.json directly:\n"
    L"%APPDATA%\\WorldClockGadget\\config.json",

    L"This change could not be saved because config.json has a syntax error.\n"
    L"Fix the file, or save again from the settings app.",
    L"This change could not be saved because config.json could not be read.\n"
    L"Please try again in a moment.",
    L"The change could not be saved to config.json.\n"
    L"Check that the file is not read-only, then try again.",

    L"Settings…",
    L"Always on top",
    L"Lock position",
    L"Exit",
};

bool IsJapaneseUiLang(LANGID lang) {
    return PRIMARYLANGID(lang) == LANG_JAPANESE;
}

const UiStrings& Ui() {
    static const UiStrings& ui =
        IsJapaneseUiLang(GetUserDefaultUILanguage()) ? kUiJa : kUiEn;
    return ui;
}
