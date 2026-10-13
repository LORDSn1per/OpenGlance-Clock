using Avalonia.Threading;
using GlanceProtocol;

namespace OpenGlance;

public sealed record NearbyClock(ulong Address, string Name, int Signal)
{
    public string SignalLabel => Signal > -60 ? "Nearby" : Signal > -80 ? "In range" : "Weak signal";
}
public sealed record ActivityItem(DateTime Date, string Title, string Detail, bool IsError);

/// Port of the Mac app's BluetoothController. All state lives on the UI thread; `Changed` fires after updates.
public sealed class ClockController
{
    public event Action? Changed;
    public List<NearbyClock> Nearby { get; } = new();
    public bool IsScanning { get; private set; }
    public bool IsConnecting { get; private set; }
    public bool IsConnected { get; private set; }
    public bool IsBusy { get; private set; }
    public string Status { get; private set; } = "Ready when you are";
    public ClockSettings? Settings { get; private set; }
    public int? Battery { get; private set; }
    public string? FirmwareVersion { get; private set; }
    public string ClockName { get; private set; } = "Your Glance Clock";
    public List<ActivityItem> Events { get; } = new();
    public bool DemoMode { get; private set; }
    public DateTime? TimerEnd { get; private set; }
    public bool NeedsCalibrationConfirmation { get; private set; }
    public bool IsCalibrating { get; private set; }
    public string HandStatus { get; private set; } = "Hand position not read";
    /// Latest hand/state flags (5075fc78) and when they were read or notified.
    public ClockHandState? HandState { get; private set; }
    public DateTime? HandStateUpdated { get; private set; }
    public bool DoNotDisturbActive => HandState?.DoNotDisturb ?? false;
    /// Called before a command that takes over the display (message, timer, calibration…), with the
    /// seconds of quiet it needs, so the seconds pixel can step aside first (as the bridge does).
    public List<Action<double>> OnDisplayCommand { get; } = new();
    public void AnnounceDisplayCommand(double quiet) { foreach (var a in OnDisplayCommand.ToArray()) a(quiet); }
    public string? RememberedName => Store.State.ClockName;
    public bool HasRememberedClock => Store.State.ClockAddress is not null;
    public bool BluetoothAvailable => radio is not null;
    public bool AutoConnect
    {
        get => Store.State.AutoConnect;
        set { Store.State.AutoConnect = value; Store.Save(); if (!value) retryTimer?.Stop(); Notify(); }
    }
    public int TimeBaseline
    {
        get => Store.State.TimeBaseline;
        private set { Store.State.TimeBaseline = value; Store.Save(); }
    }
    /// Shown to the owner as a dialog (errors from explicit actions only).
    public Func<string, Task>? ShowError;
    /// Asks the owner for the PIN shown on the clock; null if cancelled.
    public Func<Task<string?>>? AskPin;
    /// Called each time the clock connects and its settings have been read.
    public List<Action> OnClockReady { get; } = new();

    public bool CanControl => DemoMode || (IsConnected && Settings is not null && !IsBusy);
    public bool CalibrationBusy => IsCalibrating || NeedsCalibrationConfirmation;
    public bool? TimeCorrectionSupported => ClockTimeCorrection.SupportsOffset(FirmwareVersion);

    readonly IClockRadio? radio = Radio.Create();
    IClockLink? link;
    int generation;
    bool startupAttempted, autoSuspended, automaticAttempt;
    int retryCount;
    DispatcherTimer? retryTimer, scanTimer;
    readonly SemaphoreSlim gate = new(1, 1);

    public ClockController() => Log("Welcome to OpenGlance", "Your saved clock reconnects automatically when enabled.");

    void Notify() => Changed?.Invoke();
    static void Ui(Action a) { if (Dispatcher.UIThread.CheckAccess()) a(); else Dispatcher.UIThread.Post(a); }

    public void Log(string title, string detail = "", bool error = false)
    {
        Events.Insert(0, new ActivityItem(DateTime.Now, title, detail, error));
        if (Events.Count > 100) Events.RemoveRange(100, Events.Count - 100);
        Notify();
    }
    void Fail(string message)
    {
        Log("Something needs attention", message, error: true);
        if (!automaticAttempt) ShowError?.Invoke(message);
    }

    public void ConnectSavedOnLaunch()
    {
        if (startupAttempted) return; startupAttempted = true;
        if (!AutoConnect || !HasRememberedClock || DemoMode) return;
        ConnectSaved(automatic: true);
    }
    void ScheduleReconnect()
    {
        if (!AutoConnect || autoSuspended || DemoMode || !HasRememberedClock || retryCount >= 3) return;
        retryCount++;
        retryTimer?.Stop();
        retryTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(retryCount * 30) };
        retryTimer.Tick += (_, _) => { retryTimer?.Stop(); if (AutoConnect && !autoSuspended && !DemoMode) ConnectSaved(automatic: true); };
        retryTimer.Start();
    }

    public void SetDemo(bool enabled)
    {
        Disconnect();
        DemoMode = enabled;
        Nearby.Clear();
        if (enabled)
        {
            ClockName = "Studio clock";
            Settings = new ClockSettings(new byte[] { 16, 1, 32, 0, 40, 1, 72, 1, 80, 128, 1, 88, 1, 96, 0, 10, 6, 8, 1, 16, 22, 24, 7 });   // DND 22–07
            Battery = 84; FirmwareVersion = "1.6.7";
            Status = "Demo mode · no Bluetooth commands";
            Log("Demo mode", "Try the controls. No clock is connected and no Bluetooth commands are sent.");
        }
        else { Settings = null; Battery = null; FirmwareVersion = null; ClockName = "Your Glance Clock"; Status = "Ready when you are"; }
        Notify();
    }

    public void Scan()
    {
        if (DemoMode) SetDemo(false);
        if (IsConnected || IsConnecting) return;
        if (radio is null) { Fail("Bluetooth control needs Windows 10 (version 2004) or later."); return; }
        automaticAttempt = false; autoSuspended = false; retryCount = 0; retryTimer?.Stop();
        Nearby.Clear(); IsScanning = true; Status = "Looking for your clock…";
        Log("Searching nearby", "Only Glance clocks appear in the list.");
        try
        {
            radio.StartScan(s => Ui(() =>
            {
                if (!IsScanning || !ClockDiscovery.IsGlance(s.Name, s.Services)) return;
                var item = new NearbyClock(s.Address, s.Name ?? "Glance Clock", s.Signal);
                Nearby.RemoveAll(n => n.Address == item.Address); Nearby.Add(item);
                Nearby.Sort((a, b) => b.Signal.CompareTo(a.Signal));
                Notify();
            }));
        }
        catch (Exception e) { IsScanning = false; Fail("Bluetooth is not available: " + e.Message + " Turn Bluetooth on in Windows Settings, then search again."); return; }
        scanTimer?.Stop();
        scanTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(15) };
        scanTimer.Tick += (_, _) => StopScan();
        scanTimer.Start();
        Notify();
    }
    public void StopScan()
    {
        scanTimer?.Stop();
        radio?.StopScan();
        if (!IsScanning) return;
        IsScanning = false;
        if (!IsConnecting && !IsConnected) Status = Nearby.Count == 0 ? "No clock found yet" : "Choose your clock";
        Notify();
    }

    public void ConnectSaved(bool automatic = false)
    {
        if (Store.State.ClockAddress is ulong address) Connect(address, Store.State.ClockName ?? "Glance Clock", automatic);
    }

    public async void Connect(ulong address, string name, bool automatic = false)
    {
        if (DemoMode || radio is null || IsConnecting || IsConnected) return;
        automaticAttempt = automatic;
        if (!automatic) { autoSuspended = false; retryCount = 0; }
        retryTimer?.Stop();
        StopScan();
        IsConnecting = true; ClockName = name; Settings = null; Battery = null; FirmwareVersion = null;
        Status = "Connecting · enter the clock's PIN if asked";
        Log($"Connecting to {name}", "If asked, enter the PIN shown on the clock. There is no Glance account to sign in to.");
        Notify();
        int myGeneration = ++generation;
        using var cancel = new CancellationTokenSource(TimeSpan.FromSeconds(60));
        try
        {
            var opened = await radio.ConnectAsync(address, () => AskPin?.Invoke() ?? Task.FromResult<string?>(null), cancel.Token);
            if (myGeneration != generation) { opened.Dispose(); return; }
            link = opened;
            link.Disconnected += () => Ui(() => { if (link == opened) { Disconnect(userInitiated: false); Log("Clock disconnected", "Automatic reconnection will try your saved clock when enabled."); } });
            link.StateChanged += bytes => Ui(() => { if (link == opened) UpdateHandState(bytes); });
            ClockName = opened.Name;
            IsConnected = true; IsConnecting = false;
            Notify();
            RefreshSettings(address);
            var battery = await Op(l => l.ReadBatteryAsync());
            if (battery is int b) Battery = b;
            var firmware = await Op(l => l.ReadFirmwareAsync());
            if (firmware is string f)
            {
                FirmwareVersion = f;
                Log($"Clock firmware {f}", ClockTimeCorrection.SupportsOffset(f) == true
                    ? "Time correction from this PC is available (firmware 1.6.7 or later)."
                    : "Time correction from this PC needs firmware 1.6.7 or later.");
            }
            var state = await Op(l => l.ReadStateAsync());
            if (state is not null) UpdateHandState(state);
            Notify();
        }
        catch (Exception e)
        {
            if (myGeneration != generation) return;
            Disconnect(userInitiated: false);
            Fail(e is OperationCanceledException
                ? "The clock did not connect. Briefly press the clock's button to activate pairing mode, then choose Connect again. Keep it nearby and close the phone app or bridge if connected. Do not hold the button or reset the clock."
                : e.Message);
        }
    }

    public void Disconnect(bool userInitiated = true)
    {
        if (userInitiated) { autoSuspended = true; automaticAttempt = false; retryTimer?.Stop(); }
        generation++;
        StopScan();
        var old = link; link = null;
        IsConnecting = false; IsConnected = false; Settings = null; Battery = null; FirmwareVersion = null; IsBusy = false;
        NeedsCalibrationConfirmation = false; IsCalibrating = false; HandStatus = "Hand position not read";
        HandState = null; HandStateUpdated = null;
        try { old?.Dispose(); } catch { }
        if (!DemoMode) Status = "Disconnected";
        Notify();
        if (!userInitiated) ScheduleReconnect();
    }

    public void ForgetClock()
    {
        Disconnect();
        Store.State.ClockAddress = null; Store.State.ClockName = null; Store.State.TimeBaseline = 0; Store.Save();
        Log("Saved clock removed", "This removes the app's remembered selection. To remove pairing, use Windows Settings → Bluetooth & devices.");
    }

    /// Runs one Bluetooth operation at a time, with a timeout. Null result if there is no link.
    async Task<T?> Op<T>(Func<IClockLink, Task<T>> action)
    {
        var current = link ?? throw new InvalidOperationException("Connect your clock first.");
        await gate.WaitAsync();
        try
        {
            var task = action(current);
            if (await Task.WhenAny(task, Task.Delay(TimeSpan.FromSeconds(60))) != task)
            {
                Disconnect(userInitiated: false);
                throw new InvalidOperationException("Bluetooth did not respond. Complete any PIN prompt, then reconnect. No command is retried automatically.");
            }
            return await task;
        }
        finally { gate.Release(); }
    }
    Task Write(byte[] data)
    {
        if (link is not null && data.Length > link.MaxWrite) throw new InvalidOperationException("This message is too long for the current Bluetooth connection. Try a shorter message.");
        return Op(async l => { await l.WriteControlAsync(data); return true; });
    }
    async Task<ClockSettings> ReadSettings() => new ClockSettings(await Op(l => l.ReadControlAsync()) ?? throw new ProtocolException(ProtocolException.Kind.Malformed));

    public async void RefreshSettings(ulong? rememberAddress = null)
    {
        if (DemoMode || !IsConnected || IsBusy) return;
        IsBusy = true; Notify();
        try
        {
            var s = await ReadSettings();
            automaticAttempt = false; retryCount = 0; retryTimer?.Stop();
            Settings = s; Status = "Connected · local Bluetooth";
            if (rememberAddress is ulong a) { Store.State.ClockAddress = a; Store.State.ClockName = ClockName; Store.Save(); }
            Log("Clock settings read", "Authenticated access is working. Settings are preserved before each change.");
            IsBusy = false; Notify();
            foreach (var ready in OnClockReady.ToArray()) ready();
        }
        catch (Exception e) { IsBusy = false; Fail("Could not read clock settings: " + e.Message); }
        finally { if (IsBusy) { IsBusy = false; } Notify(); }
    }

    public void SetAutomaticBrightness(int low, int high)
    {
        try { ChangeSettings(new Dictionary<int, ulong>(), new AutomaticBrightnessRange(low, high)); }
        catch (ProtocolException) { Fail("Brightness limits must be 0–255, with minimum no higher than maximum."); }
    }

    public async void ChangeSettings(Dictionary<int, ulong> changes, AutomaticBrightnessRange? range = null)
    {
        if (!CanControl) return;
        if (DemoMode)
        {
            try { if (Settings is not null) Settings = new ClockSettings(Settings.Patched(changes, range)[4..]); Log("Demo · settings updated"); }
            catch (Exception e) { Fail(e.Message); }
            return;
        }
        IsBusy = true; Notify();
        try
        {
            var command = (await ReadSettings()).Patched(changes, range);
            await Write(command);
            var updated = await ReadSettings();
            Settings = updated;
            if (updated.Matches(changes, range)) Log("Settings confirmed", "The clock returned the new values, including the full automatic brightness range when changed.");
            else Fail("The write was accepted, but the clock has not returned the requested values. Try Refresh.");
        }
        catch (Exception e) { Fail(e.Message); }
        finally { IsBusy = false; Notify(); }
    }

    /// Writes one night schedule (Do Not Disturb or Silent): fresh read, replace only that schedule and its
    /// permanent flag, write, read back and verify. Same bytes as the bridge's /api/night.
    public async void SetSchedule(ScheduleKind kind, int mode, int from, int till)
    {
        if (!CanControl) return;
        var name = kind == ScheduleKind.DoNotDisturb ? "Do Not Disturb" : "Silent hours";
        var what = name + ": " + (mode == 0 ? "off" : mode == 2 ? "always on" : $"{from:00}:00–{till:00}:00");
        if (!ClockSchedule.IsValid(mode, from, till)) { Fail("Choose Off, Scheduled or Always, and different start and end hours."); return; }
        if (DemoMode)
        {
            try { if (Settings is not null) Settings = new ClockSettings(Settings.PatchedSchedule(kind, mode, from, till)[4..]); Log("Demo · " + what.ToLowerInvariant(), "No Bluetooth command was sent."); }
            catch (Exception e) { Fail(e.Message); }
            Notify(); return;
        }
        IsBusy = true; Notify();
        try
        {
            byte[] command;
            try { command = (await ReadSettings()).PatchedSchedule(kind, mode, from, till); }
            catch (ProtocolException) { Fail($"This clock's {name} schedule could not be read, so it was not changed."); return; }
            await Write(command);
            var updated = await ReadSettings();
            Settings = updated;
            if (updated.MatchesSchedule(kind, mode, from, till)) Log(what, "Confirmed by the clock.");
            else Fail($"{what}: the write was accepted, but the clock did not return the requested schedule.");
        }
        catch (Exception e) { Fail(e.Message); }
        finally { IsBusy = false; Notify(); }
        ReadHandState();   // the DND state bit changes immediately
    }
    /// Read-only refresh of the state flags (used by the seconds pixel to keep DND/calibration fresh).
    public async void ReadHandState()
    {
        if (DemoMode || link is null) return;
        try { if (await Op(l => l.ReadStateAsync()) is byte[] b) UpdateHandState(b); } catch { }
    }
    public bool CanReadFace => link?.CanReadScene ?? false;
    /// The clock's active face (5075ffac); null when unavailable or unreadable.
    public async Task<int?> ReadFace()
    {
        try { return await Op(l => l.ReadSceneAsync()) is byte[] b && b.Length == 1 ? b[0] : null; }
        catch { return null; }
    }

    /// Moves the clock's running time by `minutes` via Settings field 18 (see ClockTimeCorrection).
    /// `clockMinusNow`: the clock's reading relative to this PC once the change lands, when known
    /// (the owner typed the clock's reading); it re-learns the estimate baseline.
    public async void ShiftClockTime(int minutes, string? title = null, int? clockMinusNow = null)
    {
        if (!CanControl || minutes == 0) return;
        if (TimeCorrectionSupported != true)
        {
            Fail("Time correction needs clock firmware 1.6.7 or later" + (FirmwareVersion is string f ? $"; this clock reports {f}." : ", and this clock has not reported its firmware version."));
            return;
        }
        if (CalibrationBusy) { Fail("Finish hand calibration before changing the time; the hands must not be moving."); return; }
        var summary = title ?? $"Clock time moved {ClockTimeCorrection.Describe(minutes)}";
        if (DemoMode)
        {
            if (Settings is not null)
            {
                int target = Settings.TimeOffsetMinutes + minutes;
                Settings = new ClockSettings(Settings.PatchedTimeOffset(target)[4..]);
                if (clockMinusNow is int k) TimeBaseline = ClockTimeCorrection.Baseline(target, k);
            }
            Log("Demo · " + summary.ToLowerInvariant(), "No Bluetooth command was sent.");
            return;
        }
        IsBusy = true; Notify();
        try
        {
            var current = await ReadSettings();
            int target = current.TimeOffsetMinutes + minutes;
            await Write(current.PatchedTimeOffset(target));
            var updated = await ReadSettings();
            Settings = updated;
            if (updated.TimeOffsetMinutes == target)
            {
                if (clockMinusNow is int k) TimeBaseline = ClockTimeCorrection.Baseline(target, k);
                Log(summary, $"Stored time offset {current.TimeOffsetMinutes} → {target} minutes. Check the time shown on the clock; the hands move to match.");
            }
            else Fail("The clock did not return the new time offset. Read the clock face before trying again; no command is retried automatically.");
        }
        catch (Exception e) { Fail("The time change could not be confirmed: " + e.Message); }
        finally { IsBusy = false; Notify(); }
    }

    public void SendMessage(string text, int animation, int sound, int color)
    {
        try { var packet = GlanceCommands.Notice(text, animation, sound, color); AnnounceDisplayCommand(90); Send(packet, "Message sent", $"{text} · Bluetooth acknowledged; check the clock display."); }
        catch (ProtocolException e) { Fail(e.Message); }
    }
    public void StartTimer(int minutes)
    {
        try { var packet = GlanceCommands.Timer(minutes * 60); AnnounceDisplayCommand(0); Send(packet, "Timer sent", $"{minutes} minutes · the countdown here is an estimate.", () => TimerEnd = DateTime.Now.AddMinutes(minutes)); }
        catch (ProtocolException e) { Fail(e.Message); }
    }
    public void StopTimer() { AnnounceDisplayCommand(2); Send(GlanceCommands.StopTimer, "Timer stop sent", "", () => TimerEnd = null); }
    public void StartCalibration()
    {
        if (IsCalibrating) { Fail("Calibration is already moving the hands. Wait for the clock to stop."); return; }
        AnnounceDisplayCommand(0);
        Send(GlanceCommands.StartCalibration, "Calibration started", "Wait for the hands to finish moving. Confirm only when both hands point to 12.", () => NeedsCalibrationConfirmation = true);
    }
    public void ConfirmCalibration()
    {
        if (IsCalibrating) { Fail("Wait until the hands stop before confirming calibration."); return; }
        AnnounceDisplayCommand(10);
        Send(GlanceCommands.ConfirmCalibration, "Hand position confirmed", "", () => NeedsCalibrationConfirmation = false);
    }
    public void TestSound(int sound)
    {
        AnnounceDisplayCommand(90);
        try { Send(GlanceCommands.Notice("Sound test", 0, sound, 12), "Sound test sent", Settings?.Muted == true ? "Mute is on, so the clock may stay silent." : Glance.Sounds[sound]); }
        catch (ProtocolException e) { Fail(e.Message); }
    }
    public void StopAlarm() { AnnounceDisplayCommand(2); Send(GlanceCommands.AlarmStop, "Alarm stop sent"); }

    void UpdateHandState(byte[] data)
    {
        ClockHandState state;
        try { state = new ClockHandState(data); } catch { HandStatus = "Clock hand-state format not recognised"; Notify(); return; }
        HandState = state; HandStateUpdated = DateTime.Now;
        IsCalibrating = state.Homing;
        NeedsCalibrationConfirmation = state.AwaitingConfirmation;
        var next = state.MotorFailure ? "Clock reports a motor failure" : state.HomingFailure ? "Clock reports a homing failure"
            : state.Homing ? "Clock is finding the hand reference…" : state.AwaitingConfirmation ? "Clock is waiting for hand alignment confirmation" : "Clock reports no calibration in progress";
        if (HandStatus != next) Log("Hand calibration state", next, state.MotorFailure || state.HomingFailure);
        HandStatus = next;
        Notify();
    }

    /// Background send (chime, weather, alarms, seconds): never shows a dialog and never retries.
    /// It does not mark the app busy, so frequent seconds scenes never grey out the controls; the
    /// operation gate still runs every read and write one at a time.
    public async void SendQuietly(byte[] data, string title, bool logSuccess = true, Action<bool>? completion = null)
    {
        if (DemoMode) { if (logSuccess) Log("Demo · " + title.ToLowerInvariant(), "No Bluetooth command was sent."); completion?.Invoke(true); return; }
        if (!IsConnected || Settings is null) { completion?.Invoke(false); return; }
        bool ok;
        try { await Write(data); ok = true; if (logSuccess) Log(title); }
        catch (Exception e) { ok = false; Log(title + " failed", e.Message, error: true); }
        completion?.Invoke(ok);
    }

    async void Send(byte[] data, string title, string detail = "", Action? success = null)
    {
        if (!CanControl) return;
        if (DemoMode) { Log("Demo · " + title.ToLowerInvariant(), "No Bluetooth command was sent."); success?.Invoke(); Notify(); return; }
        IsBusy = true; Notify();
        try { await Write(data); Log(title, detail); success?.Invoke(); }
        catch (Exception e) { Fail(e.Message); }
        finally { IsBusy = false; Notify(); }
    }
}
