// C# port of Code/OpenGlanceClock/Sources/GlanceProtocol/GlanceProtocol.swift (Mac app 0.31).
// Keep the two in step: same packets, same validation, same check cases.
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace GlanceProtocol;

public sealed class ProtocolException : Exception
{
    public enum Kind { Malformed, UnsupportedWireType, InvalidSettings, InvalidText, InvalidDuration }
    public Kind Problem { get; }
    public ProtocolException(Kind kind) : base(Describe(kind)) { Problem = kind; }
    static string Describe(Kind k) => k switch
    {
        Kind.Malformed => "The clock returned an incomplete or malformed message.",
        Kind.UnsupportedWireType => "This clock uses a message format this version cannot safely edit.",
        Kind.InvalidSettings => "The response could not be recognised as clock settings. No settings were changed.",
        Kind.InvalidText => "Use 1–64 characters: English letters, numbers and basic punctuation.",
        _ => "Choose a timer between 1 minute and 12 hours.",
    };
}

static class Bytes
{
    public static byte[] Concat(params byte[][] parts)
    {
        var all = new byte[parts.Sum(p => p.Length)];
        int i = 0;
        foreach (var p in parts) { Buffer.BlockCopy(p, 0, all, i, p.Length); i += p.Length; }
        return all;
    }
}

/// A small wire-format reader that retains every original field, including unknown fields.
/// We intentionally do not reconstruct a clock's settings from guessed defaults.
public sealed class WireMessage
{
    public sealed record Field(int Number, int WireType, byte[] Encoded, ulong? Integer);
    public IReadOnlyList<Field> Fields { get; }

    public WireMessage(byte[] bytes)
    {
        if (bytes.Length > 65536) throw new ProtocolException(ProtocolException.Kind.Malformed);
        int cursor = 0;
        var output = new List<Field>();
        ulong Varint()
        {
            ulong value = 0;
            for (int index = 0; index < 10; index++)
            {
                if (cursor >= bytes.Length) throw new ProtocolException(ProtocolException.Kind.Malformed);
                byte b = bytes[cursor++];
                if (index == 9 && b > 1) throw new ProtocolException(ProtocolException.Kind.Malformed);
                value |= (ulong)(b & 0x7f) << (index * 7);
                if ((b & 0x80) == 0) return value;
            }
            throw new ProtocolException(ProtocolException.Kind.Malformed);
        }
        void Skip(ulong count)
        {
            if (count > (ulong)(bytes.Length - cursor)) throw new ProtocolException(ProtocolException.Kind.Malformed);
            cursor += (int)count;
        }
        while (cursor < bytes.Length)
        {
            int start = cursor;
            ulong key = Varint();
            ulong number = key >> 3; int type = (int)(key & 7);
            if (number == 0 || number > 536870911) throw new ProtocolException(ProtocolException.Kind.Malformed);
            ulong? integer = null;
            switch (type)
            {
                case 0: integer = Varint(); break;
                case 1: Skip(8); break;
                case 2: Skip(Varint()); break;
                case 5: Skip(4); break;
                default: throw new ProtocolException(ProtocolException.Kind.UnsupportedWireType);
            }
            output.Add(new Field((int)number, type, bytes[start..cursor], integer));
        }
        Fields = output;
    }

    public ulong? Integer(int number) => Fields.LastOrDefault(f => f.Number == number)?.Integer;
    public byte[] Encoded => Bytes.Concat(Fields.Select(f => f.Encoded).ToArray());

    public byte[] ReplacingIntegers(IReadOnlyDictionary<int, ulong> changes)
    {
        var parts = new List<byte[]>();
        var written = new HashSet<int>();
        foreach (var f in Fields)
        {
            if (changes.TryGetValue(f.Number, out var value)) { if (written.Add(f.Number)) parts.Add(IntegerField(f.Number, value)); }
            else parts.Add(f.Encoded);
        }
        foreach (var n in changes.Keys.OrderBy(k => k)) if (!written.Contains(n)) parts.Add(IntegerField(n, changes[n]));
        return Bytes.Concat(parts.ToArray());
    }

    public static byte[] Varint(ulong value)
    {
        var bytes = new List<byte>();
        do
        {
            byte b = (byte)(value & 0x7f); value >>= 7;
            if (value > 0) b |= 0x80;
            bytes.Add(b);
        } while (value > 0);
        return bytes.ToArray();
    }
    public static byte[] IntegerField(int field, ulong value) => Bytes.Concat(Varint((ulong)(field << 3)), Varint(value));
    public static byte[] BytesField(int field, byte[] value) => Bytes.Concat(Varint((ulong)(field << 3 | 2)), Varint((ulong)value.Length), value);
}

public readonly record struct AutomaticBrightnessRange
{
    public int Low { get; }
    public int High { get; }
    public AutomaticBrightnessRange(int low, int high)
    {
        if (low < 0 || low > 255 || high < 0 || high > 255 || low > high) throw new ProtocolException(ProtocolException.Kind.InvalidSettings);
        Low = low; High = high;
    }
    public ulong Encoded => (ulong)Low << 16 | (ulong)High << 8;
}

public sealed class ClockSettings
{
    public byte[] Raw { get; }
    public int Brightness { get; }
    public int AutomaticBrightnessLow { get; }
    public int AutomaticBrightnessHigh { get; }
    public bool NightMode { get; }
    public bool DigitalTime { get; }
    public bool TwelveHour { get; }
    public bool HourPoints { get; }
    public bool Muted { get; }
    public int DateFormat { get; }
    /// Settings field 18 (int32 minutes, firmware 1.6.7+). See ClockTimeCorrection.
    public int TimeOffsetMinutes { get; }
    public static readonly IReadOnlySet<int> EditableFields = new HashSet<int> { 2, 4, 5, 9, 10, 11, 12 };
    public const int TimeOffsetField = 18;

    public ClockSettings(byte[] data)
    {
        var payload = data;
        if (payload.Length >= 5 && payload[0] == 68 && payload[1] == 97 && payload[2] == 116 && payload[3] == 97 && payload[4] == 0) payload = payload[5..];
        else if (payload.Length > 0 && payload[0] == 5) payload = payload[1..];
        var message = new WireMessage(payload);
        int[] known = { 2, 4, 5, 9, 10, 11, 12, 13 };
        if (message.Fields.Count(f => known.Contains(f.Number) && f.WireType == 0) < 3) throw new ProtocolException(ProtocolException.Kind.InvalidSettings);
        int[] booleans = { 2, 3, 4, 9, 11, 12 };
        foreach (var f in message.Fields.Where(f => booleans.Contains(f.Number)))
            if (f.WireType != 0 || f.Integer is not ulong v || v > 1) throw new ProtocolException(ProtocolException.Kind.InvalidSettings);
        if ((message.Integer(5) ?? 0) > 4) throw new ProtocolException(ProtocolException.Kind.InvalidSettings);
        Raw = payload;
        ulong brightness = message.Integer(10) ?? 0;
        Brightness = (int)(brightness & 255);
        AutomaticBrightnessLow = (int)((brightness >> 16) & 255);
        AutomaticBrightnessHigh = (int)((brightness >> 8) & 255);
        NightMode = (message.Integer(2) ?? 0) != 0;
        DigitalTime = (message.Integer(11) ?? 0) != 0;
        TwelveHour = (message.Integer(12) ?? 0) != 0;
        HourPoints = (message.Integer(9) ?? 0) != 0;
        Muted = (message.Integer(4) ?? 0) != 0;
        DateFormat = (int)(message.Integer(5) ?? 0);
        TimeOffsetMinutes = message.Integer(TimeOffsetField) is ulong offset ? unchecked((int)offset) : 0;
    }

    /// Replaces only field 18; every other field, known or unknown, is resent unchanged.
    /// Firmware decodes each write from scratch, so dropping field 18 would reset it to 0
    /// and shift the clock back; that is why the full original message is always kept.
    public byte[] PatchedTimeOffset(int minutes)
    {
        if (minutes < ClockTimeCorrection.OffsetMin || minutes > ClockTimeCorrection.OffsetMax) throw new ProtocolException(ProtocolException.Kind.InvalidSettings);
        var message = new WireMessage(Raw);
        return Bytes.Concat(new byte[] { 5, 0, 0, 0 }, message.ReplacingIntegers(new Dictionary<int, ulong> { [TimeOffsetField] = unchecked((ulong)(long)minutes) }));
    }

    public byte[] Patched(IReadOnlyDictionary<int, ulong> changes, AutomaticBrightnessRange? automaticRange = null)
    {
        if ((changes.Count == 0 && automaticRange is null) || !changes.Keys.All(EditableFields.Contains) || (automaticRange is not null && changes.ContainsKey(10)))
            throw new ProtocolException(ProtocolException.Kind.InvalidSettings);
        foreach (var (field, value) in changes)
        {
            if (field == 10) { if (value > 255) throw new ProtocolException(ProtocolException.Kind.InvalidSettings); }
            else if (field == 5) { if (value > 4) throw new ProtocolException(ProtocolException.Kind.InvalidSettings); }
            else if (value > 1) throw new ProtocolException(ProtocolException.Kind.InvalidSettings);
        }
        var message = new WireMessage(Raw);
        var merged = new Dictionary<int, ulong>(changes);
        if (changes.TryGetValue(10, out var brightness)) merged[10] = ((message.Integer(10) ?? 0) & ~255UL) | brightness;
        if (automaticRange is AutomaticBrightnessRange range) merged[10] = ((message.Integer(10) ?? 0) & ~0xffffffUL) | range.Encoded;
        return Bytes.Concat(new byte[] { 5, 0, 0, 0 }, message.ReplacingIntegers(merged));
    }

    public bool Matches(IReadOnlyDictionary<int, ulong> changes, AutomaticBrightnessRange? automaticRange = null)
    {
        WireMessage message;
        try { message = new WireMessage(Raw); } catch { return false; }
        if (automaticRange is AutomaticBrightnessRange range && ((message.Integer(10) ?? 0) & 0xffffff) != range.Encoded) return false;
        return changes.All(c =>
        {
            ulong actual = message.Integer(c.Key) ?? 0;
            return (c.Key == 10 ? actual & 255 : actual) == c.Value;
        });
    }
}

/// A time as read off the clock or chosen by the owner. Date is optional.
public readonly record struct WallTime(int Hour, int Minute, DateOnly? Date = null);

/// Setting the clock's time without the Current Time Service.
///
/// From static disassembly of Glance firmware 1.6.7: Settings field 18 is an int32 offset in minutes.
/// When a Settings write changes it from old to new, the firmware moves its running time by
/// (new - old) minutes; CTS time is applied as CTS + field 18. The clock cannot report its time,
/// so the app estimates it (see EstimatedClockTime) or the owner types it. Whole minutes only.
public static class ClockTimeCorrection
{
    public const int OffsetMin = -1_000_000, OffsetMax = 1_000_000;   // about ±694 days
    static readonly int[] MinimumFirmware = { 1, 6, 7 };

    /// null when the version string is missing or unrecognised.
    public static bool? SupportsOffset(string? firmware)
    {
        if (firmware is null) return null;
        var m = Regex.Match(firmware, @"(\d+)\.(\d+)(?:\.(\d+))?");
        if (!m.Success) return null;
        int[] v = { int.Parse(m.Groups[1].Value), int.Parse(m.Groups[2].Value), m.Groups[3].Success ? int.Parse(m.Groups[3].Value) : 0 };
        for (int i = 0; i < 3; i++) if (v[i] != MinimumFirmware[i]) return v[i] > MinimumFirmware[i];
        return true;
    }

    /// Minutes to add so the clock shows `target` (null = the computer's current minute).
    /// Missing dates default to today; with no date on either side the result stays within ±12 hours.
    /// Wall-clock arithmetic only, so daylight-saving gaps do not move the times.
    public static int? Minutes(DateTime now, WallTime clockShows, WallTime? target = null)
    {
        static bool Valid(WallTime t) => t.Hour is >= 0 and <= 23 && t.Minute is >= 0 and <= 59;
        if (!Valid(clockShows) || (target is WallTime tt && !Valid(tt))) return null;
        var today = DateOnly.FromDateTime(now);
        DateTime Resolve(WallTime t) => (t.Date ?? today).ToDateTime(new TimeOnly(t.Hour, t.Minute), DateTimeKind.Unspecified);
        var wanted = target is WallTime w ? Resolve(w) : new DateTime(now.Year, now.Month, now.Day, now.Hour, now.Minute, 0, DateTimeKind.Unspecified);
        var shown = Resolve(clockShows);
        bool withDate = clockShows.Date is not null || target?.Date is not null;
        int delta = (int)Math.Round((wanted - shown).TotalMinutes, MidpointRounding.AwayFromZero);
        if (!withDate)
        {
            delta = ((delta % 1440) + 1440) % 1440;
            if (delta > 720) delta -= 1440;
        }
        return delta;
    }

    /// Estimated clock reading. CTS (bridge) sets the clock to true time + offset, and every change
    /// from this app moves both the clock and the offset by the same amount, so clock − computer =
    /// offset − baseline, where baseline is 0 after a bridge sync and is re-learned whenever the
    /// owner types what the clock shows.
    public static DateTime EstimatedClockTime(DateTime now, int offset, int baseline) => now.AddMinutes(offset - baseline);
    /// The baseline to store after a set whose result is known: clock − computer = clockMinusNow minutes.
    public static int Baseline(int offset, int clockMinusNow) => offset - clockMinusNow;

    /// Parses a typed time such as "18:05", "6:05 pm", "6.05PM", "1805" or "7".
    public static (int Hour, int Minute)? ParseTime(string text)
    {
        var s = text.ToLowerInvariant().Replace(" ", "");
        char? meridiem = null;
        foreach (var suffix in new[] { "am", "pm", "a", "p" })
            if (s.EndsWith(suffix, StringComparison.Ordinal)) { meridiem = suffix[0]; s = s[..^suffix.Length]; break; }
        int? hour = null, minute = null;
        var parts = s.Split(':', '.');
        if (parts.Length == 2 && parts[0].Length is 1 or 2 && parts[1].Length == 2 && parts.All(p => p.All(char.IsAsciiDigit)))
        { hour = int.Parse(parts[0]); minute = int.Parse(parts[1]); }
        else if (parts.Length == 1 && s.Length > 0 && s.All(char.IsAsciiDigit))
        {
            switch (s.Length)
            {
                case 1: case 2: hour = int.Parse(s); minute = 0; break;
                case 3: case 4: hour = int.Parse(s[..^2]); minute = int.Parse(s[^2..]); break;
                default: return null;
            }
        }
        if (hour is not int h || minute is not int m || m < 0 || m > 59) return null;
        if (meridiem is char mm)
        {
            if (h < 1 || h > 12) return null;
            h = h % 12 + (mm == 'p' ? 12 : 0);
        }
        if (h < 0 || h > 23) return null;
        return (h, m);
    }

    public static string Describe(int minutes)
    {
        if (minutes == 0) return "no change";
        int size = Math.Abs(minutes), days = size / 1440, hours = size % 1440 / 60, mins = size % 60;
        var parts = new List<string>();
        if (days > 0) parts.Add($"{days} day" + (days == 1 ? "" : "s"));
        if (hours > 0) parts.Add($"{hours} hour" + (hours == 1 ? "" : "s"));
        if (mins > 0) parts.Add($"{mins} minute" + (mins == 1 ? "" : "s"));
        return (minutes > 0 ? "forward " : "back ") + string.Join(" ", parts);
    }
}

public static class ClockDiscovery
{
    public static bool IsGlance(string? name, IEnumerable<string> advertisedServices)
    {
        bool namedClock = name?.Contains("glance", StringComparison.OrdinalIgnoreCase) ?? false;
        bool glanceService = advertisedServices.Any(s => string.Equals(s, GlanceCommands.ServiceUuid, StringComparison.OrdinalIgnoreCase));
        return namedClock || glanceService;
    }
}

public static class GlanceCommands
{
    public const string ServiceUuid = "5075f606-1e0e-11e7-93ae-92361f002671";
    public const string ControlUuid = "5075fb2e-1e0e-11e7-93ae-92361f002671";
    public const string StateUuid = "5075fc78-1e0e-11e7-93ae-92361f002671";

    static readonly int[] Animations = { 0, 1, 2, 10, 11, 12, 15, 17 };
    static readonly int[] Colours = { 5, 12, 15, 18, 23 };

    public static byte[] TextData(string text)
    {
        var bytes = Encoding.UTF8.GetBytes(text);
        if (bytes.Length == 0 || bytes.Length > 64 || bytes.Any(b => b < 32 || b > 126)) throw new ProtocolException(ProtocolException.Kind.InvalidText);
        return Bytes.Concat(WireMessage.IntegerField(1, 0), WireMessage.BytesField(2, bytes));
    }
    public static byte[] Notice(string text, int animation = 1, int sound = 0, int color = 18)
    {
        if (!Animations.Contains(animation) || sound < 0 || sound > 17 || !Colours.Contains(color)) throw new ProtocolException(ProtocolException.Kind.Malformed);
        return Bytes.Concat(new byte[] { 2, 16, 0, 0 }, WireMessage.IntegerField(1, (ulong)animation),
            WireMessage.IntegerField(2, (ulong)sound), WireMessage.IntegerField(3, (ulong)color), WireMessage.BytesField(4, TextData(text)));
    }
    public static byte[] Timer(int seconds, string finalText = "Time's up!")
    {
        if (seconds < 60 || seconds > 43200) throw new ProtocolException(ProtocolException.Kind.InvalidDuration);
        // countdown (field 1) is the pre-timer count, NOT the timer duration.
        var interval = Bytes.Concat(WireMessage.IntegerField(2, (ulong)seconds), WireMessage.IntegerField(3, 0));
        return Bytes.Concat(new byte[] { 3, 0, 0, 0 }, WireMessage.IntegerField(1, 0), WireMessage.BytesField(2, interval), WireMessage.BytesField(3, TextData(finalText)));
    }
    public static readonly byte[] StopTimer = { 10 };
    public static readonly byte[] AlarmStop = { 20, 0, 0, 0 };
    public static readonly byte[] RefreshDisplay = { 35 };
    public static readonly byte[] StartCalibration = { 43 };
    public static readonly byte[] ConfirmCalibration = { 44 };

    /// Whole alarm list as the original app sends it (APK 1.5.1 modules 819/1991); empty = AlarmsClear.
    public static byte[] Alarms(IReadOnlyList<AlarmEntry> list)
    {
        if (list.Count > 4) throw new ProtocolException(ProtocolException.Kind.InvalidSettings);
        if (list.Count == 0) return new byte[] { 21, 0, 0, 0 };
        var body = new List<byte[]>();
        foreach (var a in list)
        {
            if (!a.IsValid) throw new ProtocolException(ProtocolException.Kind.InvalidSettings);
            var item = new List<byte[]>();
            if (a.Days != 0) item.Add(WireMessage.IntegerField(3, (ulong)a.Days));
            item.Add(WireMessage.BytesField(4, Bytes.Concat(WireMessage.IntegerField(1, (ulong)a.Hour), WireMessage.IntegerField(2, (ulong)a.Minute))));
            item.Add(WireMessage.IntegerField(5, (ulong)a.Sound));
            item.Add(WireMessage.BytesField(6, WireMessage.BytesField(2, Encoding.ASCII.GetBytes(a.Text))));
            body.Add(WireMessage.BytesField(1, Bytes.Concat(item.ToArray())));
        }
        return Bytes.Concat(new byte[] { 4, 47, 0, 0 }, Bytes.Concat(body.ToArray()));
    }
    /// SaveForecastScene 7, priority 16, display (8 ring only keeps time/date; 24 ring + text), slot.
    public static byte[] Forecast(byte slot, ForecastScene scene, bool ringOnly = true) =>
        Bytes.Concat(new byte[] { 7, 16, (byte)(ringOnly ? 8 : 24), slot }, scene.Encoded);
    public static byte[] DeleteScene(byte slot) => new byte[] { 33, 0, 0, slot };
}

public sealed class ClockHandState
{
    public bool Homing { get; }
    public bool AwaitingConfirmation { get; }
    public bool HomingFailure { get; }
    public bool MotorFailure { get; }
    public ClockHandState(byte[] data)
    {
        if (data.Length is not (1 or 2)) throw new ProtocolException(ProtocolException.Kind.Malformed);
        int bits = data[0] | (data.Length == 2 ? data[1] << 8 : 0);
        Homing = (bits & 128) != 0; AwaitingConfirmation = (bits & 1024) != 0;
        HomingFailure = (bits & 64) != 0; MotorFailure = (bits & 2048) != 0;
    }
}

public sealed class AlarmEntry
{
    public Guid Id { get; set; } = Guid.NewGuid();
    public int Hour { get; set; } = 7;
    public int Minute { get; set; }
    public int Days { get; set; } = 31;        // Mon=1 … Sun=64; 0 = once
    public int Sound { get; set; } = 6;
    public string Text { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public bool IsValid => Hour is >= 0 and <= 23 && Minute is >= 0 and <= 59 && Days is >= 0 and <= 127 && Sound is >= 1 and <= 17
        && Encoding.UTF8.GetByteCount(Text) <= 16 && Text.All(c => c >= 32 && c <= 126);
    public AlarmEntry Copy() => (AlarmEntry)MemberwiseClone();
}

// Weather (same rules as the ESP32 bridge's weather.h)

public sealed class ForecastScene
{
    public long Timestamp;                 // local time expressed as a Unix timestamp
    public uint MaxColor, MinColor;
    public int Max, Min;
    public short[] Values = new short[24];
    public byte[] Template = Array.Empty<byte>();
    public byte[] Encoded
    {
        get
        {
            var values = new byte[48];
            for (int i = 0; i < 24; i++) { ushort v = unchecked((ushort)Values[i]); values[2 * i] = (byte)v; values[2 * i + 1] = (byte)(v >> 8); }
            return Bytes.Concat(WireMessage.IntegerField(1, unchecked((ulong)Timestamp)), WireMessage.IntegerField(4, MaxColor), WireMessage.IntegerField(5, MinColor),
                WireMessage.IntegerField(6, unchecked((ulong)(long)Max)), WireMessage.IntegerField(7, unchecked((ulong)(long)Min)),
                WireMessage.BytesField(8, values), WireMessage.BytesField(9, Template));
        }
    }
}

public sealed class HourlyForecast
{
    public List<DateTimeOffset> Hours = new();
    public List<double> Temperature = new();
    public List<int> Chance = new();
    public List<double> Rain = new();
    public double? Current;
    public HourlyForecast() { }

    /// Parses an Open-Meteo response requested with timeformat=unixtime.
    public static HourlyForecast FromOpenMeteo(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        var hourly = root.GetProperty("hourly");
        double?[] Arr(string name) => hourly.GetProperty(name).EnumerateArray().Select(e => e.ValueKind == JsonValueKind.Number ? e.GetDouble() : (double?)null).ToArray();
        var time = Arr("time"); var temp = Arr("temperature_2m"); var chance = Arr("precipitation_probability"); var rain = Arr("precipitation");
        int n = time.Length;
        if (n == 0 || temp.Length != n || chance.Length != n || rain.Length != n) throw new ProtocolException(ProtocolException.Kind.Malformed);
        var f = new HourlyForecast();
        f.Hours = time.Select(t => DateTimeOffset.FromUnixTimeSeconds((long)(t ?? 0))).ToList();
        double last = 0;
        f.Temperature = temp.Select(v => { if (v is double d) last = d; return last; }).ToList();
        f.Chance = chance.Select(v => (int)Math.Round(v ?? 0, MidpointRounding.AwayFromZero)).ToList();
        f.Rain = rain.Select(v => v ?? 0).ToList();
        if (root.TryGetProperty("current", out var current) && current.TryGetProperty("temperature_2m", out var c) && c.ValueKind == JsonValueKind.Number) f.Current = c.GetDouble();
        return f;
    }

    public int? StartIndex(DateTimeOffset now)
    {
        int i = Hours.FindIndex(h => h <= now && now < h.AddHours(1));
        return i < 0 ? null : i;
    }
}

public static class WeatherScenes
{
    public static uint Mix(uint a, uint b, double t)
    {
        t = Math.Min(1, Math.Max(0, t));
        uint Ch(int s)
        {
            double x = (a >> s) & 255, y = (b >> s) & 255;
            return (uint)Math.Round(x + (y - x) * t, MidpointRounding.AwayFromZero) << s;
        }
        return Ch(16) | Ch(8) | Ch(0);
    }
    /// Blue → green → amber → red across coldAt…hotAt.
    public static uint Palette(double t)
    {
        uint[] stops = { 0x2060FF, 0x00C070, 0xFFC000, 0xFF3000 };
        if (t <= 0) return stops[0];
        if (t >= 1) return stops[3];
        double x = t * 3; int i = (int)x;
        return Mix(stops[i], stops[i + 1], x - i);
    }

    public static ForecastScene? Temperature(HourlyForecast f, DateTimeOffset now, int utcOffset, bool fahrenheit, int coldAt, int hotAt)
    {
        if (f.StartIndex(now) is not int start) return null;
        var s = new ForecastScene { Timestamp = f.Hours[start].ToUnixTimeSeconds() + utcOffset };
        int lo = 0, hi = 0;
        for (int i = 0; i < 24; i++)
        {
            int idx = Math.Min(start + i, f.Hours.Count - 1);
            int v = (int)Math.Round((i == 0 ? f.Current : null) ?? f.Temperature[idx], MidpointRounding.AwayFromZero);
            s.Values[i] = (short)Math.Clamp(v, short.MinValue, short.MaxValue);
            if (i == 0 || v < lo) lo = v;
            if (i == 0 || v > hi) hi = v;
        }
        if (hi == lo) hi = lo + 1;
        s.Min = lo; s.Max = hi;
        double span = hotAt > coldAt ? hotAt - coldAt : 1;
        s.MinColor = Palette((lo - coldAt) / span);
        s.MaxColor = Palette((hi - coldAt) / span);
        s.Template = new byte[] { 0xC2, 0x8F, 0x08, 0xC2, 0xB0, (byte)(fahrenheit ? 'F' : 'C') };
        return s;
    }

    public static int RainValue(double rain, int chance, double minMm, int minChance)
    {
        if (rain < minMm - 1e-6 || chance < minChance) return 0;
        return Math.Min(100, Math.Max(10, chance));
    }

    /// Rain ring, or null when no hour in the look-ahead qualifies (amount AND chance).
    public static ForecastScene? Rain(HourlyForecast f, DateTimeOffset now, int utcOffset, double minMm, int minChance, int lookahead)
    {
        if (f.StartIndex(now) is not int start) return null;
        var s = new ForecastScene { Timestamp = f.Hours[start].ToUnixTimeSeconds() + utcOffset };
        bool any = false;
        for (int i = 0; i < 24; i++)
        {
            int idx = start + i;
            int v = idx < f.Hours.Count ? RainValue(f.Rain[idx], f.Chance[idx], minMm, minChance) : 0;
            s.Values[i] = (short)v;
            if (v > 0 && i < lookahead) any = true;
        }
        s.Min = 0; s.Max = 100;
        s.MinColor = 0x7FD4FF; s.MaxColor = 0x0030FF;
        s.Template = new byte[] { 0xC2, 0x95, 0x08, (byte)'%' };
        return any ? s : null;
    }
}

/// Names and choices shared by the views (same lists as the bridge web page and Mac app).
public static class Glance
{
    public static readonly string[] Sounds = { "None", "Waves", "Rise", "Charging", "Steps", "Radar", "Bells", "Bye", "Hello", "Flowers",
        "Circles", "Complete", "Popcorn", "Break", "Opening", "High", "Shine", "Extension" };
    public static readonly (int, string)[] AnimationNames = { (1, "Pulse"), (2, "Wave"), (10, "Fire"), (11, "Wheel"), (12, "Flower"), (15, "Sun"), (17, "Cloud"), (0, "None") };
    public static readonly (int, string)[] ColourNames = { (18, "Turquoise"), (23, "Lime"), (15, "Blue"), (5, "Red"), (12, "White") };
    public static readonly (int, string)[] DateFormats = { (0, "Off"), (1, "24 Jan"), (2, "24 Tue"), (3, "Jan 24"), (4, "Tue 24") };
    public static readonly string[] Days = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    public static string DaysText(int d) => d switch
    {
        0 => "Once",
        127 => "Every day",
        31 => "Weekdays",
        96 => "Weekend",
        _ => string.Join(" ", Days.Where((_, i) => (d & (1 << i)) != 0)),
    };
    public static string Hour(int h) => h.ToString("00", CultureInfo.InvariantCulture) + ":00";
}
