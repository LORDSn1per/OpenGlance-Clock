// C# port of Code/OpenGlanceFlasher/Sources/FlasherCore/FlasherCore.swift (Mac Flasher 1.01),
// with Windows COM-port handling. Keep the two in step.
using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace FlasherCore;

/// A validated OpenGlance Bridge OTA image (the single per-release BIN).
public sealed class BridgeFirmware
{
    public const int AppPartitionSize = 0x1E0000;      // min_spiffs app0/app1
    static readonly byte[] Marker = Encoding.ASCII.GetBytes("#OGBRIDGE-FW-ESP32C3:");

    public enum Problem { Empty, NotESPImage, WrongChip, FullImage, NotOpenGlance, TooLarge }
    public sealed class InvalidFirmware : Exception
    {
        public Problem Kind { get; }
        public InvalidFirmware(Problem kind) : base(Describe(kind)) { Kind = kind; }
    }
    public static string Describe(Problem p) => p switch
    {
        Problem.Empty => "The file is empty.",
        Problem.NotESPImage => "This is not an ESP32 firmware image.",
        Problem.WrongChip => "This firmware is not built for the ESP32-C3.",
        Problem.FullImage => "This is a full USB image. Choose the small “OpenGlance Bridge … OTA.bin” file instead.",
        Problem.NotOpenGlance => "This file is not OpenGlance Bridge firmware.",
        _ => "The file is larger than the bridge's app partition.",
    };

    public string Version { get; }
    public int Size { get; }

    public BridgeFirmware(byte[] b)
    {
        if (b.Length == 0) throw new InvalidFirmware(Problem.Empty);
        if (b.Length < 36 || b[0] != 0xE9) throw new InvalidFirmware(Problem.NotESPImage);
        if (b[12] != 0x05 || b[13] != 0x00) throw new InvalidFirmware(Problem.WrongChip);                 // esp_image_header_t.chip_id
        if (b[32] != 0x32 || b[33] != 0x54 || b[34] != 0xCD || b[35] != 0xAB) throw new InvalidFirmware(Problem.FullImage); // esp_app_desc_t magic
        if (b.Length > AppPartitionSize) throw new InvalidFirmware(Problem.TooLarge);
        // The marker text also occurs inside the firmware's own upload checker,
        // so use the occurrence that is followed by "<digits.dots>;".
        string? found = null;
        int from = 0;
        while (found is null && Find(Marker, b, from) is int start)
        {
            var v = new StringBuilder();
            int i = start + Marker.Length;
            while (i < b.Length && v.Length < 8 && (b[i] == '.' || (b[i] >= '0' && b[i] <= '9'))) { v.Append((char)b[i]); i++; }
            if (v.Length > 0 && i < b.Length && b[i] == ';') found = v.ToString();
            from = start + 1;
        }
        Version = found ?? throw new InvalidFirmware(Problem.NotOpenGlance);
        Size = b.Length;
    }

    static int? Find(byte[] needle, byte[] hay, int from)
    {
        int i = hay.AsSpan(from).IndexOf(needle);
        return i < 0 ? null : from + i;
    }
}

/// Where each piece goes on a bare ESP32-C3 (4 MB, min_spiffs layout).
public sealed class FlashPlan
{
    public sealed record Segment(int Offset, string Path, int Size);
    public IReadOnlyList<Segment> Segments { get; }

    public FlashPlan((string, int) bootloader, (string, int) partitions, (string, int) otadata, (string, int) app)
    {
        Segments = new[]
        {
            new Segment(0x0, bootloader.Item1, bootloader.Item2), new Segment(0x8000, partitions.Item1, partitions.Item2),
            new Segment(0xE000, otadata.Item1, otadata.Item2), new Segment(0x10000, app.Item1, app.Item2),
        };
    }

    public List<string> EsptoolArguments(string port, bool eraseAll, int baud = 921600)
    {
        var args = new List<string> { "--chip", "esp32c3", "--port", port, "--baud", baud.ToString(CultureInfo.InvariantCulture),
            "--before", "default-reset", "--after", "hard-reset", "write-flash" };
        if (eraseAll) args.Add("--erase-all");
        foreach (var s in Segments) { args.Add("0x" + s.Offset.ToString("x", CultureInfo.InvariantCulture)); args.Add(s.Path); }
        return args;
    }

    /// Overall 0–1 progress from an esptool "Writing at 0x… NN.N%" update.
    public double OverallProgress(int address, double percent)
    {
        double total = Segments.Sum(s => (double)s.Size);
        if (total <= 0) return 0;
        int index = -1;
        for (int i = 0; i < Segments.Count; i++) if (address >= Segments[i].Offset) index = i;
        if (index < 0) return 0;
        double done = Segments.Take(index).Sum(s => (double)s.Size);
        double current = Segments[index].Size * Math.Min(Math.Max(percent, 0), 100) / 100;
        return Math.Min(1, (done + current) / total);
    }
}

public static class EsptoolOutput
{
    static readonly Regex Ansi = new("\u001B\\[[0-9;?]*[ -/]*[@-~]");
    static readonly Regex Progress = new(@"Writing at 0x([0-9a-fA-F]+).*?([0-9]+(?:\.[0-9]+)?) ?%");

    /// Removes ANSI escape sequences (colours, cursor moves) from terminal output.
    public static string StripAnsi(string s) => Ansi.Replace(s, "");

    /// Finds "Writing at 0xADDR … NN.N%" in one line of esptool output.
    public static (int Address, double Percent)? ProgressIn(string line)
    {
        var m = Progress.Match(line);
        if (!m.Success) return null;
        return (int.Parse(m.Groups[1].Value, NumberStyles.HexNumber, CultureInfo.InvariantCulture), double.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture));
    }

    /// Plain-language explanation for common esptool failures (Windows and macOS wording).
    public static string FriendlyError(string log)
    {
        var l = log.ToLowerInvariant();
        if (l.Contains("no such file or directory") || l.Contains("filenotfounderror") || l.Contains("the system cannot find the file specified"))
            return "The selected port disappeared. Reconnect the board, press Refresh and choose its port again.";
        if (l.Contains("resource busy") || l.Contains("errno 16") || l.Contains("could not exclusively lock") || l.Contains("access is denied") || l.Contains("permissionerror"))
            return "The USB port is in use by another app (for example an Arduino serial monitor). Close it and try again.";
        if (l.Contains("no serial data received") || l.Contains("wrong boot mode") || l.Contains("failed to connect"))
            return "Couldn't talk to the ESP32-C3. Hold its BOOT button, tap RESET (or unplug and replug USB while holding BOOT), release BOOT, then try again.";
        if ((l.Contains("this chip is") && !l.Contains("esp32-c3")) || l.Contains("wrong chip"))
            return "The connected board is not an ESP32-C3.";
        return "Flashing failed. Open Details for the full log.";
    }
}

public static class SerialPorts
{
    /// Windows COM ports, numerically ordered (COM3 before COM10).
    public static List<string> Order(IEnumerable<string> names) => names.Where(n => n.StartsWith("COM", StringComparison.OrdinalIgnoreCase))
        .Distinct(StringComparer.OrdinalIgnoreCase)
        .OrderBy(n => int.TryParse(n.AsSpan(3), out var x) ? x : int.MaxValue).ThenBy(n => n, StringComparer.OrdinalIgnoreCase).ToList();

    /// When the list changes, prefer a port that has just appeared (the board that was plugged in).
    public static string Choose(IReadOnlyList<string> previous, IReadOnlyList<string> now, string current)
    {
        var added = now.Where(p => !previous.Contains(p, StringComparer.OrdinalIgnoreCase)).ToList();
        if (added.Count > 0 && previous.Count > 0) return added[^1];
        if (now.Contains(current, StringComparer.OrdinalIgnoreCase)) return current;
        return now.Count > 0 ? now[^1] : "";
    }
}

public static class BootBanner
{
    static readonly Regex Banner = new(@"OpenGlance Bridge ([0-9]+\.[0-9]+) started");
    /// Version from "OpenGlance Bridge 1.02 started" printed at boot.
    public static string? Version(string text) { var m = Banner.Match(text); return m.Success ? m.Groups[1].Value : null; }
}
