namespace WorldClockSettings;

// Curated "major city -> IANA tz" picker list. The user can also type any IANA
// id directly (the gadget resolves it via std::chrono / ICU). This is the
// "easy list + search" primary picker; the free-text box is the full fallback.
public static class Cities
{
    public sealed record Entry(string Label, string Tz)
    {
        public string Search => $"{Label} {Tz}".ToLowerInvariant();
        public override string ToString() => $"{Label}  —  {Tz}";
    }

    public static readonly Entry[] Major =
    {
        new("Honolulu",      "Pacific/Honolulu"),
        new("Anchorage",     "America/Anchorage"),
        new("Los Angeles",   "America/Los_Angeles"),
        new("Portland",      "America/Los_Angeles"),
        new("Vancouver",     "America/Vancouver"),
        new("Denver",        "America/Denver"),
        new("Phoenix",       "America/Phoenix"),
        new("Chicago",       "America/Chicago"),
        new("Mexico City",   "America/Mexico_City"),
        new("New York",      "America/New_York"),
        new("Toronto",       "America/Toronto"),
        new("Bogota",        "America/Bogota"),
        new("Santiago",      "America/Santiago"),
        new("Sao Paulo",     "America/Sao_Paulo"),
        new("Buenos Aires",  "America/Argentina/Buenos_Aires"),
        new("UTC",           "Etc/UTC"),
        new("London",        "Europe/London"),
        new("Lisbon",        "Europe/Lisbon"),
        new("Dublin",        "Europe/Dublin"),
        new("Madrid",        "Europe/Madrid"),
        new("Paris",         "Europe/Paris"),
        new("Berlin",        "Europe/Berlin"),
        new("Amsterdam",     "Europe/Amsterdam"),
        new("Zurich",        "Europe/Zurich"),
        new("Rome",          "Europe/Rome"),
        new("Stockholm",     "Europe/Stockholm"),
        new("Athens",        "Europe/Athens"),
        new("Helsinki",      "Europe/Helsinki"),
        new("Istanbul",      "Europe/Istanbul"),
        new("Moscow",        "Europe/Moscow"),
        new("Cairo",         "Africa/Cairo"),
        new("Johannesburg",  "Africa/Johannesburg"),
        new("Nairobi",       "Africa/Nairobi"),
        new("Lagos",         "Africa/Lagos"),
        new("Dubai",         "Asia/Dubai"),
        new("Tehran",        "Asia/Tehran"),
        new("Karachi",       "Asia/Karachi"),
        new("Mumbai",        "Asia/Kolkata"),
        new("Dhaka",         "Asia/Dhaka"),
        new("Bangkok",       "Asia/Bangkok"),
        new("Jakarta",       "Asia/Jakarta"),
        new("Singapore",     "Asia/Singapore"),
        new("Hong Kong",     "Asia/Hong_Kong"),
        new("Shanghai",      "Asia/Shanghai"),
        new("Taipei",        "Asia/Taipei"),
        new("Seoul",         "Asia/Seoul"),
        new("Tokyo",         "Asia/Tokyo"),
        new("Perth",         "Australia/Perth"),
        new("Adelaide",      "Australia/Adelaide"),
        new("Sydney",        "Australia/Sydney"),
        new("Brisbane",      "Australia/Brisbane"),
        new("Auckland",      "Pacific/Auckland"),
    };
}
