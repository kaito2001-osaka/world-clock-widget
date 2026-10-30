using System.Globalization;

namespace WorldClockSettings;

// User-visible text, in Japanese and English. The language follows the Windows
// display language (Japanese when it is Japanese, English otherwise), the same
// rule the gadget uses. Kept in code rather than .resx so the app stays a
// single exe with no satellite assemblies to install.
public static class Strings
{
    private static readonly bool Ja =
        CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "ja";

    private static string T(string ja, string en) => Ja ? ja : en;

    public static string WindowTitle => T("World Clock の設定", "World Clock Settings");

    // ---- Cities ----
    public static string Cities => T("都市", "Cities");
    public static string UnknownTzTip => T(
        "このタイムゾーン名は認識できないため、ガジェットには --:-- と表示されます",
        "This time zone name is not recognized, so the gadget shows --:--");
    public static string CityPickerTip => T(
        "主要都市から選ぶか、IANA タイムゾーン名を直接入力できます (例: Asia/Tokyo)",
        "Pick a major city, or type an IANA time zone name (e.g. Asia/Tokyo)");
    public static string Add => T("追加", "Add");
    public static string Remove => T("削除", "Remove");
    public static string CannotAddCity => T("都市を追加できません", "Can't add the city");
    public static string ReorderHint => T("選択した都市を上下に並べ替え", "Move the selected city up or down");

    // ---- Display ----
    public static string Display => T("表示", "Display");
    public static string DisplayMode => T("表示モード", "Display mode");
    public static string Digital => T("デジタル", "Digital");
    public static string Analog => T("アナログ", "Analog");
    public static string AnalogWarningTitle => T("アナログ表示", "Analog display");
    public static string AnalogWarningMessage => T(
        "都市数が多いとアナログ盤は窮屈になります。少数の都市での利用を推奨します。",
        "Analog faces get cramped with many cities. Best with just a few.");
    public static string Arrangement => T("並べ方", "Arrangement");
    public static string Vertical => T("縦並び", "Vertical");
    public static string Horizontal => T("横並び", "Horizontal");
    public static string Hour24 => T("24時間表示 (オフ = 12時間)", "24-hour clock (off = 12-hour)");
    public static string ShowDate => T("日付・曜日を表示", "Show date and weekday");
    public static string DateFormat => T("日付の書式", "Date format");
    public static string DateFormatTip => T(
        "プリセットから選ぶか、独自の書式を直接入力できます。",
        "Pick a preset, or type your own format.");
    public static string PreviewPrefix => T("プレビュー: ", "Preview: ");
    public static string TokenHelp => T(
        "トークン: yyyy/yy 年, MMMM/MMM/MM/M 月, dddd/ddd/dd/d 日・曜日, aaaa/aaa 日本語曜日(金/金曜日)。その他の文字 (年 月 日 / - 空白) はそのまま表示。",
        "Tokens: yyyy/yy year, MMMM/MMM/MM/M month, dddd/ddd/dd/d day and weekday, aaaa/aaa Japanese weekday (金/金曜日). Other characters (/ - , spaces) are shown as is.");
    public static string ShowSeconds => T("秒を表示", "Show seconds");
    public static string Size => T("サイズ", "Size");
    public static string Small => T("小", "Small");
    public static string Medium => T("中", "Medium");
    public static string Large => T("大", "Large");
    public static string Opacity => T("不透明度", "Opacity");

    // ---- Window ----
    public static string Window => T("ウィンドウ", "Window");
    public static string AlwaysOnTop => T("常に最前面に表示", "Always on top");
    public static string LockPosition => T("位置を固定 (ドラッグ移動を無効化)", "Lock position (disable dragging)");
    public static string LaunchAtStartup => T("Windows 起動時に自動的に開始", "Start automatically with Windows");

    // ---- Buttons ----
    public static string Cancel => T("キャンセル", "Cancel");
    public static string Apply => T("適用", "Apply");

    // ---- Messages ----
    public static string ConfigInvalid => T(
        "config.json に構文エラーがあるため、読み込めませんでした。",
        "config.json could not be loaded because it has a syntax error.");
    public static string ConfigUnreadable => T(
        "config.json を読み込めませんでした（他のアプリが使用中か、アクセスが拒否されています）。\n" +
        "いったん閉じて開き直すと、読み込めることがあります。",
        "config.json could not be loaded (another app is using it, or access was denied).\n" +
        "Closing and reopening this window may help.");
    public static string ConfigNotLoaded(string reason, string backupPath) => T(
        $"{reason}\n既定の設定を表示しています。\n\n" +
        "このまま保存すると config.json は上書きされます。" +
        $"上書き前のファイルは次の場所に残します:\n{backupPath}",
        $"{reason}\nThe default settings are shown.\n\n" +
        "Saving now overwrites config.json. " +
        $"The previous file is kept here:\n{backupPath}");
    public static string NeedOneCity => T("少なくとも1つの都市が必要です。", "Add at least one city.");
    public static string SaveFailed(string error) => T(
        $"設定の保存に失敗しました:\n{error}",
        $"Failed to save the settings:\n{error}");
    public static string UnknownTimeZone(string text) => T(
        $"\"{text}\" はタイムゾーン名として認識できません。" +
        "一覧から選ぶか、Asia/Tokyo のような IANA 名を入力してください。",
        $"\"{text}\" is not a recognized time zone name. " +
        "Pick one from the list, or type an IANA name such as Asia/Tokyo.");
    public static string UnexpectedError(string error) => T(
        $"予期しないエラーが発生したため、設定を終了します:\n{error}",
        $"An unexpected error occurred, so Settings will close:\n{error}");
}
