using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Shapes;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using GlanceProtocol;

namespace OpenGlance;

public enum TabId { Home, Messages, Alarms, Weather, Display, Clock }

public interface ITab { IEnumerable<Control> Cards { get; } void Refresh(); }

public sealed class AppModel
{
    public ClockController Clock { get; } = new();
    public AlarmStore Alarms { get; }
    public ChimeService Chime { get; }
    public WeatherService Weather { get; }
    public AppModel() { Alarms = new AlarmStore(Clock); Chime = new ChimeService(Clock); Weather = new WeatherService(Clock); }
}

public sealed class MainWindow : Window
{
    public static string Version { get; set; } = "dev";
    readonly AppModel model;
    readonly Dictionary<TabId, ITab> tabs = new();
    readonly Dictionary<TabId, Button> tabButtons = new();
    readonly CardGrid grid = new();
    readonly StackPanel below = new() { Spacing = 16 };
    readonly Border statePill, batteryPill, weatherPill;
    readonly TextBlock stateText, batteryText, weatherText;
    readonly Ellipse stateDot, batteryDot, weatherDot;
    public TabId Tab { get; private set; } = TabId.Home;
    bool refreshQueued;

    public static Bitmap Logo { get; } = new(AssetLoader.Open(new Uri("avares://OpenGlanceClock/Assets/Logo.png")));

    public MainWindow(AppModel model)
    {
        this.model = model;
        Title = "OpenGlance Clock"; Width = 1100; Height = 800; MinWidth = 820; MinHeight = 640;
        Icon = new WindowIcon(AssetLoader.Open(new Uri("avares://OpenGlanceClock/Assets/Logo.png")));
        this.Res(BackgroundProperty, "Bg");

        statePill = Ui.Pill(out stateText, out stateDot);
        batteryPill = Ui.Pill(out batteryText, out batteryDot);
        weatherPill = Ui.Pill(out weatherText, out weatherDot);
        var logo = new Border { Width = 34, Height = 34, CornerRadius = new CornerRadius(8), ClipToBounds = true, Child = new Image { Source = Logo } };
        var title = Ui.Text("OpenGlance", 19, FontWeight.SemiBold); title.VerticalAlignment = VerticalAlignment.Center;
        var pills = new StackPanel { Orientation = Orientation.Horizontal, Children = { statePill, batteryPill, weatherPill }, VerticalAlignment = VerticalAlignment.Center };
        var header = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,Auto,*,Auto"), Margin = new Thickness(18, 12) };
        title.Margin = new Thickness(12, 0, 0, 0);
        Grid.SetColumn(title, 1); Grid.SetColumn(pills, 3);
        header.Children.Add(logo); header.Children.Add(title); header.Children.Add(pills);

        var tabRow = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        foreach (var id in Enum.GetValues<TabId>())
        {
            var b = new Button { Content = id.ToString(), FontSize = 14, FontWeight = FontWeight.SemiBold, Padding = new Thickness(14, 7), CornerRadius = new CornerRadius(20),
                BorderThickness = new Thickness(0), Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand) };
            b.Template = new Avalonia.Controls.Templates.FuncControlTemplate<Button>((x, _) => new Border
            {
                CornerRadius = new CornerRadius(20), Padding = new Thickness(14, 7),
                [!Border.BackgroundProperty] = x[!TemplatedControl.BackgroundProperty],
                Child = new Avalonia.Controls.Presenters.ContentPresenter { [!Avalonia.Controls.Presenters.ContentPresenter.ContentProperty] = x[!ContentControl.ContentProperty] },
            });
            b.Click += (_, _) => Show(id);
            tabButtons[id] = b; tabRow.Children.Add(b);
        }
        var version = Ui.Note("v" + Version); version.VerticalAlignment = VerticalAlignment.Center;
        var tabBar = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), Margin = new Thickness(14, 8) };
        Grid.SetColumn(version, 1); tabBar.Children.Add(tabRow); tabBar.Children.Add(version);

        Border Bar(Control c) => new Border { Child = c, BorderThickness = new Thickness(0, 0, 0, 1) }.Res(Border.BackgroundProperty, "Card").Res(Border.BorderBrushProperty, "Line");
        var content = new StackPanel { Spacing = 16, Margin = new Thickness(18), Children = { grid, below } };
        var scroll = new ScrollViewer { Content = content, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
        var root = new DockPanel();
        var top = new StackPanel { Children = { Bar(header), Bar(tabBar) } };
        DockPanel.SetDock(top, Dock.Top);
        root.Children.Add(top); root.Children.Add(scroll);
        Content = root;

        var clock = model.Clock;
        tabs[TabId.Home] = new HomeTab(this, model);
        tabs[TabId.Messages] = new MessagesTab(model);
        tabs[TabId.Alarms] = new AlarmsTab(this, model);
        tabs[TabId.Weather] = new WeatherTab(model);
        tabs[TabId.Display] = new DisplayTab(model);
        tabs[TabId.Clock] = new ClockTab(this, model);

        clock.Changed += QueueRefresh;
        model.Alarms.Changed += QueueRefresh;
        model.Weather.Changed += QueueRefresh;
        clock.ShowError = message => Dialogs.Alert(this, message);
        clock.AskPin = () => Dialogs.Ask(this, "Enter the clock's PIN", "Type the PIN shown on the clock's display.", "PIN");
        var tick = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        tick.Tick += (_, _) => Refresh();
        tick.Start();
        Closing += (_, _) => clock.Disconnect();
        Show(TabId.Home);
    }

    public void Show(TabId id)
    {
        Tab = id;
        grid.Children.Clear(); below.Children.Clear();
        foreach (var card in tabs[id].Cards) grid.Children.Add(card);
        if (id == TabId.Home) below.Children.Add(activityCard = Ui.Card("Recent activity"));
        if (id == TabId.Clock) below.Children.Add(activityCard = Ui.Card("Activity"));
        if (id is not (TabId.Home or TabId.Clock)) activityCard = null;
        lastLogTop = null;
        Refresh();
    }
    Border? activityCard;
    ActivityItem? lastLogTop;
    int lastLogCount;

    void QueueRefresh()
    {
        if (refreshQueued) return;
        refreshQueued = true;
        Dispatcher.UIThread.Post(() => { refreshQueued = false; Refresh(); });
    }

    void Refresh()
    {
        var clock = model.Clock;
        foreach (var (id, b) in tabButtons)
        {
            b.Res(TemplatedControl.BackgroundProperty, id == Tab ? "Teal" : "Card");
            b.Res(TemplatedControl.ForegroundProperty, id == Tab ? "White" : "Muted");
        }
        var (label, kind) = clock.DemoMode ? ("Demo", PillKind.Mid)
            : clock.IsConnected && clock.Settings is not null ? ("Connected", PillKind.Good)
            : clock.IsConnected || clock.IsConnecting ? ("Connecting…", PillKind.Mid)
            : clock.IsScanning ? ("Searching…", PillKind.Mid)
            : (clock.HasRememberedClock ? "Not connected" : "No clock paired", PillKind.Bad);
        Ui.SetPill(statePill, stateText, stateDot, "Clock: " + label, kind);
        batteryPill.IsVisible = clock.Battery is not null;
        if (clock.Battery is int b2) Ui.SetPill(batteryPill, batteryText, batteryDot, $"Battery {b2}%", b2 > 20 ? PillKind.Good : PillKind.Mid);
        var w = model.Weather;
        weatherPill.IsVisible = w.Options.On;
        Ui.SetPill(weatherPill, weatherText, weatherDot, w.Showing == 0 ? "Weather off" : w.RainDue ? "Rain coming" : "Weather on ring", PillKind.Plain);
        tabs[Tab].Refresh();
        if (activityCard is not null)
        {
            var top = clock.Events.FirstOrDefault();
            if (!ReferenceEquals(top, lastLogTop) || clock.Events.Count != lastLogCount)
            {
                lastLogTop = top; lastLogCount = clock.Events.Count;
                var body = activityCard.Body();
                while (body.Children.Count > 1) body.Children.RemoveAt(1);
                body.Children.Add(Ui.LogList(Tab == TabId.Home ? clock.Events.Take(6) : clock.Events));
            }
        }
    }

    /// Renders the window to a PNG (screenshot mode; demo data only, never Bluetooth).
    public void SaveScreenshot(string path)
    {
        var size = new PixelSize((int)Bounds.Width, (int)Bounds.Height);
        using var bitmap = new RenderTargetBitmap(size, new Vector(96, 96));
        bitmap.Render(this);
        bitmap.Save(path);
    }
}

// MARK: Home

sealed class HomeTab : ITab
{
    readonly AppModel m;
    readonly Border pair, clockCard, weatherCard;
    readonly TextBlock big, status, weatherBig, weatherNote;
    readonly TextBlock[] kv;
    readonly Button connect, disconnect, refresh;
    public IEnumerable<Control> Cards => new Control[] { pair, clockCard, weatherCard };

    public HomeTab(MainWindow window, AppModel m)
    {
        this.m = m;
        var c = m.Clock;
        pair = Ui.Card("Pair your clock", Ui.Note("No clock has been connected from this PC yet."), Ui.Row(Ui.Btn("Pair a clock", Kind.Primary, () => window.Show(TabId.Clock))));
        big = Ui.Big(); status = Ui.Note();
        var (g, values) = Ui.KV("Clock", "Battery", "Hands"); kv = values;
        connect = Ui.Btn("Connect", Kind.Secondary, () => c.ConnectSaved());
        disconnect = Ui.Btn("Disconnect", Kind.Secondary, () => c.Disconnect());
        refresh = Ui.Btn("Refresh", Kind.Secondary, () => c.RefreshSettings());
        clockCard = Ui.Card("Clock", big, status, g, Ui.Row(connect, disconnect, refresh), Ui.Note("Set the clock's time on the Clock tab."));
        weatherBig = Ui.Big(); weatherNote = Ui.Note();
        weatherCard = Ui.Card("Weather", weatherBig, weatherNote, Ui.Link("Weather settings →", () => window.Show(TabId.Weather)));
    }

    public void Refresh()
    {
        var c = m.Clock; var w = m.Weather;
        pair.IsVisible = !c.HasRememberedClock && !c.DemoMode;
        big.Text = c.DemoMode ? "Demo" : c.IsConnected && c.Settings is not null ? "Connected" : c.IsConnecting ? "Connecting…" : "Not connected";
        status.Text = c.Status;
        kv[0].Text = c.IsConnected || c.DemoMode ? c.ClockName : c.RememberedName ?? "—";
        kv[1].Text = c.Battery is int b ? $"{b}%" : "—";
        kv[2].Text = c.HandStatus;
        connect.IsEnabled = !c.IsConnected && !c.IsConnecting && c.HasRememberedClock && c.BluetoothAvailable;
        disconnect.IsEnabled = c.IsConnected;
        refresh.IsEnabled = c.CanControl && !c.DemoMode;
        weatherBig.Text = w.Options.On && w.Forecast?.Current is double t ? $"{Math.Round(t, MidpointRounding.AwayFromZero)}°{(w.Options.Fahrenheit ? "F" : "C")}" : "—";
        weatherNote.Text = w.Options.On ? $"{w.Place?.Name ?? "No location"} · ring: {(w.Showing == 0 ? "nothing" : w.RainDue ? "rain coming" : "temperature")}" : "Weather ring is off";
    }
}

// MARK: Messages

sealed class MessagesTab : ITab
{
    readonly AppModel m;
    readonly Border message, timer, chime;
    readonly TextBlock countdown;
    readonly List<Control> needsClock = new();
    readonly CheckBox chimeBox;
    public IEnumerable<Control> Cards => new Control[] { message, timer, chime };

    public MessagesTab(AppModel m)
    {
        this.m = m;
        var c = m.Clock;
        var text = new TextBox { Text = "Hello world", MaxLength = 64 };
        int animation = 1, colour = 18, sound = 0;
        var anim = Ui.Combo(Glance.AnimationNames.Select(a => (a.Item1, a.Item2)), 1, v => animation = v);
        var col = Ui.Combo(Glance.ColourNames.Select(a => (a.Item1, a.Item2)), 18, v => colour = v);
        var picker = Ui.SoundPicker(0, true, v => sound = v, c, () => sound, out _);
        var send = Ui.Btn("Send to clock", Kind.Primary, () => c.SendMessage(text.Text ?? "", animation, sound, colour));
        needsClock.Add(send); needsClock.Add(((StackPanel)picker).Children[1]);
        message = Ui.Card("Message", Ui.Note("Text (up to 64 plain characters)"), text,
            Ui.Wrap(Ui.Note("Animation"), anim, Ui.Note("Colour"), col), Ui.Note("Sound"), picker, Ui.Row(send),
            Ui.Note("If no sound plays, check that Mute is off on the Display tab."));

        countdown = Ui.Big();
        var minutes = new NumericUpDown { Minimum = 1, Maximum = 720, Value = 10, Increment = 1, FormatString = "0", Width = 130 };
        var start = Ui.Btn("Start", Kind.Primary, () => c.StartTimer((int)(minutes.Value ?? 10)));
        var stop = Ui.Btn("Stop", Kind.Secondary, () => c.StopTimer());
        needsClock.Add(start); needsClock.Add(stop);
        var presets = new List<Control>();
        foreach (var p in new[] { 5, 15, 30, 60 })
        {
            var b = Ui.Btn(p == 60 ? "1 hour" : $"{p} min", Kind.Secondary, () => { minutes.Value = p; c.StartTimer(p); });
            needsClock.Add(b); presets.Add(b);
        }
        timer = Ui.Card("Timer", countdown, Ui.Note("The countdown here is an estimate from when the timer was sent."),
            Ui.Row(minutes, Ui.Note("min"), start, stop), Ui.Wrap(presets.ToArray()));

        var ch = m.Chime;
        var (chimeRow, box) = Ui.Switch("Chime every hour", "Shows the time with a sound, while this app is open", v => ch.On = v);
        chimeBox = box;
        var chimeSound = Ui.SoundPicker(ch.Sound, false, v => ch.Sound = v, c, () => ch.Sound, out _);
        needsClock.Add(((StackPanel)chimeSound).Children[1]);
        var hours = Enumerable.Range(0, 24).Select(h => (h, Glance.Hour(h)));
        chime = Ui.Card("Hourly chime", chimeRow, Ui.Note("Sound"), chimeSound, Ui.Note("Quiet hours (no chime)"),
            Ui.Wrap(Ui.Note("From"), Ui.Combo(hours, ch.QuietFrom, v => ch.QuietFrom = v), Ui.Note("To"), Ui.Combo(hours, ch.QuietTo, v => ch.QuietTo = v)),
            Ui.Note("Set both to the same hour to chime around the clock. The clock also mutes sounds in its own night mode."));
    }

    public void Refresh()
    {
        var c = m.Clock;
        foreach (var x in needsClock) x.IsEnabled = c.CanControl;
        var left = c.TimerEnd is DateTime end ? (int)Math.Max(0, (end - DateTime.Now).TotalSeconds) : 0;
        countdown.Text = left > 0 ? $"{left / 60}:{left % 60:00}" : "—";
        chimeBox.SetQuietly(m.Chime.On);
    }
}

// MARK: Alarms

sealed class AlarmsTab : ITab
{
    readonly MainWindow window;
    readonly AppModel m;
    readonly Border list, editorCard, ringing;
    readonly StackPanel rows = new() { Spacing = 0 };
    readonly TextBlock pendingNote = Ui.Note();
    readonly Button add, stop;
    AlarmEntry? editing;
    bool editingIsNew;
    readonly TextBox time = new() { Width = 110, Watermark = "07:30" };
    readonly TextBox label = new() { Watermark = "Wake up", MaxLength = 16 };
    readonly CheckBox[] days = new CheckBox[7];
    readonly ComboBox sound;
    readonly TextBlock editorTitle, timeProblem = Ui.Note();
    string shownList = "";
    public IEnumerable<Control> Cards => editing is null ? new Control[] { list, ringing } : new Control[] { list, editorCard, ringing };

    public AlarmsTab(MainWindow window, AppModel m)
    {
        this.window = window; this.m = m;
        var c = m.Clock;
        add = Ui.Btn("Add alarm", Kind.Primary, () => Edit(new AlarmEntry(), true));
        list = Ui.Card("Alarms", rows, Ui.Row(add), pendingNote);

        for (int i = 0; i < 7; i++) days[i] = new CheckBox { Content = Glance.Days[i], MinWidth = 0, Margin = new Thickness(0, 0, 6, 0) };
        var dayRow = new WrapPanel(); foreach (var d in days) dayRow.Children.Add(d);
        void Preset(int bits) { for (int i = 0; i < 7; i++) days[i].IsChecked = (bits & (1 << i)) != 0; }
        var presets = Ui.Wrap(Ui.Btn("Weekdays", Kind.Secondary, () => Preset(31)), Ui.Btn("Weekend", Kind.Secondary, () => Preset(96)),
            Ui.Btn("Every day", Kind.Secondary, () => Preset(127)), Ui.Btn("Once", Kind.Secondary, () => Preset(0)));
        int chosenSound = 6;
        var soundPicker = Ui.SoundPicker(6, false, v => chosenSound = v, c, () => chosenSound, out sound);
        label.TextChanged += (_, _) =>
        {
            var clean = new string((label.Text ?? "").Where(ch => ch >= 32 && ch <= 126).Take(16).ToArray());
            if (clean != label.Text) label.Text = clean;
        };
        editorTitle = Ui.Text("New alarm", 15, FontWeight.SemiBold);
        var save = Ui.Btn("Save alarm", Kind.Primary, () =>
        {
            if (editing is null) return;
            if (ClockTimeCorrection.ParseTime(time.Text ?? "") is not (int h, int mi)) { timeProblem.Text = "Type the time like 7:30 am or 07:30."; return; }
            var a = editing.Copy();
            a.Hour = h; a.Minute = mi; a.Sound = chosenSound; a.Text = label.Text ?? "";
            a.Days = Enumerable.Range(0, 7).Where(i => days[i].IsChecked == true).Sum(i => 1 << i);
            if (!a.IsValid) { timeProblem.Text = "Check the alarm's time and label."; return; }
            m.Alarms.Save(a); Close();
        });
        var cancel = Ui.Btn("Cancel", Kind.Secondary, Close);
        editorCard = Ui.Card("", Ui.Row(Ui.Note("Time"), time), timeProblem, Ui.Note("Days"), dayRow, presets, Ui.Note("Sound"), soundPicker,
            Ui.Note("Label on the display (up to 16 characters)"), label, Ui.Row(save, cancel),
            Ui.Note("“Once” sends the alarm with no repeat days, as the original app did for one-off alarms."));
        editorCard.Body().Children[0] = editorTitle;
        // Sound choice must follow the alarm being edited.
        editorSound = v => { chosenSound = v; sound.SelectQuietly(Enumerable.Range(1, 17).Select(i => (i, Glance.Sounds[i])), v); };

        stop = Ui.Btn("Stop the alarm now", Kind.Primary, () => c.StopAlarm());
        ringing = Ui.Card("Alarm ringing?", Ui.Row(stop),
            Ui.Note("The clock can't report which alarms it holds. Saving an alarm here replaces every alarm on the clock, including old ones set from the phone app."),
            Ui.Row(Ui.Btn("Delete all alarms on the clock", Kind.Danger, async () =>
            {
                if (await Dialogs.Confirm(window, "Delete every alarm?", "This deletes every alarm on the clock and on this PC.", "Delete all alarms", danger: true)) m.Alarms.DeleteAll();
            })));
    }
    readonly Action<int> editorSound;

    void Edit(AlarmEntry alarm, bool isNew)
    {
        editing = alarm.Copy(); editingIsNew = isNew;
        editorTitle.Text = isNew ? "New alarm" : "Edit alarm";
        time.Text = $"{alarm.Hour:00}:{alarm.Minute:00}";
        label.Text = alarm.Text; timeProblem.Text = "";
        for (int i = 0; i < 7; i++) days[i].IsChecked = (alarm.Days & (1 << i)) != 0;
        editorSound(alarm.Sound);
        window.Show(TabId.Alarms);
    }
    void Close() { editing = null; window.Show(TabId.Alarms); }

    public void Refresh()
    {
        var c = m.Clock;
        stop.IsEnabled = c.CanControl;
        add.IsEnabled = m.Alarms.Alarms.Count < 4;
        pendingNote.Text = m.Alarms.Pending ? "Changes are waiting to be sent to the clock (they are sent when it connects)." : "The clock holds up to 4 alarms.";
        var signature = string.Join("|", m.Alarms.Alarms.Select(a => $"{a.Id}{a.Hour}{a.Minute}{a.Days}{a.Sound}{a.Text}{a.Enabled}"));
        if (signature == shownList) return;
        shownList = signature;
        rows.Children.Clear();
        if (m.Alarms.Alarms.Count == 0) rows.Children.Add(Ui.Note("No alarms saved on this PC."));
        foreach (var a in m.Alarms.Alarms.ToList())
        {
            var alarm = a;
            var on = new CheckBox { IsChecked = a.Enabled, VerticalAlignment = VerticalAlignment.Center };
            on.IsCheckedChanged += (_, _) => m.Alarms.SetEnabled(alarm, on.IsChecked == true);
            var t = Ui.Text($"{a.Hour:00}:{a.Minute:00}", 20, FontWeight.SemiBold); t.VerticalAlignment = VerticalAlignment.Center;
            var d = Ui.Note($"{Glance.DaysText(a.Days)} · {Glance.Sounds[a.Sound]}{(a.Text.Length == 0 ? "" : $" · “{a.Text}”")}"); d.VerticalAlignment = VerticalAlignment.Center;
            var g = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"), Margin = new Thickness(0, 4) };
            var right = Ui.Row(on, Ui.Btn("Edit", Kind.Secondary, () => Edit(alarm, false)), Ui.Btn("Delete", Kind.Danger, () => m.Alarms.Delete(alarm)));
            d.Margin = new Thickness(10, 0); Grid.SetColumn(d, 1); Grid.SetColumn(right, 2);
            g.Children.Add(t); g.Children.Add(d); g.Children.Add(right);
            rows.Children.Add(g);
            rows.Children.Add(new Border { Height = 1 }.Res(Border.BackgroundProperty, "Line"));
        }
    }
}

// MARK: Weather

sealed class WeatherTab : ITab
{
    readonly AppModel m;
    readonly Border options, preview, location, colours;
    readonly CheckBox on, temperature, text;
    readonly ComboBox rainMode, cycle, rainHours, units;
    readonly Control cycleRow, rainRules;
    readonly Slider mm, chance;
    readonly TextBlock mmNote, chanceNote, status, current, previewBody;
    readonly StackPanel previewBars = new() { Spacing = 6 };
    readonly StackPanel results = new() { Spacing = 4 };
    readonly Button update;
    string previewKey = "";
    static readonly (int, string)[] RainModes = { (1, "Alternate rain and temperature"), (2, "Show rain instead of temperature"), (0, "Don't show rain") };
    static readonly (int, string)[] Cycles = { (10, "10 seconds"), (20, "20 seconds"), (30, "30 seconds"), (60, "1 minute"), (120, "2 minutes"), (300, "5 minutes"), (600, "10 minutes") };
    static readonly (int, string)[] Hours = { (2, "2 hours"), (3, "3 hours"), (6, "6 hours"), (12, "12 hours") };
    static readonly (bool, string)[] Units = { (false, "Celsius"), (true, "Fahrenheit") };
    public IEnumerable<Control> Cards => new Control[] { options, preview, location, colours };

    public WeatherTab(AppModel m)
    {
        this.m = m;
        var w = m.Weather;
        var (onRow, onBox) = Ui.Switch("Show weather on the ring", null, v => w.Update(o => o with { On = v })); on = onBox;
        var (tRow, tBox) = Ui.Switch("Temperature ring (stays up all the time)", null, v => w.Update(o => o with { Temperature = v })); temperature = tBox;
        var (xRow, xBox) = Ui.Switch("Also show the temperature on the display", "Off keeps the time and date on the display", v => w.Update(o => o with { Text = v })); text = xBox;
        rainMode = Ui.Combo(RainModes, w.Options.RainMode, v => w.Update(o => o with { RainMode = v }));
        cycle = Ui.Combo(Cycles, w.Options.Cycle, v => w.Update(o => o with { Cycle = v }));
        cycleRow = Ui.Wrap(Ui.Note("Switch rings every"), cycle);
        mm = new Slider { Minimum = 0.1, Maximum = 5, Value = w.Options.RainMm };
        chance = new Slider { Minimum = 10, Maximum = 90, Value = w.Options.RainChance };
        mmNote = Ui.Note(); chanceNote = Ui.Note();
        Debounce(mm, v => w.Update(o => o with { RainMm = Math.Round(v * 10, MidpointRounding.AwayFromZero) / 10 }), v => mmNote.Text = $"Rain is coming when an hour has at least {Math.Round(v * 10, MidpointRounding.AwayFromZero) / 10:0.0} mm");
        Debounce(chance, v => w.Update(o => o with { RainChance = (int)(Math.Round(v / 5, MidpointRounding.AwayFromZero) * 5) }), v => chanceNote.Text = $"… and at least {(int)(Math.Round(v / 5, MidpointRounding.AwayFromZero) * 5)}% chance");
        rainHours = Ui.Combo(Hours, w.Options.RainHours, v => w.Update(o => o with { RainHours = v }));
        var rules = new StackPanel { Spacing = 6, Children = { mmNote, mm, chanceNote, chance, Ui.Wrap(Ui.Note("… within the next"), rainHours) } };
        rainRules = rules;
        update = Ui.Btn("Update forecast now", Kind.Secondary, () => _ = w.Fetch(user: true));
        status = Ui.Note();
        options = Ui.Card("Weather on the clock", onRow, tRow, xRow, Ui.Wrap(Ui.Note("When rain is coming"), rainMode), cycleRow, rainRules, Ui.Row(update), status,
            Ui.Note("Works while this app is open. The clock hides the rings by itself in night mode."));

        previewBody = Ui.Note();
        preview = Ui.Card("Next 12 hours", previewBody, previewBars);

        current = Ui.Note();
        var query = new TextBox { Watermark = "Town or suburb", MinWidth = 220 };
        Button? search = null;
        async void Search()
        {
            var q = (query.Text ?? "").Trim();
            if (q.Length < 2) return;
            search!.IsEnabled = false;
            List<WeatherPlace> found;
            try { found = await WeatherService.Search(q); } catch { found = new(); }
            search.IsEnabled = true;
            results.Children.Clear();
            if (found.Count == 0) results.Children.Add(Ui.Note("No places found."));
            foreach (var p in found)
            {
                var place = p;
                var name = Ui.Text(p.Name); name.VerticalAlignment = VerticalAlignment.Center;
                var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
                var use = Ui.Btn("Use", Kind.Secondary, () => { w.SetPlace(place); results.Children.Clear(); query.Text = ""; });
                Grid.SetColumn(use, 1); g.Children.Add(name); g.Children.Add(use);
                results.Children.Add(g);
            }
        }
        search = Ui.Btn("Search", Kind.Secondary, Search);
        query.KeyDown += (_, e) => { if (e.Key == Avalonia.Input.Key.Enter) Search(); };
        location = Ui.Card("Location", current, Ui.Row(query, search), results, Ui.Note("Weather data by Open-Meteo.com (CC BY 4.0)."));

        units = Ui.Combo(Units, w.Options.Fahrenheit, v => w.Update(o => o with { Fahrenheit = v }));
        var gradient = new Border
        {
            Height = 12, CornerRadius = new CornerRadius(6),
            Background = new LinearGradientBrush
            {
                StartPoint = new RelativePoint(0, 0.5, RelativeUnit.Relative), EndPoint = new RelativePoint(1, 0.5, RelativeUnit.Relative),
                GradientStops = { new GradientStop(Color.Parse("#2060FF"), 0), new GradientStop(Color.Parse("#00C070"), 0.33), new GradientStop(Color.Parse("#FFC000"), 0.66), new GradientStop(Color.Parse("#FF3000"), 1) },
            },
        };
        var cold = new NumericUpDown { Minimum = -40, Maximum = 129, Value = w.Options.ColdAt, Increment = 1, FormatString = "0", Width = 130 };
        var hot = new NumericUpDown { Minimum = -39, Maximum = 130, Value = w.Options.HotAt, Increment = 1, FormatString = "0", Width = 130 };
        var saveRange = Ui.Btn("Save range", Kind.Secondary, () =>
        {
            int lo = (int)(cold.Value ?? 5), hi = (int)(hot.Value ?? 35);
            if (hi <= lo) hi = lo + 1;
            w.Update(o => o with { ColdAt = lo, HotAt = hi });
        });
        colours = Ui.Card("Temperature colours", Ui.Wrap(Ui.Note("Units"), units), gradient, Ui.Wrap(Ui.Note("Blue at"), cold, Ui.Note("Red at"), hot), Ui.Row(saveRange),
            Ui.Note("Temperatures are coloured blue → green → amber → red across this range. The clock blends between the colours of the day's lowest and highest temperature."));
    }

    static void Debounce(Slider s, Action<double> commit, Action<double> live)
    {
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(700) };
        timer.Tick += (_, _) => { timer.Stop(); commit(s.Value); };
        live(s.Value);
        s.PropertyChanged += (_, e) =>
        {
            if (e.Property != RangeBase.ValueProperty) return;
            live(s.Value);
            if (s.Tag is null) { timer.Stop(); timer.Start(); }
        };
    }

    public void Refresh()
    {
        var w = m.Weather; var o = w.Options;
        on.SetQuietly(o.On); temperature.SetQuietly(o.Temperature); text.SetQuietly(o.Text);
        rainMode.SelectQuietly(RainModes, o.RainMode); cycle.SelectQuietly(Cycles, o.Cycle); rainHours.SelectQuietly(Hours, o.RainHours); units.SelectQuietly(Units, o.Fahrenheit);
        cycleRow.IsVisible = o.RainMode == 1 && o.Temperature;
        rainRules.IsVisible = o.RainMode != 0;
        update.IsEnabled = o.On && w.Place is not null;
        status.Text = !o.On ? "Weather ring is off."
            : w.Error ?? (w.Fetched is DateTime f ? $"Forecast updated {f:HH:mm}" + (w.Sent is DateTime s ? $" · sent to clock {s:HH:mm}" : "")
            : w.Place is null ? "Choose a location below." : "Waiting for the first forecast…");
        current.Text = "Current: " + (w.Place?.Name ?? "not set");
        RefreshPreview();
    }

    void RefreshPreview()
    {
        var w = m.Weather; var o = w.Options;
        var hours = w.PreviewHours();
        var key = string.Join(",", hours.Select(h => $"{h.Label}{h.Temp}{h.Rain}")) + o.ToString() + w.Place?.Name + w.Forecast?.Current;
        if (key == previewKey) return;
        previewKey = key;
        previewBody.Text = (w.Place?.Name ?? "No location") + (w.Forecast?.Current is double c ? $" · now {Math.Round(c, MidpointRounding.AwayFromZero)}°" : "");
        previewBars.Children.Clear();
        if (hours.Count == 0) { previewBars.Children.Add(Ui.Note("No forecast yet.")); return; }
        double lo = hours.Min(h => h.Temp) - 1, hi = hours.Max(h => h.Temp);
        previewBars.Children.Add(Bars(hours.Select(h => (h.Label, (h.Temp - lo) / Math.Max(1, hi - lo),
            (Color?)Color.FromUInt32(0xFF000000 | WeatherScenes.Palette((h.Temp - o.ColdAt) / Math.Max(1, o.HotAt - o.ColdAt))), $"{Math.Round(h.Temp, MidpointRounding.AwayFromZero)}°")).ToList(), 90));
        bool any = hours.Take(o.RainHours).Any(h => h.Rain > 0);
        previewBars.Children.Add(Ui.Note(o.RainMode == 0 ? "Rain ring is off"
            : any ? $"Rain coming ({o.RainMm:0.0} mm and {o.RainChance}% within {o.RainHours} h) — rain ring in use"
            : $"No rain coming ({o.RainMm:0.0} mm and {o.RainChance}% within {o.RainHours} h) — temperature only"));
        previewBars.Children.Add(Bars(hours.Select(h => (h.Label, h.Rain / 100.0,
            h.Rain > 0 ? Color.FromUInt32(0xFF000000 | WeatherScenes.Mix(0x7FD4FF, 0x0030FF, h.Rain / 100.0)) : (Color?)null, h.Rain > 0 ? $"{h.Rain}%" : "")).ToList(), 60));
    }

    static Control Bars(List<(string Label, double Value, Color? Colour, string Top)> items, double height)
    {
        var g = new Grid { Height = height + 34 };
        for (int i = 0; i < items.Count; i++)
        {
            g.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            var (label, value, colour, top) = items[i];
            var bar = new Border { CornerRadius = new CornerRadius(4), Height = Math.Max(3, height * (0.1 + 0.8 * Math.Clamp(value, 0, 1))), Margin = new Thickness(2, 0) };
            if (colour is Color col) bar.Background = new SolidColorBrush(col); else bar.Res(Border.BackgroundProperty, "Line");
            var t = Ui.Note(top); t.FontSize = 10; t.TextWrapping = TextWrapping.NoWrap; t.HorizontalAlignment = HorizontalAlignment.Center;
            var l = Ui.Note(label); l.FontSize = 10; l.HorizontalAlignment = HorizontalAlignment.Center;
            var stack = new StackPanel { Spacing = 2, VerticalAlignment = VerticalAlignment.Bottom, Children = { t, bar, l } };
            Grid.SetColumn(stack, i); g.Children.Add(stack);
        }
        return g;
    }
}

// MARK: Display

sealed class DisplayTab : ITab
{
    readonly AppModel m;
    readonly Border display, brightness;
    readonly (int Field, CheckBox Box)[] toggles;
    readonly ComboBox dateFormat;
    readonly CheckBox automatic;
    readonly Slider low, high, manual;
    readonly TextBlock lowNote, highNote, manualNote;
    readonly Control autoPanel, manualPanel;
    readonly List<Control> needsClock = new();
    public IEnumerable<Control> Cards => new Control[] { display, brightness };

    public DisplayTab(AppModel m)
    {
        this.m = m;
        var c = m.Clock;
        var list = new List<(int, CheckBox)>();
        Control Setting(string title, int field)
        {
            var (row, box) = Ui.Switch(title, null, v => c.ChangeSettings(new() { [field] = v ? 1UL : 0UL }));
            list.Add((field, box)); needsClock.Add(row);
            return row;
        }
        var rows = new[] { Setting("Digital time on the display", 11), Setting("12-hour time", 12), Setting("Hour points always on", 9), Setting("Automatic night mode", 2), Setting("Mute sounds", 4) };
        toggles = list.ToArray();
        dateFormat = Ui.Combo(Glance.DateFormats.Select(d => (d.Item1, d.Item2)), 0, v => c.ChangeSettings(new() { [5] = (ulong)v }));
        needsClock.Add(dateFormat);
        display = Ui.Card("Display", rows.Append(Ui.Wrap(Ui.Note("Date on the display"), dateFormat)).ToArray());

        lowNote = Ui.Note(); highNote = Ui.Note(); manualNote = Ui.Note();
        low = new Slider { Minimum = 0, Maximum = 255, Value = 30 };
        high = new Slider { Minimum = 0, Maximum = 255, Value = 220 };
        manual = new Slider { Minimum = 1, Maximum = 255, Value = 128 };
        void Live() { lowNote.Text = $"Minimum (darkest) {(int)low.Value}"; highNote.Text = $"Maximum (brightest) {(int)high.Value}"; manualNote.Text = $"Fixed brightness {(int)manual.Value}"; }
        Commit(low, () => { if (high.Value < low.Value) Quiet(high, low.Value); c.SetAutomaticBrightness((int)low.Value, (int)high.Value); }, Live);
        Commit(high, () => { if (low.Value > high.Value) Quiet(low, high.Value); c.SetAutomaticBrightness((int)low.Value, (int)high.Value); }, Live);
        Commit(manual, () => c.ChangeSettings(new() { [10] = (ulong)manual.Value }), Live);
        Live();
        var (autoRow, autoBox) = Ui.Switch("Automatic (light sensor)", null, v =>
        {
            if (v) c.SetAutomaticBrightness((int)low.Value, (int)high.Value); else c.ChangeSettings(new() { [10] = (ulong)manual.Value });
        });
        automatic = autoBox; needsClock.Add(autoRow);
        var restore = Ui.Btn("Restore 30–220 defaults", Kind.Secondary, () => { Quiet(low, 30); Quiet(high, 220); Live(); c.SetAutomaticBrightness(30, 220); });
        autoPanel = new StackPanel { Spacing = 6, Children = { lowNote, low, highNote, high, Ui.Row(restore),
            Ui.Note("Limits for the sensor-driven range, as in the original app. Changes are sent when you stop moving a slider.") } };
        manualPanel = new StackPanel { Spacing = 6, Children = { manualNote, manual } };
        needsClock.Add(autoPanel); needsClock.Add(manualPanel);
        brightness = Ui.Card("Brightness", autoRow, autoPanel, manualPanel);
    }

    static void Quiet(Slider s, double v) { s.Tag = "quiet"; s.Value = v; s.Tag = null; }
    static void Commit(Slider s, Action commit, Action live)
    {
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(700) };
        timer.Tick += (_, _) => { timer.Stop(); commit(); };
        s.PropertyChanged += (_, e) =>
        {
            if (e.Property != RangeBase.ValueProperty) return;
            live();
            if (s.Tag is null) { timer.Stop(); timer.Start(); }
        };
    }

    int? syncedFrom;
    public void Refresh()
    {
        var c = m.Clock; var s = c.Settings;
        foreach (var x in needsClock) x.IsEnabled = c.CanControl;
        foreach (var (field, box) in toggles)
            box.SetQuietly(field switch { 11 => s?.DigitalTime, 12 => s?.TwelveHour, 9 => s?.HourPoints, 2 => s?.NightMode, _ => s?.Muted } ?? false);
        dateFormat.SelectQuietly(Glance.DateFormats.Select(d => (d.Item1, d.Item2)), s?.DateFormat ?? 0);
        bool auto = (s?.Brightness ?? 0) == 0;
        automatic.SetQuietly(auto);
        autoPanel.IsVisible = auto; manualPanel.IsVisible = !auto;
        if (s is not null && syncedFrom != s.GetHashCode())
        {
            syncedFrom = s.GetHashCode();
            if (s.Brightness > 0) Quiet(manual, s.Brightness);
            Quiet(low, s.AutomaticBrightnessLow); Quiet(high, s.AutomaticBrightnessHigh);
            lowNote.Text = $"Minimum (darkest) {(int)low.Value}"; highNote.Text = $"Maximum (brightest) {(int)high.Value}"; manualNote.Text = $"Fixed brightness {(int)manual.Value}";
        }
    }
}

// MARK: Clock (pairing, clock time, connection, calibration)

sealed class ClockTab : ITab
{
    readonly MainWindow window;
    readonly AppModel m;
    readonly Border pair, time, connection, calibration;
    readonly Button find, forget, demo, exitDemo;
    readonly StackPanel nearby = new() { Spacing = 0 };
    readonly TextBlock searching = Ui.Note();
    string nearbyKey = "";
    readonly CheckBox autoConnect;
    readonly TextBlock handState;
    readonly Button startCal, confirmCal;
    // Clock time
    readonly TextBlock[] kv;
    readonly Border firmwareWarning;
    readonly TextBlock firmwareWarningText = Ui.Note();
    readonly TextBox shownText = new() { Watermark = "e.g. 6:27 pm", Width = 130 };
    readonly TextBox targetText = new() { Watermark = "e.g. 7:55 pm", Width = 130 };
    readonly CheckBox shownDateWrong = new() { Content = "Its date is wrong" };
    readonly CheckBox setTargetDate = new() { Content = "Also set the date" };
    readonly CalendarDatePicker shownDate = new() { SelectedDate = DateTime.Today };
    readonly CalendarDatePicker targetDate = new() { SelectedDate = DateTime.Today };
    readonly ComboBox mode;
    readonly Control manualRow;
    readonly Button set, reset;
    readonly TextBlock plan = Ui.Note();
    readonly List<Control> timeControls = new();
    bool manual;
    public IEnumerable<Control> Cards => new Control[] { pair, time, connection, calibration };

    public ClockTab(MainWindow window, AppModel m)
    {
        this.window = window; this.m = m;
        var c = m.Clock;
        var steps = new StackPanel { Spacing = 4, Children = {
            Ui.Text("1. Quit the bridge or phone app if either is connected to the clock."),
            Ui.Text("2. Short-press the clock's button to activate pairing mode (do not hold it)."),
            Ui.Text("3. Press Find clocks, then Connect beside your clock."),
            Ui.Text("4. Type the PIN shown on the clock when this app asks for it.") } };
        find = Ui.Btn("Find clocks", Kind.Primary, () => c.Scan());
        forget = Ui.Btn("Forget clock", Kind.Danger, async () =>
        {
            if (await Dialogs.Confirm(window, "Forget the saved clock?", "Removes the clock remembered by this app. It never resets the clock or clears its pairings.", "Forget", danger: true)) c.ForgetClock();
        });
        var forgetGroup = new Expander { Header = "Forget the saved clock", Content = new StackPanel { Spacing = 8, Children = {
            Ui.Note("Removes the clock remembered by this app. It never resets the clock or clears its pairings."), Ui.Row(forget) } } };
        demo = Ui.Link("Explore demo (no Bluetooth)", () => c.SetDemo(true));
        exitDemo = Ui.Btn("Exit demo", Kind.Secondary, () => c.SetDemo(false));
        var noBluetooth = Ui.Callout(Ui.Note(c.BluetoothAvailable ? "" : "This build has no Bluetooth (it is a preview build for checking the screens). Use the Windows release."));
        noBluetooth.IsVisible = !c.BluetoothAvailable;
        pair = Ui.Card("Pair a clock", steps, Ui.Row(find, searching), nearby, forgetGroup, noBluetooth, Ui.Row(demo), Ui.Row(exitDemo));

        // Clock time (Settings field 18; see ClockTimeCorrection)
        var (g, values) = Ui.KV("This PC", "Clock firmware", "Clock should show", "Stored offset"); kv = values;
        firmwareWarning = Ui.Callout(firmwareWarningText);
        mode = Ui.Combo(new[] { (false, "This PC's time"), (true, "Manual time") }, false, v => { manual = v; Refresh(); });
        manualRow = Ui.Wrap(Ui.Note("Time"), targetText, setTargetDate, targetDate);
        set = Ui.Btn("Set", Kind.Primary, async () =>
        {
            var p = Plan(DateTime.Now);
            if (p.Delta is not int delta || delta == 0) return;
            if (!await Dialogs.Confirm(window, $"Move the clock {ClockTimeCorrection.Describe(delta)}?", "The hands move to the new time.", "Move the clock")) return;
            var again = Plan(DateTime.Now);   // the minute may have turned while the dialog was open
            if (again.Delta is int d2 && d2 != 0) { c.ShiftClockTime(d2, clockMinusNow: again.ClockMinusNow); shownText.Text = ""; }
        });
        var nudges = Ui.Wrap(Ui.Note("Fine adjust"),
            Ui.Btn("−1 hour", Kind.Secondary, () => c.ShiftClockTime(-60)), Ui.Btn("−1 min", Kind.Secondary, () => c.ShiftClockTime(-1)),
            Ui.Btn("+1 min", Kind.Secondary, () => c.ShiftClockTime(1)), Ui.Btn("+1 hour", Kind.Secondary, () => c.ShiftClockTime(60)));
        var correct = new Expander { Header = "Clock not showing the estimated time?", Content = new StackPanel { Spacing = 8, Children = {
            Ui.Note("Type what the clock shows right now. The next Set uses it, and the app remembers the correction for later estimates."),
            Ui.Wrap(Ui.Note("Clock shows"), shownText, shownDateWrong, shownDate) } } };
        reset = Ui.Btn("Reset offset to 0", Kind.Secondary, async () =>
        {
            int offset = c.Settings?.TimeOffsetMinutes ?? 0;
            if (offset == 0) return;
            if (await Dialogs.Confirm(window, "Reset the stored offset to 0?", $"This moves the clock {ClockTimeCorrection.Describe(-offset)} until the bridge sets the time again.", "Reset offset"))
                c.ShiftClockTime(-offset, "Time offset reset to 0");
        });
        var bridge = new Expander { Header = "Using the OpenGlance Bridge again?", Content = new StackPanel { Spacing = 8, Children = {
            Ui.Note("The clock adds the stored offset to the time it reads from the bridge. Before switching back to the bridge, reset the offset to 0; the bridge then sets the exact time on its next sync."),
            Ui.Row(reset) } } };
        timeControls.AddRange(new Control[] { mode, nudges, reset });
        foreach (var x in new Control[] { shownText, targetText, shownDateWrong, setTargetDate, shownDate, targetDate }) { x.VerticalAlignment = VerticalAlignment.Center; }
        shownText.TextChanged += (_, _) => Refresh(); targetText.TextChanged += (_, _) => Refresh();
        shownDateWrong.IsCheckedChanged += (_, _) => Refresh(); setTargetDate.IsCheckedChanged += (_, _) => Refresh();
        time = Ui.Card("Clock time", g, firmwareWarning,
            Ui.Note("Choose the time to show and press Set. The clock cannot report its time, so the app estimates it from the bridge's last sync and every change made here."),
            Ui.Wrap(Ui.Note("Set the clock to"), mode), manualRow, Ui.Row(set, plan), nudges, correct, bridge,
            Ui.Note("Whole minutes only; the clock keeps its own seconds. Check the clock face after each change."));
        plan.MaxWidth = 380;

        var (autoRow, autoBox) = Ui.Switch("Connect to the saved clock automatically", "Turn off while the OpenGlance Bridge is in use; the clock accepts one connection at a time.", v => c.AutoConnect = v);
        autoConnect = autoBox;
        connection = Ui.Card("Connection", autoRow);

        handState = Ui.Text("");
        startCal = Ui.Btn("Start calibration", Kind.Secondary, async () =>
        {
            if (await Dialogs.Confirm(window, "Start calibration?", "The hands will move.", "Start calibration")) c.StartCalibration();
        });
        confirmCal = Ui.Btn("Confirm hands at 12", Kind.Primary, async () =>
        {
            if (await Dialogs.Confirm(window, "Both hands point exactly to 12?", "Confirm only when the clock has stopped and shows Confirm with a green segment.", "Confirm hands at 12")) c.ConfirmCalibration();
        });
        var guide = new Expander { Header = "Hands stop short of 12 (e.g. 12:14)? Reseating guide", Content = new StackPanel { Spacing = 4, Children = {
            Ui.Text("1. Only while the clock is stopped, showing Confirm and the green segment.", 13),
            Ui.Text("2. Pull the long minute hand straight off, then the short hour hand. Do not twist hands on the shaft.", 13),
            Ui.Text("3. Refit the hour hand pointing exactly at 12, then the minute hand at 12, keeping clearance from the fabric and between hands.", 13),
            Ui.Text("4. Never force anything. Then press Confirm hands at 12.", 13) } } };
        calibration = Ui.Card("Hand calibration", handState,
            Ui.Callout(Ui.Note("Calibration physically moves the hands. Confirm only when the clock has stopped, shows Confirm with a green segment, and both hands point exactly to 12.")),
            Ui.Row(startCal, confirmCal), guide);
    }

    sealed record PlanResult(int? Delta, string Message, int? ClockMinusNow);
    WallTime? Typed(TextBox box, CheckBox withDate, CalendarDatePicker date)
    {
        if (ClockTimeCorrection.ParseTime(box.Text ?? "") is not (int h, int mi)) return null;
        return new WallTime(h, mi, withDate.IsChecked == true && date.SelectedDate is DateTime d ? DateOnly.FromDateTime(d) : null);
    }
    DateTime? Estimate(DateTime now) => m.Clock.Settings is ClockSettings s ? ClockTimeCorrection.EstimatedClockTime(now, s.TimeOffsetMinutes, m.Clock.TimeBaseline) : null;
    bool TypedShown => !string.IsNullOrWhiteSpace(shownText.Text);

    /// What Set would do now. A blank "Clock shows" uses the estimate.
    PlanResult Plan(DateTime now)
    {
        WallTime shown;
        string lead = "";
        if (TypedShown)
        {
            if (Typed(shownText, shownDateWrong, shownDate) is not WallTime t) return new(null, "Type the clock's time like 6:27 pm or 18:27.", null);
            shown = t; lead = "Using the clock time you typed. ";
        }
        else
        {
            if (Estimate(now) is not DateTime e) return new(null, "Connect the clock first.", null);
            shown = new WallTime(e.Hour, e.Minute, shownDateWrong.IsChecked == true && shownDate.SelectedDate is DateTime d ? DateOnly.FromDateTime(d) : null);
        }
        WallTime? wanted = null;
        if (manual)
        {
            if (string.IsNullOrWhiteSpace(targetText.Text)) return new(null, lead + "Type the time to set.", null);
            if (Typed(targetText, setTargetDate, targetDate) is not WallTime t) return new(null, lead + "Type the time to set like 7:55 pm or 19:55.", null);
            wanted = t;
        }
        if (ClockTimeCorrection.Minutes(now, shown, wanted) is not int delta) return new(null, lead, null);
        int? after = null;
        if (TypedShown)
        {
            var pc = new WallTime(now.Hour, now.Minute, wanted?.Date is not null ? DateOnly.FromDateTime(now) : null);
            after = wanted is WallTime w ? ClockTimeCorrection.Minutes(now, pc, w) ?? 0 : 0;
        }
        var move = delta == 0 ? (manual ? "The clock already shows that time." : "The clock already matches this PC.") : $"Set moves the clock {ClockTimeCorrection.Describe(delta)}.";
        return new(delta, lead + move, after);
    }

    public void Refresh()
    {
        var c = m.Clock;
        find.Content = c.IsScanning ? "Searching…" : "Find clocks";
        find.IsEnabled = !c.IsScanning && !c.IsConnecting && !c.IsConnected && c.BluetoothAvailable;
        searching.Text = c.IsScanning ? "Looking for clocks…" : c.IsConnecting ? "Connecting…" : "";
        forget.IsEnabled = c.HasRememberedClock;
        demo.IsVisible = !c.IsConnected && !c.DemoMode;
        exitDemo.IsVisible = c.DemoMode;
        var key = string.Join("|", c.Nearby.Select(n => $"{n.Address}{n.Signal}")) + c.IsConnecting + c.IsConnected;
        if (key != nearbyKey)
        {
            nearbyKey = key;
            nearby.Children.Clear();
            foreach (var n in c.Nearby)
            {
                var item = n;
                var left = new StackPanel { Children = { Ui.Text(n.Name, 14, FontWeight.SemiBold), Ui.Note($"{n.SignalLabel} · {n.Signal} dBm") } };
                var connect = Ui.Btn("Connect", Kind.Primary, () => c.Connect(item.Address, item.Name));
                connect.IsEnabled = !c.IsConnecting && !c.IsConnected;
                var row = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), Margin = new Thickness(0, 6) };
                Grid.SetColumn(connect, 1); row.Children.Add(left); row.Children.Add(connect);
                nearby.Children.Add(row);
                nearby.Children.Add(new Border { Height = 1 }.Res(Border.BackgroundProperty, "Line"));
            }
        }

        // Clock time
        var now = DateTime.Now;
        bool supported = c.TimeCorrectionSupported == true;
        bool enabled = c.CanControl && supported && !c.CalibrationBusy;
        kv[0].Text = now.ToString("d MMM yyyy 'at' h:mm:ss tt");
        kv[1].Text = c.FirmwareVersion ?? (c.IsConnected ? "Not reported" : "—");
        kv[2].Text = Estimate(now) is DateTime e ? e.ToString("h:mm tt") + (TypedShown ? "" : " (estimate)") : "—";
        kv[3].Text = c.Settings is ClockSettings s ? $"{s.TimeOffsetMinutes} min" : "—";
        firmwareWarning.IsVisible = (c.IsConnected || c.DemoMode) && !supported;
        firmwareWarningText.Text = c.FirmwareVersion is null
            ? "This clock has not reported its firmware version, so time correction stays off. Reconnect to try again."
            : "Time correction needs clock firmware 1.6.7 or later. Older firmware ignores the time offset.";
        manualRow.IsVisible = manual;
        targetDate.IsVisible = setTargetDate.IsChecked == true;
        shownDate.IsVisible = shownDateWrong.IsChecked == true;
        foreach (var x in timeControls) x.IsEnabled = enabled;
        reset.IsEnabled = enabled && (c.Settings?.TimeOffsetMinutes ?? 0) != 0;
        var p = Plan(now);
        plan.Text = p.Message;
        set.IsEnabled = enabled && (p.Delta ?? 0) != 0;

        autoConnect.SetQuietly(c.AutoConnect);
        handState.Text = "State reported by the clock: " + c.HandStatus;
        startCal.IsEnabled = c.CanControl && !c.IsCalibrating;
        confirmCal.IsEnabled = c.CanControl && !c.IsCalibrating && c.NeedsCalibrationConfirmation;
    }
}
