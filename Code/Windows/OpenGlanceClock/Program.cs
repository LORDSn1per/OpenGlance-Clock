using System.Reflection;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Styling;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;
using GlanceProtocol;

namespace OpenGlance;

public sealed class App : Application
{
    public static string[] Args = Array.Empty<string>();

    public override void Initialize()
    {
        Styles.Add(new FluentTheme());
        Theme.Install(this);
        RequestedThemeVariant = Args.Contains("--dark") ? ThemeVariant.Dark : Args.Contains("--light") ? ThemeVariant.Light : ThemeVariant.Default;
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var info = Assembly.GetEntryAssembly()?.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "dev";
            MainWindow.Version = info.Split('+')[0];
            int shot = Array.IndexOf(Args, "--screenshot");
            Store.ReadOnly = shot >= 0;
            Store.Load();
            var model = new AppModel();
            var window = new MainWindow(model);
            desktop.MainWindow = window;
            if (shot >= 0 && shot + 1 < Args.Length) Screenshot(window, model, Args[shot + 1]);
            else window.Opened += (_, _) => model.Clock.ConnectSavedOnLaunch();
        }
        base.OnFrameworkInitializationCompleted();
    }

    /// `--screenshot <file.png> [--tab name] [--dark] [--height px] [--fill]` renders a demo window and exits.
    /// Demo data only: no Bluetooth, and the owner's settings are neither read nor written.
    static void Screenshot(MainWindow window, AppModel model, string path)
    {
        string Arg(string name, string fallback) { int i = Array.IndexOf(Args, name); return i >= 0 && i + 1 < Args.Length ? Args[i + 1] : fallback; }
        window.Height = double.Parse(Arg("--height", "820"), System.Globalization.CultureInfo.InvariantCulture);
        window.Width = 1100;
        model.Clock.SetDemo(true);
        var f = new HourlyForecast();
        var start = DateTimeOffset.FromUnixTimeSeconds(DateTimeOffset.Now.ToUnixTimeSeconds() / 3600 * 3600);
        double[] chance = { 8, 6, 3, 0, 0, 3, 9, 18, 55, 70, 59, 48 };
        double[] rain = { 0, 0, 0, 0, 0, 0, 0, 0.2, 0.8, 1.2, 0.2, 0 };
        for (int i = 0; i < 24; i++)
        {
            f.Hours.Add(start.AddHours(i)); f.Temperature.Add(17 - 6 * Math.Sin(i / 24.0 * Math.PI));
            f.Chance.Add((int)chance[i % 12]); f.Rain.Add(rain[i % 12]);
        }
        f.Current = 17;
        Store.State.Place = new WeatherPlace("Canberra, Australian Capital Territory, Australia", -35.28, 149.13);
        Store.State.Weather = Store.State.Weather with { On = true };
        model.Weather.LoadDemo(f);
        if (Args.Contains("--fill"))
            Store.State.Alarms.Add(new AlarmEntry { Hour = 6, Minute = 45, Days = 31, Sound = 6, Text = "Wake" });
        var tabName = Arg("--tab", "home");
        if (tabName.Equals("messages", StringComparison.OrdinalIgnoreCase)) tabName = "alerts";
        var tab = Enum.GetValues<TabId>().FirstOrDefault(t => t.ToString().Equals(tabName, StringComparison.OrdinalIgnoreCase));
        if (Args.Contains("--seconds")) model.Seconds.Apply(true, 18, 3);
        window.Opened += async (_, _) =>
        {
            window.Show(tab);
            await Task.Delay(1200);
            window.SaveScreenshot(path);
            Environment.Exit(0);
        };
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
