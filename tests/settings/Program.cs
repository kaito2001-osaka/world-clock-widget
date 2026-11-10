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

static string Canonical(JsonElement value) => JsonSerializer.Serialize(value);
static void Check(bool ok, string message)
{
    if (!ok) throw new Exception(message);
}
