// Standalone checks for the Windows protocol port. Same cases as the Mac app's ProtocolChecks
// (Code/OpenGlanceClock/Tests), except the Mac-only Current Time Service encoding groups.
using GlanceProtocol;

int failures = 0;
void Check(bool ok, string what, [System.Runtime.CompilerServices.CallerLineNumber] int line = 0)
{
    if (!ok) { failures++; Console.WriteLine($"FAIL line {line}: {what}"); }
}
void Equal<T>(T actual, T expected, string what, [System.Runtime.CompilerServices.CallerLineNumber] int line = 0)
{
    if (!EqualityComparer<T>.Default.Equals(actual, expected)) { failures++; Console.WriteLine($"FAIL line {line}: {what}: expected {expected}, got {actual}"); }
}
void Same(byte[] actual, byte[] expected, string what, [System.Runtime.CompilerServices.CallerLineNumber] int line = 0)
{
    if (!actual.SequenceEqual(expected)) { failures++; Console.WriteLine($"FAIL line {line}: {what}: expected [{string.Join(",", expected)}], got [{string.Join(",", actual)}]"); }
}
void Throws(Action a, string what, [System.Runtime.CompilerServices.CallerLineNumber] int line = 0)
{
    try { a(); failures++; Console.WriteLine($"FAIL line {line}: {what}: expected rejection"); } catch (ProtocolException) { }
}
byte[] Cat(params byte[][] p) => p.SelectMany(x => x).ToArray();
byte[] Settings(ulong brightness = 128) => Cat(WireMessage.IntegerField(2, 0), WireMessage.IntegerField(4, 1), WireMessage.IntegerField(10, brightness));
Dictionary<int, ulong> D(params (int, ulong)[] p) => p.ToDictionary(x => x.Item1, x => x.Item2);

// 1. Discovery filtering
Equal(ClockDiscovery.IsGlance(null, Array.Empty<string>()), false, "no name");
Equal(ClockDiscovery.IsGlance("", new[] { "180F" }), false, "battery only");
Equal(ClockDiscovery.IsGlance("Headphones", Array.Empty<string>()), false, "other name");
Check(ClockDiscovery.IsGlance("GlanceClock", Array.Empty<string>()), "named clock");
Check(ClockDiscovery.IsGlance(null, new[] { GlanceCommands.ServiceUuid.ToUpperInvariant() }), "service UUID");

// 2. Settings preservation
{
    var unknown = WireMessage.BytesField(30, new byte[] { 0, 255, 8 });
    var original = new ClockSettings(Cat(Settings(2016768), unknown));
    Equal(original.Brightness, 0, "automatic brightness");
    var packet = original.Patched(D((10, 180), (2, 1)));
    Same(packet[..4], new byte[] { 5, 0, 0, 0 }, "settings header");
    var payload = packet[4..];
    Equal(new WireMessage(payload).Integer(10), (ulong?)(2016768 | 180), "upper brightness bits kept");
    Same(payload[^unknown.Length..], unknown, "unknown field kept");
    Check(new ClockSettings(payload).Matches(D((10, 180), (2, 1))), "verification");
}
// 3. Validation
Throws(() => new ClockSettings(new byte[] { 8, 1 }), "too few fields");
Throws(() => new ClockSettings(Settings()).Patched(D((3, 1))), "non-editable field");
Throws(() => new ClockSettings(Settings()).Patched(D((10, 256))), "brightness range");
Equal(new ClockSettings(Cat(new byte[] { 68, 97, 116, 97, 0 }, Settings())).Brightness, 128, "Data\\0 prefix");

// 4. Automatic brightness range
{
    var range = new AutomaticBrightnessRange(30, 220);
    Equal(range.Encoded, 2022400UL, "packed 30–220");
    var unknown = WireMessage.BytesField(30, new byte[] { 1, 2, 255 });
    var original = new ClockSettings(Cat(Settings(0xab000000 | 128), unknown));
    var updated = new ClockSettings(original.Patched(D(), range)[4..]);
    Equal(updated.Brightness, 0, "automatic");
    Equal(updated.AutomaticBrightnessLow, 30, "low"); Equal(updated.AutomaticBrightnessHigh, 220, "high");
    Equal(new WireMessage(updated.Raw).Integer(10), (ulong?)(0xab000000 | 2022400), "higher bits kept");
    Same(updated.Raw[^unknown.Length..], unknown, "unknown kept");
    Check(updated.Matches(D(), range), "range matches");
    Equal(updated.Matches(D(), new AutomaticBrightnessRange(31, 220)), false, "range mismatch");
    var manual = new ClockSettings(updated.Patched(D((10, 200)))[4..]);
    Equal(manual.Brightness, 200, "manual"); Equal(manual.AutomaticBrightnessLow, 30, "manual keeps low");
    Throws(() => new AutomaticBrightnessRange(-1, 220), "low < 0");
    Throws(() => new AutomaticBrightnessRange(30, 256), "high > 255");
    Throws(() => new AutomaticBrightnessRange(200, 100), "low > high");
    Throws(() => original.Patched(D((10, 128)), range), "conflicting writes");
    foreach (var v in new[] { 0, 255 })
    {
        var fixedRange = new AutomaticBrightnessRange(v, v);
        Check(new ClockSettings(original.Patched(D(), fixedRange)[4..]).Matches(D(), fixedRange), $"boundary {v}");
    }
}
// 5. Notice golden packet
Same(GlanceCommands.Notice("Hi"), new byte[] { 2, 16, 0, 0, 8, 1, 16, 0, 24, 18, 34, 6, 8, 0, 18, 2, 72, 105 }, "notice");
Throws(() => GlanceCommands.Notice("Hello 👋"), "emoji");
Throws(() => GlanceCommands.Notice(""), "empty");
Throws(() => GlanceCommands.Notice(new string('x', 65)), "too long");
// 6. Timer
Same(GlanceCommands.Timer(300, "Done")[..12], new byte[] { 3, 0, 0, 0, 8, 0, 18, 5, 16, 172, 2, 24 }, "timer interval");
Throws(() => GlanceCommands.Timer(59), "short timer");
Throws(() => GlanceCommands.Timer(43201), "long timer");
Same(GlanceCommands.StartCalibration, new byte[] { 43 }, "homing start");
Same(GlanceCommands.ConfirmCalibration, new byte[] { 44 }, "homing confirm");
// 7. Malformed packets
foreach (var bytes in new[] { new byte[] { 8, 128 }, new byte[] { 18, 5, 1 }, new byte[] { 0 }, new byte[] { 11 }, new byte[] { 8, 255, 255, 255, 255, 255, 255, 255, 255, 255, 2 } })
    Throws(() => new WireMessage(bytes), "malformed " + string.Join(",", bytes));
// 8. Hand state
{
    var moving = new ClockHandState(new byte[] { 128 });
    Check(moving.Homing && !moving.AwaitingConfirmation, "homing");
    var waiting = new ClockHandState(new byte[] { 0, 4 });
    Check(waiting.AwaitingConfirmation && !waiting.Homing, "waiting");
    var failed = new ClockHandState(new byte[] { 64, 8 });
    Check(failed.HomingFailure && failed.MotorFailure, "failures");
    Throws(() => new ClockHandState(Array.Empty<byte>()), "empty state");
    Throws(() => new ClockHandState(new byte[] { 0, 0, 0 }), "long state");
}
// 9. Alarms
{
    var wake = new AlarmEntry { Hour = 7, Minute = 30, Days = 31, Sound = 6, Text = "Wake" };
    Same(GlanceCommands.Alarms(new[] { wake }), new byte[] { 4, 47, 0, 0, 0x0A, 0x12, 0x18, 0x1F, 0x22, 0x04, 0x08, 0x07, 0x10, 0x1E, 0x28, 0x06, 0x32, 0x06, 0x12, 0x04, 87, 97, 107, 101 }, "alarm golden");
    Same(GlanceCommands.Alarms(Array.Empty<AlarmEntry>()), new byte[] { 21, 0, 0, 0 }, "clear");
    Equal(GlanceCommands.Alarms(new[] { new AlarmEntry { Hour = 7, Minute = 30, Days = 0, Sound = 6, Text = "Wake" } })[6], (byte)0x22, "once omits days");
    var big = new AlarmEntry { Hour = 23, Minute = 59, Days = 127, Sound = 17, Text = new string('x', 16) };
    Check(GlanceCommands.Alarms(new[] { big, big, big, big }).Length <= 155, "four alarms fit");
    Throws(() => GlanceCommands.Alarms(new[] { big, big, big, big, big }), "five alarms");
    Throws(() => GlanceCommands.Alarms(new[] { new AlarmEntry { Hour = 24 } }), "hour 24");
    Throws(() => GlanceCommands.Alarms(new[] { new AlarmEntry { Text = new string('x', 17) } }), "long label");
    Same(GlanceCommands.AlarmStop, new byte[] { 20, 0, 0, 0 }, "alarm stop");
    Check(GlanceCommands.Notice("14:00", 0, 17, 12).Length > 0, "sound 17");
    Throws(() => GlanceCommands.Notice("14:00", 0, 18, 12), "sound 18");
}
// 10. Weather scenes match the bridge
{
    var f = HourlyForecast.FromOpenMeteo(Fixtures.Sydney);
    Equal(f.Hours.Count, 24, "hours"); Equal(f.Chance[0], 98, "chance");
    var now = DateTimeOffset.FromUnixTimeSeconds(1791868500);
    var t = WeatherScenes.Temperature(f, now, 39600, false, 5, 35)!;
    Equal(t.Timestamp, 1791867600L + 39600, "timestamp");
    Equal(t.Values[0], (short)19, "v0"); Equal(t.Values[1], (short)18, "v1"); Equal(t.Min, 14, "min"); Equal(t.Max, 19, "max");
    Equal(t.MinColor, WeatherScenes.Palette(9.0 / 30), "min colour"); Equal(t.MaxColor, WeatherScenes.Palette(14.0 / 30), "max colour");
    Same(t.Template, new byte[] { 0xC2, 0x8F, 0x08, 0xC2, 0xB0, 67 }, "template");
    var cmd = GlanceCommands.Forecast(1, t);
    Same(cmd[..4], new byte[] { 7, 16, 8, 1 }, "ring-only header");
    Equal(GlanceCommands.Forecast(1, t, ringOnly: false)[2], (byte)24, "ring+text");
    var m = new WireMessage(cmd[4..]);
    Equal(m.Integer(1), (ulong?)(1791867600 + 39600), "f1"); Equal(m.Integer(6), (ulong?)19, "f6"); Equal(m.Integer(7), (ulong?)14, "f7");
    var r = WeatherScenes.Rain(f, now, 39600, 0.3, 40, 6)!;
    Equal(r.Values[0], (short)0, "rain 0"); Equal(r.Values[1], (short)98, "rain 1");
    Check(WeatherScenes.Rain(f, now, 0, 50, 0, 12) is null, "no heavy rain");
    Check(WeatherScenes.Rain(f, now, 0, 0.3, 40, 1) is null, "look-ahead 1");
    Check(WeatherScenes.Temperature(f, now.AddHours(30), 0, false, 5, 35) is null, "stale forecast");
    t.Values[0] = -5; t.Min = -5;
    Equal(unchecked((int)new WireMessage(t.Encoded).Integer(7)!.Value), -5, "negative min");
    Equal(WeatherScenes.Palette(-1), 0x2060FFu, "cold"); Equal(WeatherScenes.Palette(2), 0xFF3000u, "hot");
    Same(GlanceCommands.DeleteScene(2), new byte[] { 33, 0, 0, 2 }, "delete scene");
}
// 11. Clock time offset (Settings field 18, firmware 1.6.7)
{
    var unknown = WireMessage.BytesField(30, new byte[] { 7, 7 });
    var original = new ClockSettings(Cat(Settings(), unknown));
    Equal(original.TimeOffsetMinutes, 0, "no offset");
    var back = original.PatchedTimeOffset(-90);
    Same(back[^12..], new byte[] { 0x90, 0x01, 0xA6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 }, "negative int32 varint");
    var stored = new ClockSettings(back[4..]);
    Equal(stored.TimeOffsetMinutes, -90, "decoded -90"); Equal(stored.Brightness, 128, "brightness kept");
    Same(stored.Raw[..(Settings().Length + unknown.Length)], Cat(Settings(), unknown), "original fields first");
    var forward = new ClockSettings(stored.PatchedTimeOffset(65)[4..]);
    Equal(forward.TimeOffsetMinutes, 65, "65");
    Equal(new WireMessage(forward.Raw).Fields.Count(x => x.Number == 18), 1, "single field 18");
    Equal(new ClockSettings(forward.Patched(D((2, 1)))[4..]).TimeOffsetMinutes, 65, "toggle keeps offset");
    Equal(new ClockSettings(forward.Patched(D(), new AutomaticBrightnessRange(30, 220))[4..]).TimeOffsetMinutes, 65, "range keeps offset");
    Throws(() => original.PatchedTimeOffset(1_000_001), "offset range");
    Throws(() => original.Patched(D((18, 5))), "field 18 not via Patched");
    Equal(ClockTimeCorrection.SupportsOffset("1.6.7"), true, "1.6.7");
    Equal(ClockTimeCorrection.SupportsOffset("1.6.8"), true, "1.6.8");
    Equal(ClockTimeCorrection.SupportsOffset("v2.0"), true, "2.0");
    Equal(ClockTimeCorrection.SupportsOffset("1.5.10"), false, "1.5.10");
    Equal(ClockTimeCorrection.SupportsOffset("1.6.6"), false, "1.6.6");
    Equal(ClockTimeCorrection.SupportsOffset(null), null, "null");
    Equal(ClockTimeCorrection.SupportsOffset("Glance"), null, "text");
}
// 12. Typed times, manual targets and the estimate
{
    var now = new DateTime(2026, 10, 14, 0, 2, 40);
    Equal(ClockTimeCorrection.Minutes(now, new WallTime(23, 58)), 4, "across midnight");
    Equal(ClockTimeCorrection.Minutes(now, new WallTime(1, 2)), -60, "an hour fast");
    Equal(ClockTimeCorrection.Minutes(now, new WallTime(0, 2)), 0, "matches");
    Equal(ClockTimeCorrection.Minutes(now, new WallTime(0, 0, new DateOnly(2026, 10, 12))), 2 * 1440 + 2, "with date");
    Equal(ClockTimeCorrection.Minutes(new DateTime(2026, 10, 4, 3, 10, 0), new WallTime(2, 10)), 60, "DST gap is wall-clock");
    Equal(ClockTimeCorrection.Minutes(now, new WallTime(24, 0)), null, "hour 24");
    Equal(ClockTimeCorrection.Minutes(now, new WallTime(9, 0), new WallTime(9, 30)), 30, "manual target");
    Equal(ClockTimeCorrection.Minutes(now, new WallTime(23, 50), new WallTime(0, 10)), 20, "manual across midnight");
    Equal(ClockTimeCorrection.Minutes(now, new WallTime(12, 0), new WallTime(12, 0, new DateOnly(2026, 10, 15))), 1440, "manual date");
    Equal(ClockTimeCorrection.Minutes(now, new WallTime(9, 0), new WallTime(9, 60)), null, "minute 60");
    foreach (var (text, h, mi) in new[] { ("18:05", 18, 5), ("6:05 pm", 18, 5), ("6.05PM", 18, 5), ("12:00am", 0, 0), ("12pm", 12, 0), ("1805", 18, 5), ("905", 9, 5), ("7", 7, 0), (" 07:30 ", 7, 30) })
        Equal(ClockTimeCorrection.ParseTime(text), ((int, int)?)(h, mi), $"parse '{text}'");
    foreach (var bad in new[] { "", "25:00", "12:60", "13pm", "0am", "7:5", "abc", "12345", "1:2:3" })
        Equal(ClockTimeCorrection.ParseTime(bad), null, $"reject '{bad}'");
    Equal(ClockTimeCorrection.EstimatedClockTime(now, 62, 0), now.AddMinutes(62), "estimate");
    int learned = ClockTimeCorrection.Baseline(-3, 0);
    Equal(ClockTimeCorrection.EstimatedClockTime(now, -3, learned), now, "learned baseline");
    Equal(ClockTimeCorrection.EstimatedClockTime(now, 7, learned), now.AddMinutes(10), "learned + 10");
    Equal(ClockTimeCorrection.Describe(-61), "back 1 hour 1 minute", "describe back");
    Equal(ClockTimeCorrection.Describe(2882), "forward 2 days 2 minutes", "describe forward");
}

if (failures > 0) { Console.WriteLine($"FAILED: {failures} check(s)"); return 1; }
Console.WriteLine("PASS: 12 check groups (discovery filtering, settings preservation, validation, automatic brightness range, notices, timers, malformed packets, hand-state decoding, alarms, weather scenes, clock time offset, typed/manual time and estimate).");
return 0;
