using System.Globalization;
using System.Net.Http;
using System.Text.Json;
using Avalonia.Threading;
using GlanceProtocol;

namespace OpenGlance;

/// The clock cannot report its alarms, so the list lives on this PC. Every change
/// replaces the whole list on the clock (as the original app did).
public sealed class AlarmStore
{
    public event Action? Changed;
    readonly ClockController clock;
    readonly DispatcherTimer retry;
    public IReadOnlyList<AlarmEntry> Alarms => Store.State.Alarms;
    public bool Pending => Store.State.AlarmsPending;

    public AlarmStore(ClockController clock)
    {
        this.clock = clock;
        clock.OnClockReady.Add(SyncIfPending);
        // Bluetooth sends are one at a time; retry if another send was in progress.
        retry = new DispatcherTimer { Interval = TimeSpan.FromSeconds(10) };
        retry.Tick += (_, _) => { if (Pending && clock.IsConnected && !clock.IsBusy) SyncIfPending(); };
        retry.Start();
    }
    public void Save(AlarmEntry alarm)
    {
        int i = Store.State.Alarms.FindIndex(a => a.Id == alarm.Id);
        if (i >= 0) Store.State.Alarms[i] = alarm; else if (Store.State.Alarms.Count < 4) Store.State.Alarms.Add(alarm);
        Modified();
    }
    public void SetEnabled(AlarmEntry alarm, bool enabled)
    {
        var a = Store.State.Alarms.Find(x => x.Id == alarm.Id);
        if (a is null) return;
        a.Enabled = enabled; Modified();
    }
    public void Delete(AlarmEntry alarm) { Store.State.Alarms.RemoveAll(a => a.Id == alarm.Id); Modified(); }
    public void DeleteAll() { Store.State.Alarms.Clear(); Modified(); }
    void Modified()
    {
        Store.State.AlarmsPending = true; Store.Save();
        Changed?.Invoke();
        SyncIfPending();
    }
    public void SyncIfPending()
    {
        if (!Pending) return;
        byte[] data;
        var active = Alarms.Where(a => a.Enabled).ToList();
        try { data = GlanceCommands.Alarms(active); }
        catch { clock.Log("Alarm list is not valid; nothing was sent", error: true); return; }
        clock.SendQuietly(data, active.Count == 0 ? "All alarms on the clock cleared" : $"Alarms on the clock replaced: {active.Count} active", completion: ok =>
        {
            if (!ok) return;
            Store.State.AlarmsPending = false; Store.Save(); Changed?.Invoke();
        });
    }
}

public sealed class ChimeService
{
    readonly ClockController clock;
    readonly DispatcherTimer timer;
    int lastHour = DateTime.Now.Hour;   // never chime for the hour already under way

    public bool On { get => Store.State.ChimeOn; set { Store.State.ChimeOn = value; Store.Save(); } }
    public int Sound { get => Store.State.ChimeSound; set { Store.State.ChimeSound = value; Store.Save(); } }
    public int QuietFrom { get => Store.State.QuietFrom; set { Store.State.QuietFrom = value; Store.Save(); } }
    public int QuietTo { get => Store.State.QuietTo; set { Store.State.QuietTo = value; Store.Save(); } }

    public ChimeService(ClockController clock)
    {
        this.clock = clock;
        timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(5) };
        timer.Tick += (_, _) => Tick();
        timer.Start();
    }
    public bool IsQuiet(int hour)
    {
        if (QuietFrom == QuietTo) return false;
        return QuietFrom < QuietTo ? hour >= QuietFrom && hour < QuietTo : hour >= QuietFrom || hour < QuietTo;
    }
    void Tick()
    {
        var now = DateTime.Now;
        int hour = now.Hour;
        if (now.Minute != 0 || hour == lastHour) return;
        lastHour = hour;
        int previousHour = (hour + 23) % 24;
        if (!On || IsQuiet(hour) || clock.CalibrationBusy || (clock.TimerEnd is DateTime end && end > now)) return;
        var text = clock.Settings?.TwelveHour == true ? $"{(hour % 12 == 0 ? 12 : hour % 12)}{(hour < 12 ? "AM" : "PM")}" : $"{hour:00}:00";
        try
        {
            var packet = GlanceCommands.Notice(text, 0, Sound, 12);
            clock.AnnounceDisplayCommand(90);
            clock.SendQuietly(packet, $"Hourly chime {text}", completion: ok =>
            {
                if (!ok && clock.IsConnected) lastHour = previousHour;   // try again a few seconds later this minute
            });
        }
        catch (ProtocolException) { }
    }
}

/// Fetches Open-Meteo every 30 minutes while the app is open and keeps one
/// weather ring (slot 1) on the clock, exactly like the ESP32 bridge.
public sealed class WeatherService
{
    public event Action? Changed;
    readonly ClockController clock;
    static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(20) };
    public HourlyForecast? Forecast { get; private set; }
    public DateTime? Fetched { get; private set; }
    public DateTime? Sent { get; private set; }
    public string? Error { get; private set; }
    public int Showing { get; private set; }       // 0 nothing, 1 temperature, 2 rain
    public bool RainDue { get; private set; }
    bool phaseRain, oldSlotCleanup = true, retryPending;
    DateTime nextSwap = DateTime.MinValue;
    int lastHour = -1;
    byte[]? lastCommand;
    public bool Demo { get; set; }
    /// True while the seconds pixel owns the ring slot; weather waits (as on the bridge).
    public Func<bool> RingTaken { get; set; } = () => false;
    /// The seconds pixel released the slot: send the weather ring again.
    public void RingReleased() { lastCommand = null; Showing = 0; Send(user: false); }

    public WeatherOptions Options => Store.State.Weather;
    public WeatherPlace? Place => Store.State.Place;

    public WeatherService(ClockController clock)
    {
        this.clock = clock;
        clock.OnClockReady.Add(() => { lastCommand = null; Send(user: false); });
        var fetchTimer = new DispatcherTimer { Interval = TimeSpan.FromMinutes(30) };
        fetchTimer.Tick += async (_, _) => await Fetch();
        fetchTimer.Start();
        var tick = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        tick.Tick += (_, _) => Tick();
        tick.Start();
        _ = Fetch();
    }

    public void Update(Func<WeatherOptions, WeatherOptions> change)
    {
        var old = Options;
        var updated = change(old);
        if (updated == old) return;
        Store.State.Weather = updated; Store.Save();
        Changed?.Invoke();
        if (updated.Fahrenheit != old.Fahrenheit || (updated.On && !old.On)) _ = Fetch(user: true); else Send(user: true);
    }
    public void SetPlace(WeatherPlace place)
    {
        Store.State.Place = place;
        Store.State.Weather = Options with { On = true };
        Store.Save(); Changed?.Invoke();
        _ = Fetch(user: true);
    }

    public async Task Fetch(bool user = false)
    {
        if (Demo || !Options.On || Place is null) return;
        var o = Options; var p = Place;
        var url = "https://api.open-meteo.com/v1/forecast?latitude=" + p.Latitude.ToString("0.0000", CultureInfo.InvariantCulture)
            + "&longitude=" + p.Longitude.ToString("0.0000", CultureInfo.InvariantCulture)
            + "&hourly=temperature_2m,precipitation_probability,precipitation&current=temperature_2m&forecast_hours=36&timeformat=unixtime"
            + (o.Fahrenheit ? "&temperature_unit=fahrenheit" : "");
        try
        {
            var json = await Http.GetStringAsync(url);
            Forecast = HourlyForecast.FromOpenMeteo(json);
            Fetched = DateTime.Now; Error = null;
            Changed?.Invoke();
            Send(user);
        }
        catch (Exception e)
        {
            Error = "Weather update failed: " + e.Message;
            clock.Log("Weather update failed", e.Message, error: true);
            Changed?.Invoke();
        }
    }

    public static async Task<List<WeatherPlace>> Search(string query)
    {
        var url = "https://geocoding-api.open-meteo.com/v1/search?count=8&language=en&name=" + Uri.EscapeDataString(query);
        using var doc = JsonDocument.Parse(await Http.GetStringAsync(url));
        var list = new List<WeatherPlace>();
        if (!doc.RootElement.TryGetProperty("results", out var results)) return list;
        foreach (var r in results.EnumerateArray())
        {
            string? S(string k) => r.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;
            var name = string.Join(", ", new[] { S("name"), S("admin1"), S("country") }.Where(x => !string.IsNullOrEmpty(x)));
            list.Add(new WeatherPlace(name, r.GetProperty("latitude").GetDouble(), r.GetProperty("longitude").GetDouble()));
        }
        return list;
    }

    void Tick()
    {
        var now = DateTime.Now;
        if (retryPending && clock.IsConnected && !clock.IsBusy) { retryPending = false; Send(user: false); }
        if (lastHour < 0) lastHour = now.Hour;
        else if (now.Hour != lastHour && now.Minute >= 1) { lastHour = now.Hour; Send(user: false); }   // advance the window
        if (Options.On && Options.RainMode == 1 && Options.Temperature && RainDue && now >= nextSwap)
        {
            phaseRain = !phaseRain;
            nextSwap = now.AddSeconds(Options.Cycle);
            Send(user: false);
        }
    }

    public void Send(bool user)
    {
        if (RingTaken()) return;
        if (!clock.IsConnected || clock.Settings is null || clock.CalibrationBusy) return;
        if (clock.IsBusy) { retryPending = true; return; }
        var now = DateTimeOffset.Now;
        int offset = (int)TimeZoneInfo.Local.GetUtcOffset(now).TotalSeconds;
        var o = Options;
        ForecastScene? temp = null, rain = null;
        if (Forecast is HourlyForecast f)
        {
            if (o.Temperature) temp = WeatherScenes.Temperature(f, now, offset, o.Fahrenheit, o.ColdAt, o.HotAt);
            if (o.RainMode != 0) rain = WeatherScenes.Rain(f, now, offset, o.RainMm, o.RainChance, o.RainHours);
        }
        bool due = rain is not null;
        if (!due) phaseRain = false;
        int show = 0;
        if (o.On)
        {
            if (due && (o.RainMode == 2 || temp is null)) show = 2;
            else if (due && o.RainMode == 1) show = phaseRain ? 2 : 1;
            else if (temp is not null) show = 1;
        }
        if (o.On && oldSlotCleanup)
        {
            oldSlotCleanup = false;
            clock.SendQuietly(GlanceCommands.DeleteScene(2), "Remove any second weather scene", logSuccess: false);
        }
        byte[]? command = show == 0
            ? (Showing != 0 || (lastCommand is null && o.On) ? GlanceCommands.DeleteScene(1) : null)
            : GlanceCommands.Forecast(1, show == 2 ? rain! : temp!, ringOnly: !o.Text);
        bool dueChanged = due != RainDue;
        RainDue = due;
        if (command is null || (!user && lastCommand is not null && command.SequenceEqual(lastCommand))) { Showing = show; Changed?.Invoke(); return; }
        var label = show == 2 ? "Rain forecast" : show == 1 ? "Temperature forecast" : "Weather removed";
        clock.SendQuietly(command, label, logSuccess: user || dueChanged || (show == 0) != (Showing == 0), completion: ok =>
        {
            if (!ok) { retryPending = clock.IsConnected; return; }
            lastCommand = command; Showing = show; Sent = DateTime.Now;
            if (user && show != 0) clock.SendQuietly(GlanceCommands.RefreshDisplay, "Refresh display", logSuccess: false);
            Changed?.Invoke();
        });
    }

    /// Next 12 hours for the preview.
    public List<(string Label, double Temp, int Rain)> PreviewHours()
    {
        var list = new List<(string, double, int)>();
        if (Forecast is not HourlyForecast f || f.StartIndex(DateTimeOffset.Now) is not int start) return list;
        for (int i = 0; i < 12 && start + i < f.Hours.Count; i++)
        {
            int k = start + i;
            list.Add((f.Hours[k].ToLocalTime().ToString("HH", CultureInfo.InvariantCulture), f.Temperature[k],
                WeatherScenes.RainValue(f.Rain[k], f.Chance[k], Options.RainMm, Options.RainChance)));
        }
        return list;
    }

    public void LoadDemo(HourlyForecast forecast) { Demo = true; Forecast = forecast; Fetched = DateTime.Now; Showing = 1; }
}

/// A single ticking pixel on the ring, drawn by the clock from ~10-second scenes (SecondsScene), while
/// this app is open and connected. A port of the bridge's updateComet() (and the Mac app's
/// SecondsService): same scenes, pauses, slot sharing with weather and error latching.
public sealed class SecondsService
{
    public event Action? Changed;
    readonly ClockController clock;
    readonly AlarmStore alarms;
    readonly ChimeService chime;
    readonly WeatherService weather;
    readonly DispatcherTimer timer;
    bool ownsSlot, inFlight, wasTimer, releasing;
    long nextStep = -1;
    DateTime holdUntil = DateTime.MinValue, lastStateRead = DateTime.MinValue;

    public bool On => Store.State.SecondsOn;
    public int Color => Store.State.SecondsColor;
    public int Rings => Store.State.SecondsRings;
    public string Status { get; private set; } = "Off — weather ring available";
    public bool Running { get; private set; }
    public bool Failed { get; private set; }

    public SecondsService(ClockController clock, AlarmStore alarms, ChimeService chime, WeatherService weather)
    {
        this.clock = clock; this.alarms = alarms; this.chime = chime; this.weather = weather;
        if (!SecondsScene.ValidColor(Color)) Store.State.SecondsColor = 18;
        if (!SecondsScene.ValidRings(Rings)) Store.State.SecondsRings = SecondsScene.OuterRing;
        weather.RingTaken = () => On;
        clock.OnDisplayCommand.Add(quiet => Suspend(quiet));
        timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
        timer.Tick += (_, _) => Tick();
        timer.Start();
    }

    /// Saves the options. Like the bridge's /api/comet: pause first, clear any error, start again.
    public void Apply(bool on, int color, int rings)
    {
        if (!SecondsScene.ValidColor(color) || !SecondsScene.ValidRings(rings)) return;
        Suspend();
        Store.State.SecondsOn = on; Store.State.SecondsColor = color; Store.State.SecondsRings = rings; Store.Save();
        Failed = false; nextStep = -1;
        var name = SecondsScene.Colours.FirstOrDefault(c => c.Id == color).Name ?? color.ToString();
        clock.Log(on ? $"Seconds on, colour {name}, LEDs {rings}" : "Seconds off; restoring weather");
        if (!on) weather.RingReleased();   // queued after the delete above
        Changed?.Invoke();
    }
    public void Retry() => Apply(On, Color, Rings);

    /// Removes the seconds scene before a disconnect or quit, so no stopped pixel is left on the ring.
    public void Release(Action done)
    {
        releasing = true; nextStep = -1;
        if (!ownsSlot || !clock.IsConnected || clock.DemoMode) { done(); return; }
        bool finished = false;
        void Finish() { if (!finished) { finished = true; done(); } }
        clock.SendQuietly(GlanceCommands.DeleteScene(SecondsScene.Slot), "Remove seconds scene", logSuccess: false, completion: ok => { if (ok) ownsSlot = false; Finish(); });
        DispatcherTimer.RunOnce(Finish, TimeSpan.FromSeconds(1.5));
    }

    void SetStatus(string text, bool running = false)
    {
        if (Status == text && Running == running) return;
        Status = text; Running = running; Changed?.Invoke();
    }
    /// Removes our scene (if any) and optionally holds off for `hold` seconds.
    public void Suspend(double hold = 0)
    {
        if (hold > 0 && DateTime.Now.AddSeconds(hold) > holdUntil) holdUntil = DateTime.Now.AddSeconds(hold);
        nextStep = -1;
        if (!ownsSlot || !clock.IsConnected || clock.DemoMode) return;
        ownsSlot = false;
        clock.SendQuietly(GlanceCommands.DeleteScene(SecondsScene.Slot), "Pause seconds", logSuccess: false, completion: ok =>
        {
            if (!ok && clock.IsConnected) { ownsSlot = true; LatchFailure("Pausing the seconds failed"); }
        });
    }
    void LatchFailure(string why)
    {
        Failed = true;
        SetStatus("Stopped after a Bluetooth error — press Retry");
        clock.Log("Seconds stopped", why + ". Press Retry on the Display tab.", error: true);
    }

    static int FrameAt(DateTime utc)
    {
        long ticks = utc.Ticks;
        return (int)(ticks / TimeSpan.TicksPerSecond % 60) * SecondsScene.Fps + Math.Min(49, (int)(ticks % TimeSpan.TicksPerSecond * SecondsScene.Fps / TimeSpan.TicksPerSecond));
    }
    static long StepAt(DateTime utc) => utc.Ticks / TimeSpan.TicksPerMinute * SecondsScene.Pixels + SecondsScene.PixelAt(FrameAt(utc));

    async void Tick()
    {
        if (releasing) { if (!clock.IsConnected) releasing = false; return; }   // until the disconnect lands
        if (inFlight) return;
        if (!On)
        {
            if (ownsSlot) Suspend();
            if (!Failed) SetStatus("Off — weather ring available");
            return;
        }
        if (clock.DemoMode) { SetStatus("Demo · seconds preview only, nothing is sent"); return; }
        if (Failed) { SetStatus("Stopped after a Bluetooth error — press Retry"); return; }
        if (!clock.IsConnected || clock.Settings is null) { ownsSlot = false; nextStep = -1; SetStatus("Waiting for the clock"); return; }
        var now = DateTime.Now;
        // Read-only state checks: quicker while paused, slower while playing.
        bool blockedState = clock.HandState is null || clock.CalibrationBusy || clock.DoNotDisturbActive;
        if ((now - lastStateRead).TotalSeconds >= (blockedState ? 2 : 5)) { lastStateRead = now; clock.ReadHandState(); }
        if (clock.HandStateUpdated is not DateTime updated || (now - updated).TotalSeconds > 12) { Suspend(); SetStatus("Paused — waiting for fresh clock status"); return; }
        int day = ((int)now.DayOfWeek + 6) % 7;   // Monday = 0
        bool alarmDue = alarms.Alarms.Any(a => a.Enabled && a.Hour == now.Hour && a.Minute == now.Minute && (a.Days == 0 || (a.Days & (1 << day)) != 0));
        bool chimeDue = now.Minute == 0 && ((clock.Settings?.BuiltInChimeSound ?? 0) != 0 || (chime.On && !chime.IsQuiet(now.Hour)));
        bool timerRunning = clock.TimerEnd is DateTime end && end > now;
        if (wasTimer && !timerRunning && now.AddSeconds(15) > holdUntil) holdUntil = now.AddSeconds(15);   // timer-complete sound/animation
        wasTimer = timerRunning;
        bool hands = clock.CalibrationBusy, dnd = clock.DoNotDisturbActive;
        if (hands || timerRunning || dnd || alarmDue || chimeDue || holdUntil > now)
        {
            Suspend();
            SetStatus(hands ? "Paused for hand calibration" : timerRunning ? "Paused for the timer" : dnd ? "Paused — clock Do Not Disturb"
                : alarmDue || chimeDue ? "Paused for an alarm or hourly chime" : "Paused for a message or timer finish");
            return;
        }
        long step = StepAt(DateTime.UtcNow);
        if (ownsSlot && nextStep >= 0 && step < nextStep && step > nextStep - 2 * SecondsScene.Pixels) return;   // the scene still draws by itself
        if (!clock.CanReadFace) { Suspend(); SetStatus("Unavailable: clock display status cannot be read"); return; }
        inFlight = true;
        // Read the active face before each scene; an unknown face fails closed.
        var face = await clock.ReadFace();
        if (face is not int f)
        {
            inFlight = false;
            if (clock.IsConnected) { Suspend(); LatchFailure("Could not read the active clock face"); }   // a dropped link just resets
            return;
        }
        if (SecondsScene.AlertFace(f)) { inFlight = false; Suspend(5); SetStatus("Paused for the clock's alert display"); return; }
        SendScene();
    }

    void SendScene()
    {
        var utc = DateTime.UtcNow;
        int frame = FrameAt(utc); long step = StepAt(utc);
        bool continuation = ownsSlot && step == nextStep, starting = !ownsSlot;
        if (!On || Failed || SecondsScene.Scene(frame, Color, continuation, Rings) is not (byte[] data, int endPixel)) { inFlight = false; return; }
        void Finish(bool ok)
        {
            inFlight = false;
            if (!ok)
            {
                if (!clock.IsConnected) { ownsSlot = false; nextStep = -1; return; }   // link dropped: start fresh on reconnect
                ownsSlot = true; LatchFailure("A seconds scene was not acknowledged"); return;   // it may have acted; Off/Retry cleans up
            }
            ownsSlot = true; nextStep = step + (endPixel - SecondsScene.PixelAt(frame));
            SetStatus($"Playing · single pixel, new scene about every {(SecondsScene.SceneSteps[Rings] == 8 ? "10" : "7.5")} s", running: true);
            if (starting) clock.Log("Seconds started", "Ring only, silent. Runs while this app is open.");
        }
        void Write() => clock.SendQuietly(data, "Seconds scene", logSuccess: false, completion: ok =>
        {
            // Start once (command 31, the reference's silent start; not 35, the cloud-fetch animation).
            if (ok && starting) clock.SendQuietly(new byte[] { 31, 0, 0, 0 }, "Start seconds scene", logSuccess: false, completion: Finish);
            else Finish(ok);
        });
        if (!starting) { Write(); return; }
        // Weather forecasts are stored apart from custom scenes, so remove both weather slots first.
        clock.SendQuietly(GlanceCommands.DeleteScene(2), "Remove old weather scene (slot 2)", logSuccess: false, completion: ok =>
        {
            if (!ok) { Finish(false); return; }
            clock.SendQuietly(GlanceCommands.DeleteScene(1), "Remove old weather scene (slot 1)", logSuccess: false, completion: ok2 => { if (ok2) Write(); else Finish(false); });
        });
    }
}
