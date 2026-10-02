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

        try
        {
            var model = JsonSerializer.Deserialize<ConfigModel>(json, Opts);
            if (model != null)
            {
                // Like the gadget: no usable city list means the default cities.
                model.Cities ??= new();
                if (model.Cities.Count == 0) model.Cities = Default().Cities;
                status = ConfigLoadStatus.Ok;
                return model;
            }
        }
        catch
        {
            // Syntax error or a value of the wrong type.
        }
        status = ConfigLoadStatus.Invalid;
        return Default();
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
        var json = JsonSerializer.Serialize(this, Opts);
        var path = ConfigPath;
        var tmp = path + ".settings.tmp";
        File.WriteAllText(tmp, json);
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
