using System.IO;
using Microsoft.Win32;

namespace WorldClockSettings;

// Manages the per-user autostart entry (HKCU\...\Run) for the gadget.
// Registers the GADGET exe (sibling of this settings app), not the settings app.
public static class Startup
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "WorldClockGadget";

    // Path to the resident gadget exe (assumed next to this settings exe, as in dist\).
    public static string GadgetExePath =>
        Path.Combine(AppContext.BaseDirectory, "WorldClockGadget.exe");

    public static void Apply(bool enable)
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(RunKey, writable: true)
                            ?? Registry.CurrentUser.CreateSubKey(RunKey);
            if (key == null) return;

            if (enable)
                key.SetValue(ValueName, $"\"{GadgetExePath}\"", RegistryValueKind.String);
            else if (key.GetValue(ValueName) != null)
                key.DeleteValue(ValueName, throwOnMissingValue: false);
        }
        catch
        {
            // Autostart is best-effort; the gadget also reconciles the entry on launch.
        }
    }
}
