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

    // Atomic write: write to a temp file then replace, so the gadget's file
    // watcher never observes a half-written config.
    public void Save()
    {
        var json = JsonSerializer.Serialize(this, Opts);
        var path = ConfigPath;
        var tmp = path + ".tmp";
        File.WriteAllText(tmp, json);
        if (File.Exists(path)) File.Replace(tmp, path, null);
        else File.Move(tmp, path);
    }

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
