using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace WorldClockSettings;

// Mirrors the config.json schema consumed by the C++ gadget. The settings app
// OWNS config.json; it never touches state.json (owned by the gadget).
public sealed class CityModel
{
    [JsonPropertyName("label")] public string Label { get; set; } = "";
    [JsonPropertyName("tz")]    public string Tz { get; set; } = "";

    [JsonIgnore] public string Display => string.IsNullOrWhiteSpace(Label) ? Tz : $"{Label}  ({Tz})";

    // The gadget shows --:-- for a zone it cannot resolve; the city list flags
    // such entries (e.g. hand-edited into config.json) so the user can see why.
    [JsonIgnore] public bool HasUnknownTz => !TimeZoneIds.IsValid(Tz);
}

public enum ConfigLoadStatus
{
    Ok,          // parsed from config.json
    Missing,     // no file yet
    Unreadable,  // exists but could not be read (locked, access denied)
    Invalid,     // read, but not a config this app can parse
}

public sealed class ConfigModel
{
    [JsonPropertyName("cities")]       public List<CityModel> Cities { get; set; } = new();
    [JsonPropertyName("displayMode")]  public string DisplayMode { get; set; } = "digital"; // digital|analog
    [JsonPropertyName("layout")]       public string Layout { get; set; } = "vertical";     // vertical|horizontal
    [JsonPropertyName("hourFormat")]   public int HourFormat { get; set; } = 24;            // 12|24
    [JsonPropertyName("showDate")]     public bool ShowDate { get; set; } = true;
    [JsonPropertyName("dateFormat")]   public string DateFormat { get; set; } = "ddd, MMM d";
    [JsonPropertyName("showSeconds")]  public bool ShowSeconds { get; set; } = false;
    [JsonPropertyName("size")]         public string Size { get; set; } = "medium";         // small|medium|large
    [JsonPropertyName("opacity")]      public int Opacity { get; set; } = 85;               // 0..100
    [JsonPropertyName("alwaysOnTop")]  public bool AlwaysOnTop { get; set; } = false;
    [JsonPropertyName("lockPosition")] public bool LockPosition { get; set; } = false;
    [JsonPropertyName("launchAtStartup")] public bool LaunchAtStartup { get; set; } = false;
    [JsonPropertyName("theme")]        public string Theme { get; set; } = "dark";

    // The gadget's limits (kMaxLabelChars and friends in Config.hpp): it sizes
    // its panel from the longest strings of every city, so nothing longer or
    // more may reach it (#54). File parsing counts code points like the gadget;
    // the editor's UTF-16 limit is conservatively no larger.
    public const int MaxLabelLength = 64;
    public const int MaxDateFormatLength = 64;
    public const int MaxCities = 32;

    [JsonIgnore] public List<string> CorrectedFields { get; } = new();

    private static readonly JsonSerializerOptions Opts = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never,
    };

    public static string ConfigDir
    {
        get
        {
            var dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "WorldClockGadget");
            Directory.CreateDirectory(dir);
            return dir;
        }
    }

    public static string ConfigPath => Path.Combine(ConfigDir, "config.json");

    public static ConfigModel Default()
    {
        return new ConfigModel
        {
            Cities = new List<CityModel>
            {
                new() { Label = "Tokyo",    Tz = "Asia/Tokyo" },
                new() { Label = "Portland", Tz = "America/Los_Angeles" },
                new() { Label = "London",   Tz = "Europe/London" },
                new() { Label = "New York", Tz = "America/New_York" },
            }
        };
    }

    // Anything other than Ok comes back with the defaults, and the status says
    // so: the caller must not save those over a file that still holds the
    // user's settings without telling them first.
    public static ConfigModel Load(out ConfigLoadStatus status)
    {
        string json;
        try
        {
            if (!File.Exists(ConfigPath))
            {
                status = ConfigLoadStatus.Missing;
                return Default();
            }
            json = File.ReadAllText(ConfigPath);
        }
        catch
        {
            status = ConfigLoadStatus.Unreadable;
            return Default();
        }

        var model = Parse(json);
        if (model != null)
        {
            status = ConfigLoadStatus.Ok;
            return model;
        }
        status = ConfigLoadStatus.Invalid;
        return Default();
    }

    // Match ParseConfig in the gadget: a bad field must not discard good cities.
    // Kept apart from Load so tests never touch the user's config.json.
    internal static ConfigModel? Parse(string json)
    {
        try
        {
            using var document = JsonDocument.Parse(json);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object) return null;
            var model = Default();
            var reader = new ConfigReader(model.CorrectedFields);
            var cities = reader.Find(root, "cities");
            if (cities.ValueKind == JsonValueKind.Array)
            {
                var list = new List<CityModel>();
                int index = 0;
                foreach (var city in cities.EnumerateArray())
                {
                    if (list.Count == MaxCities) break;
                    string path = $"cities[{index++}]";
                    if (city.ValueKind != JsonValueKind.Object)
                    {
                        reader.Correct(path);
                        continue;
                    }
                    string label = reader.String(city, "label", "", path + ".");
                    string tz = reader.String(city, "tz", "", path + ".");
                    if (tz.Length == 0) { reader.Correct(path + ".tz"); continue; }
                    list.Add(new CityModel { Label = Truncate(label, MaxLabelLength), Tz = tz });
                }
                if (list.Count != 0) model.Cities = list;
            }
            else if (cities.ValueKind != JsonValueKind.Undefined) reader.Correct("cities");

            string mode = reader.String(root, "displayMode", "digital");
            model.DisplayMode = mode == "analog" ? "analog" : "digital";
            string layout = reader.String(root, "layout", "vertical");
            model.Layout = layout == "horizontal" ? "horizontal" : "vertical";
            model.HourFormat = reader.Int(root, "hourFormat", 24) == 12 ? 12 : 24;
            model.ShowDate = reader.Bool(root, "showDate", true);
            string date = reader.String(root, "dateFormat", "ddd, MMM d");
            model.DateFormat = Truncate(date.Length == 0 ? "ddd, MMM d" : date, MaxDateFormatLength);
            model.ShowSeconds = reader.Bool(root, "showSeconds", false);
            string size = reader.String(root, "size", "medium");
            model.Size = size is "small" or "large" ? size : "medium";
            model.Opacity = Math.Clamp(reader.Int(root, "opacity", 85), 10, 100);
            model.AlwaysOnTop = reader.Bool(root, "alwaysOnTop", false);
            model.LockPosition = reader.Bool(root, "lockPosition", false);
            model.LaunchAtStartup = reader.Bool(root, "launchAtStartup", false);
            model.Theme = reader.String(root, "theme", "dark");
            return model;
        }
        catch (JsonException)
        {
            return null;
        }
        catch (InvalidOperationException)
        {
            // System.Text.Json cannot decode an unpaired surrogate escape.
            return null;
        }
    }

    private sealed class ConfigReader(List<string> corrected)
    {
        public void Correct(string path)
        {
            if (!corrected.Contains(path)) corrected.Add(path);
        }

        // TryGetProperty chooses the last duplicate; the gadget chooses the first.
        public JsonElement Find(JsonElement parent, string key)
        {
            foreach (var property in parent.EnumerateObject())
                if (property.NameEquals(key)) return property.Value;
            return default;
        }

        public string String(JsonElement parent, string key, string fallback, string prefix = "")
        {
            var value = Find(parent, key);
            if (value.ValueKind == JsonValueKind.String) return value.GetString()!;
            if (value.ValueKind != JsonValueKind.Undefined) Correct(prefix + key);
            return fallback;
        }

        public bool Bool(JsonElement parent, string key, bool fallback)
        {
            var value = Find(parent, key);
            if (value.ValueKind is JsonValueKind.True or JsonValueKind.False) return value.GetBoolean();
            if (value.ValueKind != JsonValueKind.Undefined) Correct(key);
            return fallback;
        }

        public int Int(JsonElement parent, string key, int fallback)
        {
            var value = Find(parent, key);
            // Use the gadget's double conversion and truncation bounds, including
            // fractions just below INT_MIN that still truncate into range.
            if (value.ValueKind == JsonValueKind.Number && value.TryGetDouble(out double number)
                && number > -2147483649.0 && number < 2147483648.0)
                return (int)number;
            if (value.ValueKind != JsonValueKind.Undefined) Correct(key);
            return fallback;
        }
    }

    // Match TruncateUtf8's code-point limit without splitting a surrogate pair.
    private static string Truncate(string s, int max)
    {
        int end = 0, count = 0;
        while (end < s.Length && count++ < max)
            end += char.IsSurrogatePair(s, end) ? 2 : 1;
        return s[..end];
    }

    public static string BackupPath => ConfigPath + ".bak";

    // Keeps a copy of a config.json that could not be loaded before it is
    // overwritten, so a hand edit with a typo is never simply lost.
    public static void BackUpExistingFile()
    {
        if (File.Exists(ConfigPath)) File.Copy(ConfigPath, BackupPath, overwrite: true);
    }

    // The gadget writes config.json too (menu toggles). A replace can collide
    // with its replace or with a reader that has the file open; those clear
    // within milliseconds, so a few short retries ride them out.
    private const int ReplaceAttempts = 5;
    private const int ReplaceRetryMs = 50;

    // Atomic write: write to a temp file then replace, so the gadget's file
    // watcher never observes a half-written config. The temp name is ours
    // alone; the gadget replaces through "config.json.gadget.tmp".
    public void Save()
    {
        var json = JsonSerializer.SerializeToUtf8Bytes(this, Opts);
        var path = ConfigPath;
        var tmp = path + ".settings.tmp";
        // Flush to disk before the replace: otherwise a crash or power cut can
        // commit the rename while the data is still cached, leaving
        // config.json empty or zero-filled.
        try
        {
            using var fs = new FileStream(tmp, FileMode.Create, FileAccess.Write, FileShare.None);
            fs.Write(json);
            fs.Flush(flushToDisk: true);
        }
        catch
        {
            try { File.Delete(tmp); } catch { }
            throw;
        }
        for (int attempt = 1; ; attempt++)
        {
            try
            {
                if (File.Exists(path)) File.Replace(tmp, path, null);
                else File.Move(tmp, path);
                return;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException
                                       && attempt < ReplaceAttempts)
            {
                Thread.Sleep(ReplaceRetryMs);
            }
            catch
            {
                try { File.Delete(tmp); } catch { }
                throw;
            }
        }
    }

    // Three-way merge for a save. `baseline` is what the window last loaded or
    // saved, `edited` is what it shows now, `disk` is config.json as it is now.
    // A field the user did not change in the window takes the file's value, so
    // a change made elsewhere since (the gadget's menu toggles) is not reverted.
    public static ConfigModel Merge(ConfigModel baseline, ConfigModel edited, ConfigModel disk)
    {
        T Pick<T>(Func<ConfigModel, T> f) =>
            EqualityComparer<T>.Default.Equals(f(edited), f(baseline)) ? f(disk) : f(edited);

        var cities = SameCities(edited.Cities, baseline.Cities) ? disk.Cities : edited.Cities;
        return new ConfigModel
        {
            Cities = cities.Select(c => new CityModel { Label = c.Label, Tz = c.Tz }).ToList(),
            DisplayMode = Pick(c => c.DisplayMode),
            Layout = Pick(c => c.Layout),
            HourFormat = Pick(c => c.HourFormat),
            ShowDate = Pick(c => c.ShowDate),
            DateFormat = Pick(c => c.DateFormat),
            ShowSeconds = Pick(c => c.ShowSeconds),
            Size = Pick(c => c.Size),
            Opacity = Pick(c => c.Opacity),
            AlwaysOnTop = Pick(c => c.AlwaysOnTop),
            LockPosition = Pick(c => c.LockPosition),
            LaunchAtStartup = Pick(c => c.LaunchAtStartup),
            Theme = Pick(c => c.Theme),
        };
    }

    private static bool SameCities(List<CityModel> a, List<CityModel> b) =>
        a.Select(c => (c.Label, c.Tz)).SequenceEqual(b.Select(c => (c.Label, c.Tz)));

    public ConfigModel Clone()
    {
        return new ConfigModel
        {
            Cities = Cities.Select(c => new CityModel { Label = c.Label, Tz = c.Tz }).ToList(),
            DisplayMode = DisplayMode,
            Layout = Layout,
            HourFormat = HourFormat,
            ShowDate = ShowDate,
            DateFormat = DateFormat,
            ShowSeconds = ShowSeconds,
            Size = Size,
            Opacity = Opacity,
            AlwaysOnTop = AlwaysOnTop,
            LockPosition = LockPosition,
            LaunchAtStartup = LaunchAtStartup,
            Theme = Theme,
        };
    }
}
