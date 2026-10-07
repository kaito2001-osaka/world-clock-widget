namespace WorldClockSettings;

// Checks zone ids against what the gadget can resolve. The gadget looks names
// up with std::chrono::locate_zone, which uses the same system ICU as .NET but
// accepts only IANA names, spelled with their exact case.
public static class TimeZoneIds
{
    // Resolves `id` to the IANA name the gadget accepts. .NET also finds Windows
    // ids ("Tokyo Standard Time"), which the gadget does not, so those fail
    // here. It matches IANA names case-insensitively and reports the canonical
    // spelling in tz.Id, so a lowercase entry can be stored as the gadget needs.
    public static bool TryNormalize(string id, out string canonical)
    {
        canonical = "";
        if (string.IsNullOrWhiteSpace(id)) return false;
        if (!TimeZoneInfo.TryFindSystemTimeZoneById(id, out var tz) || !tz.HasIanaId)
            return false;
        canonical = tz.Id;
        return true;
    }

    // True when the gadget can resolve `id` exactly as written.
    public static bool IsValid(string id) =>
        TryNormalize(id, out var canonical) && string.Equals(canonical, id, StringComparison.Ordinal);
}
