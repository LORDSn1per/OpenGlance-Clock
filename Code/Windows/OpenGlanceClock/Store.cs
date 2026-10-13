using System.Text.Json;
using GlanceProtocol;

namespace OpenGlance;

/// Everything the app remembers, saved as JSON in %APPDATA%\OpenGlance Clock\settings.json.
public sealed class StoredState
{
    public bool AutoConnect { get; set; } = true;
    public ulong? ClockAddress { get; set; }
    public string? ClockName { get; set; }
    public int TimeBaseline { get; set; }
    public List<AlarmEntry> Alarms { get; set; } = new();
    public bool AlarmsPending { get; set; }
    public bool ChimeOn { get; set; }
    public int ChimeSound { get; set; } = 6;
    public int QuietFrom { get; set; } = 22;
    public int QuietTo { get; set; } = 7;
    public WeatherOptions Weather { get; set; } = new();
    public WeatherPlace? Place { get; set; }
}

public sealed record WeatherPlace(string Name, double Latitude, double Longitude);

public sealed record WeatherOptions
{
    public bool On { get; init; }
    public bool Temperature { get; init; } = true;
    public bool Text { get; init; }
    public bool Fahrenheit { get; init; }
    public int RainMode { get; init; } = 1;      // 0 off, 1 alternate, 2 instead of temperature
    public int Cycle { get; init; } = 20;
    public int RainHours { get; init; } = 6;
    public int RainChance { get; init; } = 40;
    public int ColdAt { get; init; } = 5;
    public int HotAt { get; init; } = 35;
    public double RainMm { get; init; } = 0.3;
}

public static class Store
{
    /// Screenshot mode never writes the owner's settings.
    public static bool ReadOnly { get; set; }
    public static StoredState State { get; private set; } = new();
    static string Folder => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "OpenGlance Clock");
    static string FilePath => Path.Combine(Folder, "settings.json");
    static readonly JsonSerializerOptions Options = new() { WriteIndented = true };

    public static void Load()
    {
        if (ReadOnly) { State = new StoredState(); return; }
        try { if (File.Exists(FilePath)) State = JsonSerializer.Deserialize<StoredState>(File.ReadAllText(FilePath), Options) ?? new(); }
        catch { State = new StoredState(); }
    }
    public static void Save()
    {
        if (ReadOnly) return;
        try
        {
            Directory.CreateDirectory(Folder);
            var temp = FilePath + ".tmp";
            File.WriteAllText(temp, JsonSerializer.Serialize(State, Options));
            File.Move(temp, FilePath, overwrite: true);
        }
        catch { /* settings are a convenience; never crash over them */ }
    }
}
