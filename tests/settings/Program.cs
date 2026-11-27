using System.Text.Json;
using WorldClockSettings;

using var fixtures = JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "config-parsing.json")));
int count = 0;
foreach (var test in fixtures.RootElement.EnumerateArray())
{
    string name = test.GetProperty("name").GetString()!;
    var actual = ConfigModel.Parse(test.GetProperty("input").GetString()!);
    bool accepted = test.GetProperty("accepted").GetBoolean();
    Check((actual != null) == accepted, name + ": acceptance");
    if (actual != null)
    {
        using var output = JsonDocument.Parse(JsonSerializer.Serialize(actual));
        foreach (var field in test.GetProperty("expected").EnumerateObject())
            Check(Canonical(output.RootElement.GetProperty(field.Name)) == Canonical(field.Value),
                  name + ": " + field.Name);
        var warnings = test.GetProperty("warnings").EnumerateArray().Select(v => v.GetString()!).ToArray();
        Check(actual.CorrectedFields.SequenceEqual(warnings), name + ": warnings");

        // A no-op settings save must retain the parsed cities and other fields.
        var merged = ConfigModel.Merge(actual.Clone(), actual.Clone(), actual);
        Check(JsonSerializer.Serialize(merged) == JsonSerializer.Serialize(actual), name + ": merge");
        var roundTrip = ConfigModel.Parse(JsonSerializer.Serialize(actual));
        Check(roundTrip != null && JsonSerializer.Serialize(roundTrip) == JsonSerializer.Serialize(actual),
              name + ": round trip");
    }
    count++;
}
Console.WriteLine($"Settings: {count} shared config cases passed.");

// File-level cases the gadget's test_config.cpp checks too (#87): encodings
// and the size cap are decided before the text reaches Parse.
string dir = Path.Combine(Path.GetTempPath(), $"worldclock_settings_test_{Environment.ProcessId}");
Directory.CreateDirectory(dir);
try
{
    string path = Path.Combine(dir, "config.json");
    const string doc = "{\"cities\":[{\"label\":\"Osaka\",\"tz\":\"Asia/Tokyo\"}],\"opacity\":40}";

    // What Windows PowerShell 5.1's `>` and Out-File write by default.
    File.WriteAllText(path, doc, new System.Text.UnicodeEncoding(bigEndian: false, byteOrderMark: true));
    var utf16 = ConfigModel.LoadFile(path, out var status);
    Check(status == ConfigLoadStatus.Ok && utf16.Cities[0].Label == "Osaka" && utf16.Opacity == 40,
          "UTF-16 file: loads");

    // Ill-formed UTF-8 reads as U+FFFD; the gadget's DecodeConfigBytes matches.
    File.WriteAllBytes(path, [.. System.Text.Encoding.ASCII.GetBytes(
        "{\"cities\":[{\"label\":\"Osaka"), 0xFF, .. System.Text.Encoding.ASCII.GetBytes(
        "\",\"tz\":\"Asia/Tokyo\"}]}")]);
    var badUtf8 = ConfigModel.LoadFile(path, out status);
    Check(status == ConfigLoadStatus.Ok && badUtf8.Cities[0].Label == "Osaka�",
          "ill-formed UTF-8 file: U+FFFD");

    string Padded(long bytes) => doc + new string(' ', (int)(bytes - doc.Length));
    File.WriteAllText(path, Padded(ConfigModel.MaxFileBytes));
    var atCap = ConfigModel.LoadFile(path, out status);
    Check(status == ConfigLoadStatus.Ok && atCap.Cities[0].Label == "Osaka", "1 MiB file: loads");

    File.WriteAllText(path, Padded(ConfigModel.MaxFileBytes + 1));
    var over = ConfigModel.LoadFile(path, out status);
    Check(status == ConfigLoadStatus.TooLarge && over.Cities.Count == 4, "1 MiB + 1 byte file: too large");

    File.Delete(path);
    ConfigModel.LoadFile(path, out status);
    Check(status == ConfigLoadStatus.Missing, "no file: missing");
}
finally
{
    Directory.Delete(dir, recursive: true);
}
Console.WriteLine("Settings: file loading cases passed.");

static string Canonical(JsonElement value) => JsonSerializer.Serialize(value);
static void Check(bool ok, string message)
{
    if (!ok) throw new Exception(message);
}
