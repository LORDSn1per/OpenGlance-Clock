using System.Diagnostics;
using System.IO.Ports;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Platform.Storage;
using Avalonia.Styling;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;
using FlasherCore;

namespace OpenGlanceFlasher;

enum Phase { Idle, Flashing, Verifying, Done, Failed }

/// Port of the Mac Flasher's model. UI-thread state; `Changed` fires after updates.
sealed class FlasherModel
{
    public event Action? Changed;
    public List<string> Ports { get; private set; } = new();
    public string Port { get; set; } = "";
    public string? FirmwarePath { get; private set; }
    public BridgeFirmware? Firmware { get; private set; }
    public string? FirmwareProblem { get; private set; }
    public bool EraseAll { get; set; }
    public Phase Phase { get; private set; } = Phase.Idle;
    public double? Progress { get; private set; }
    public string Status { get; private set; } = "Connect the ESP32-C3 with a USB-C data cable.";
    public string? Result { get; private set; }
    public StringBuilder Log { get; } = new();
    Process? process;

    public bool Busy => Phase is Phase.Flashing or Phase.Verifying;
    public bool CanFlash => !Busy && Port.Length > 0 && Firmware is not null && SupportFilesPresent;
    public static string Version { get; set; } = "dev";
    static readonly Assembly Me = typeof(FlasherModel).Assembly;
    public bool SupportFilesPresent => new[] { "Support/bootloader.bin", "Support/partitions.bin", "Support/boot_app0.bin", "Tools/esptool.exe" }
        .All(n => Me.GetManifestResourceInfo(n) is not null);

    public FlasherModel()
    {
        RefreshPorts();
        LoadDefaultFirmware();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        timer.Tick += (_, _) => { if (!Busy) RefreshPorts(); };
        timer.Start();
    }
    void Notify() => Changed?.Invoke();

    public void RefreshPorts()
    {
        List<string> found;
        try { found = SerialPorts.Order(SerialPort.GetPortNames()); } catch { found = new(); }
        if (found.SequenceEqual(Ports) && (Port.Length > 0 || found.Count == 0)) return;
        Port = SerialPorts.Choose(Ports, found, Port);
        Ports = found;
        Notify();
    }

    void LoadDefaultFirmware()
    {
        var exeDir = AppContext.BaseDirectory;
        var folders = new[] { Path.Combine(exeDir, "..", "Firmware"), Path.Combine(exeDir, "Firmware"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads") };
        (double Version, string Path)? best = null;
        foreach (var folder in folders.Where(Directory.Exists))
            foreach (var path in Directory.GetFiles(folder, "OpenGlance Bridge * OTA.bin"))
            {
                var name = System.IO.Path.GetFileName(path);
                if (double.TryParse(name["OpenGlance Bridge ".Length..^" OTA.bin".Length], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var v)
                    && v > (best?.Version ?? -1)) best = (v, path);
            }
        if (best is { } b) Load(b.Path);
    }

    public void Load(string path)
    {
        FirmwarePath = path; Result = null;
        try
        {
            Firmware = new BridgeFirmware(File.ReadAllBytes(path)); FirmwareProblem = null;
            if (Phase is Phase.Done or Phase.Failed) Phase = Phase.Idle;
        }
        catch (BridgeFirmware.InvalidFirmware e) { Firmware = null; FirmwareProblem = e.Message; }
        catch (Exception e) { Firmware = null; FirmwareProblem = "The file could not be read: " + e.Message; }
        Notify();
    }

    /// Unpacks the embedded esptool once into %LOCALAPPDATA% (re-checked by SHA-256 every run).
    static string ExtractTool()
    {
        using var source = Me.GetManifestResourceStream("Tools/esptool.exe")!;
        var bytes = new byte[source.Length]; source.ReadExactly(bytes);
        var hash = Convert.ToHexString(SHA256.HashData(bytes));
        var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenGlance Flasher", "esptool-" + hash[..12]);
        var exe = Path.Combine(dir, "esptool.exe");
        if (!File.Exists(exe) || Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(exe))) != hash)
        {
            Directory.CreateDirectory(dir);
            File.WriteAllBytes(exe + ".tmp", bytes);
            File.Move(exe + ".tmp", exe, overwrite: true);
            using var licence = Me.GetManifestResourceStream("Tools/esptool-LICENSE.txt");
            if (licence is not null) { using var f = File.Create(Path.Combine(dir, "LICENSE.txt")); licence.CopyTo(f); }
        }
        return exe;
    }
    static (string Path, int Size) ExtractSupport(string dir, string name)
    {
        using var s = Me.GetManifestResourceStream("Support/" + name)!;
        var path = Path.Combine(dir, name);
        using (var f = File.Create(path)) s.CopyTo(f);
        return (path, (int)new FileInfo(path).Length);
    }

    public async void Flash()
    {
        if (!CanFlash || Firmware is null || FirmwarePath is null) return;
        string exe; FlashPlan plan; string work;
        try
        {
            exe = ExtractTool();
            work = Path.Combine(Path.GetTempPath(), "OpenGlanceFlasher-" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(work);
            plan = new FlashPlan(ExtractSupport(work, "bootloader.bin"), ExtractSupport(work, "partitions.bin"), ExtractSupport(work, "boot_app0.bin"), (FirmwarePath, Firmware.Size));
        }
        catch (Exception e) { Phase = Phase.Failed; Status = "Could not prepare the flashing tools: " + e.Message; Notify(); return; }
        var args = plan.EsptoolArguments(Port, EraseAll);
        var port = Port; var version = Firmware.Version; bool erase = EraseAll;
        Phase = Phase.Flashing; Progress = null; Result = null;
        Status = erase ? $"Erasing and installing {version}…" : $"Installing OpenGlance Bridge {version}…";
        Log.Clear(); Log.Append("> esptool ").Append(string.Join(" ", args.Select(a => a.Contains(' ') ? $"\"{a}\"" : a))).Append('\n');
        Notify();

        var full = new StringBuilder();
        int code = await Task.Run(() => RunEsptool(exe, args, chunk =>
        {
            full.Append(chunk);
            var lines = EsptoolOutput.StripAnsi(chunk).Split('\r', '\n');
            Dispatcher.UIThread.Post(() =>
            {
                foreach (var l in lines)
                {
                    if (EsptoolOutput.ProgressIn(l) is { } p) Progress = plan.OverallProgress(p.Address, p.Percent);
                    if (l.Trim().Length > 0 && (EsptoolOutput.ProgressIn(l) is null || l.Contains("100.0%"))) Log.Append(l).Append('\n');
                }
                Notify();
            });
        }));
        try { Directory.Delete(work, true); } catch { }
        if (code != 0)
        {
            Phase = Phase.Failed;
            Status = cancelled ? "Cancelled. The board needs a complete install before it will run again." : EsptoolOutput.FriendlyError(full.ToString());
            cancelled = false; Notify(); return;
        }
        Progress = 1; Phase = Phase.Verifying;
        Status = "Installed and checksum-verified. Waiting for the bridge to start…";
        Notify();
        var booted = await Task.Run(() => WaitForBanner(port, TimeSpan.FromSeconds(15)));
        Phase = Phase.Done;
        if (booted is not null) { Status = $"OpenGlance Bridge {booted} is installed and running."; Log.Append($"Bridge reported: OpenGlance Bridge {booted} started\n"); }
        else Status = "Installed and checksum-verified. The start-up message wasn't seen; that's usually fine.";
        Result = erase
            ? "Settings were erased. On your phone, join the Wi-Fi hotspot “OpenGlance-Setup” to set up Wi-Fi, then pair the clock."
            : "Existing settings were kept. A brand-new board shows the “OpenGlance-Setup” Wi-Fi hotspot; a configured bridge reconnects to Wi-Fi and the clock at http://openglance.local/.";
        Notify();
    }

    bool cancelled;
    public void Cancel()
    {
        cancelled = true;
        try { process?.Kill(entireProcessTree: true); } catch { }
    }

    int RunEsptool(string exe, List<string> args, Action<string> output)
    {
        var info = new ProcessStartInfo(exe) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8 };
        foreach (var a in args) info.ArgumentList.Add(a);
        info.Environment["NO_COLOR"] = "1"; info.Environment["PYTHONUNBUFFERED"] = "1"; info.Environment["PYTHONIOENCODING"] = "utf-8";
        try { process = Process.Start(info); }
        catch (Exception e) { output("Could not start esptool: " + e.Message + "\n"); return -1; }
        if (process is null) return -1;
        async Task Pump(StreamReader reader)
        {
            var buffer = new char[2048];
            int n;
            while ((n = await reader.ReadAsync(buffer, 0, buffer.Length)) > 0) output(new string(buffer, 0, n));
        }
        Task.WaitAll(Pump(process.StandardOutput), Pump(process.StandardError));
        process.WaitForExit();
        int code = process.ExitCode;
        process.Dispose(); process = null;
        return code;
    }

    /// Reads the bridge's USB serial output after reset and returns the version it reports.
    static string? WaitForBanner(string port, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        var text = new StringBuilder();
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                using var serial = new SerialPort(port, 115200) { ReadTimeout = 300, DtrEnable = false, RtsEnable = false };
                serial.Open();
                while (DateTime.UtcNow < deadline)
                {
                    try { text.Append(serial.ReadExisting()); } catch (TimeoutException) { }
                    if (BootBanner.Version(text.ToString()) is string v) return v;
                    Thread.Sleep(100);
                }
            }
            catch { Thread.Sleep(300); }   // the port re-enumerates after the reset
        }
        return null;
    }

    /// Screenshot-only states; no process is started.
    public void Demo(string state)
    {
        switch (state)
        {
            case "flashing": Phase = Phase.Flashing; Progress = 0.46; Status = $"Installing OpenGlance Bridge {Firmware?.Version}…"; break;
            case "done": Phase = Phase.Done; Progress = 1; Status = $"OpenGlance Bridge {Firmware?.Version} is installed and running.";
                Result = "Existing settings were kept. A brand-new board shows the “OpenGlance-Setup” Wi-Fi hotspot; a configured bridge reconnects to Wi-Fi and the clock at http://openglance.local/."; break;
            case "failed": Phase = Phase.Failed; Progress = 0.03; Status = EsptoolOutput.FriendlyError("Failed to connect to ESP32-C3: No serial data received."); break;
        }
        Log.Clear(); Log.Append("Screenshot preview — nothing was flashed.\n");
        Notify();
    }
}

sealed class FlasherWindow : Window
{
    readonly FlasherModel model;
    readonly ComboBox portBox = new() { MinWidth = 160 };
    readonly TextBlock noPorts = Note("No USB board found. Plug in the ESP32-C3 with a data cable."), fileName = new() { FontWeight = FontWeight.Medium, TextTrimming = TextTrimming.CharacterEllipsis },
        fileOk = new() { TextWrapping = TextWrapping.Wrap }, fileProblem = new() { TextWrapping = TextWrapping.Wrap }, status = new() { TextWrapping = TextWrapping.Wrap },
        missing = new() { Text = "This app is missing its bundled flashing files. Rebuild it with build.sh.", TextWrapping = TextWrapping.Wrap };
    readonly Button refresh, choose, install, cancel;
    readonly CheckBox erase = new();
    readonly ProgressBar progress = new() { Minimum = 0, Maximum = 1, Height = 6 };
    readonly Border result;
    readonly TextBlock resultText = new() { TextWrapping = TextWrapping.Wrap };
    readonly TextBox log = new() { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, Height = 160, FontFamily = new FontFamily("Cascadia Mono, Consolas, Menlo, monospace"), FontSize = 11 };
    readonly Expander details;
    bool updating, quitConfirmed;
    static readonly IBrush Teal = new SolidColorBrush(Color.Parse("#16897F"));
    static readonly IBrush Error = new SolidColorBrush(Color.Parse("#C42B1C"));

    static TextBlock Note(string text) => new() { Text = text, FontSize = 12, TextWrapping = TextWrapping.Wrap, Opacity = 0.7 };

    static Border Card(int number, string title, params Control[] children)
    {
        var badge = new Border { Width = 20, Height = 20, CornerRadius = new CornerRadius(10), Background = Teal,
            Child = new TextBlock { Text = number.ToString(), FontSize = 12, FontWeight = FontWeight.Bold, Foreground = Brushes.White, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center } };
        var head = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { badge, new TextBlock { Text = title, FontSize = 15, FontWeight = FontWeight.SemiBold, VerticalAlignment = VerticalAlignment.Center } } };
        var stack = new StackPanel { Spacing = 10, Children = { head } };
        foreach (var c in children) stack.Children.Add(c);
        var card = new Border { CornerRadius = new CornerRadius(14), Padding = new Thickness(16), BorderThickness = new Thickness(1), Child = stack };
        card.Bind(Border.BackgroundProperty, card.GetResourceObservable("SystemControlBackgroundAltHighBrush"));
        card.Bind(Border.BorderBrushProperty, card.GetResourceObservable("SystemControlForegroundBaseLowBrush"));
        return card;
    }

    public FlasherWindow(FlasherModel model)
    {
        this.model = model;
        Title = "OpenGlance Flasher"; Width = 560; Height = 720; MinWidth = 520; MinHeight = 600;
        var logoStream = AssetLoader.Open(new Uri("avares://OpenGlanceFlasher/Assets/Logo.png"));
        var logo = new Bitmap(logoStream);
        Icon = new WindowIcon(AssetLoader.Open(new Uri("avares://OpenGlanceFlasher/Assets/Logo.png")));

        var header = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto") };
        var logoBox = new Border { Width = 46, Height = 46, CornerRadius = new CornerRadius(11), ClipToBounds = true, Child = new Image { Source = logo } };
        var titles = new StackPanel { Spacing = 2, Margin = new Thickness(12, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center, Children = {
            new TextBlock { Text = "OpenGlance Flasher", FontSize = 20, FontWeight = FontWeight.SemiBold },
            Note("Install bridge firmware on an ESP32-C3 over USB") } };
        var version = Note("v" + FlasherModel.Version); version.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(titles, 1); Grid.SetColumn(version, 2);
        header.Children.Add(logoBox); header.Children.Add(titles); header.Children.Add(version);

        portBox.SelectionChanged += (_, _) => { if (!updating && portBox.SelectedItem is string p) model.Port = p; Refresh(); };
        refresh = new Button { Content = "Refresh" };
        refresh.Click += (_, _) => model.RefreshPorts();
        var board = Card(1, "Board", noPorts, new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { new TextBlock { Text = "USB port", VerticalAlignment = VerticalAlignment.Center }, portBox, refresh } },
            Note("The ESP32-C3 Super Mini appears as a new COM port when plugged in. It needs no driver on Windows 10 and 11."));

        choose = new Button { Content = "Choose firmware…" };
        choose.Click += async (_, _) =>
        {
            var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Choose OpenGlance Bridge firmware", AllowMultiple = false,
                FileTypeFilter = new[] { new FilePickerFileType("Firmware (*.bin)") { Patterns = new[] { "*.bin" } } },
            });
            if (files.Count > 0 && files[0].TryGetLocalPath() is string path) model.Load(path);
        };
        fileOk.Foreground = Teal; fileProblem.Foreground = Error;
        var firmware = Card(2, "Firmware", fileName, fileOk, fileProblem, choose);

        erase.Content = new StackPanel { Spacing = 2, Children = { new TextBlock { Text = "Erase the whole chip first" },
            Note("Removes the bridge's Wi-Fi settings and clock pairing. Not needed for a new board.") } };
        erase.IsCheckedChanged += (_, _) => { if (!updating) model.EraseAll = erase.IsChecked == true; };
        missing.Foreground = Error;
        install = new Button { Content = "Install firmware", MinWidth = 160, HorizontalContentAlignment = HorizontalAlignment.Center, Padding = new Thickness(16, 8) };
        install.Classes.Add("accent");
        install.Click += (_, _) => model.Flash();
        cancel = new Button { Content = "Cancel" };
        cancel.Click += (_, _) => model.Cancel();
        result = new Border { CornerRadius = new CornerRadius(10), Padding = new Thickness(10), Background = new SolidColorBrush(Color.Parse("#2016897F")), Child = resultText };
        var installCard = Card(3, "Install", erase, missing, new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { install, cancel } }, progress, status, result);

        details = new Expander { Header = "Details", Content = log, HorizontalAlignment = HorizontalAlignment.Stretch };
        var stack = new StackPanel { Spacing = 14, Margin = new Thickness(20), Children = { header, board, firmware, installCard, details } };
        Content = new ScrollViewer { Content = stack, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };

        DragDrop.SetAllowDrop(this, true);
        AddHandler(DragDrop.DropEvent, (_, e) =>
        {
            if (model.Busy) return;
            var file = e.Data.GetFiles()?.FirstOrDefault()?.TryGetLocalPath();
            if (file is not null) model.Load(file);
        });
        Closing += async (_, e) =>
        {
            if (!model.Busy || quitConfirmed) return;
            e.Cancel = true;
            if (await Confirm()) { quitConfirmed = true; model.Cancel(); Close(); }
        };
        model.Changed += () => Dispatcher.UIThread.Post(Refresh);
        Refresh();
    }

    async Task<bool> Confirm()
    {
        var keep = new Button { Content = "Keep installing" }; var quit = new Button { Content = "Quit anyway" };
        var w = new Window { Title = "OpenGlance Flasher", Width = 400, SizeToContent = SizeToContent.Height, CanResize = false, WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Content = new StackPanel { Margin = new Thickness(20), Spacing = 12, Children = {
                new TextBlock { Text = "Firmware is being installed", FontSize = 16, FontWeight = FontWeight.SemiBold },
                Note("Quitting now leaves the board unable to start until it is flashed again."),
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right, Children = { keep, quit } } } } };
        keep.Click += (_, _) => w.Close(false); quit.Click += (_, _) => w.Close(true);
        return await w.ShowDialog<bool>(this);
    }

    public void ExpandLog() => details.IsExpanded = true;

    void Refresh()
    {
        updating = true;
        var m = model;
        noPorts.IsVisible = m.Ports.Count == 0;
        portBox.ItemsSource = m.Ports.ToList();
        portBox.SelectedItem = m.Port.Length > 0 ? m.Port : null;
        portBox.IsEnabled = !m.Busy && m.Ports.Count > 0; refresh.IsEnabled = !m.Busy;
        fileName.Text = m.FirmwarePath is string p ? Path.GetFileName(p) : "No firmware chosen. Choose an “OpenGlance Bridge … OTA.bin” file, or drop it here.";
        fileOk.IsVisible = m.Firmware is not null;
        if (m.Firmware is BridgeFirmware fw) fileOk.Text = $"✓ OpenGlance Bridge {fw.Version} · {fw.Size / 1024.0 / 1024.0:0.0} MB · ESP32-C3";
        fileProblem.IsVisible = m.FirmwareProblem is not null; fileProblem.Text = "⚠ " + m.FirmwareProblem;
        choose.IsEnabled = !m.Busy;
        erase.IsChecked = m.EraseAll; erase.IsEnabled = !m.Busy;
        missing.IsVisible = !m.SupportFilesPresent;
        install.Content = m.Busy ? "Installing…" : "Install firmware";
        install.IsEnabled = m.CanFlash;
        cancel.IsVisible = m.Phase == Phase.Flashing;
        progress.IsVisible = m.Phase != Phase.Idle;
        progress.IsIndeterminate = m.Progress is null && m.Busy;
        progress.Value = m.Progress ?? 0;
        status.Text = m.Status;
        if (m.Phase == Phase.Failed) status.Foreground = Error; else status.ClearValue(TextBlock.ForegroundProperty);
        result.IsVisible = m.Result is not null; resultText.Text = m.Result ?? "";
        var text = m.Log.Length == 0 ? "No output yet." : m.Log.ToString();
        if (log.Text != text) { log.Text = text; log.CaretIndex = text.Length; }
        updating = false;
    }

    public void SaveScreenshot(string path)
    {
        using var bitmap = new RenderTargetBitmap(new PixelSize((int)Bounds.Width, (int)Bounds.Height), new Vector(96, 96));
        bitmap.Render(this);
        bitmap.Save(path);
    }
}

sealed class App : Application
{
    public static string[] Args = Array.Empty<string>();
    public override void Initialize()
    {
        Styles.Add(new FluentTheme());
        Resources["SystemAccentColor"] = Color.Parse("#16897F");
        Resources["SystemAccentColorDark1"] = Color.Parse("#127A71");
        Resources["SystemAccentColorLight1"] = Color.Parse("#2A9D92");
        RequestedThemeVariant = Args.Contains("--dark") ? ThemeVariant.Dark : Args.Contains("--light") ? ThemeVariant.Light : ThemeVariant.Default;
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var info = Assembly.GetEntryAssembly()?.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "dev";
            FlasherModel.Version = info.Split('+')[0];
            var model = new FlasherModel();
            var window = new FlasherWindow(model);
            desktop.MainWindow = window;
            string? Arg(string name) { int i = Array.IndexOf(Args, name); return i >= 0 && i + 1 < Args.Length ? Args[i + 1] : null; }
            // `--screenshot <file.png> [--firmware <bin>] [--state flashing|done|failed] [--log]` renders and exits. Never flashes.
            if (Arg("--screenshot") is string shot)
            {
                if (Arg("--firmware") is string fw) model.Load(fw);
                if (Arg("--state") is string state) model.Demo(state);
                if (Args.Contains("--log")) window.ExpandLog();
                window.Opened += async (_, _) => { await Task.Delay(1000); window.SaveScreenshot(shot); Environment.Exit(0); };
            }
        }
        base.OnFrameworkInitializationCompleted();
    }
}

static class Program
{
    [STAThread]
    public static void Main(string[] args)
    {
        App.Args = args;
        AppBuilder.Configure<App>().UsePlatformDetect().LogToTrace().StartWithClassicDesktopLifetime(args);
    }
}
