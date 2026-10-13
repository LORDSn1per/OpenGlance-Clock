using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Shapes;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;

namespace OpenGlance;

/// Same colour tokens as the bridge web page and the Mac app, light and dark.
public static class Theme
{
    static readonly (string Key, string Light, string Dark)[] Tokens =
    {
        ("Bg", "#EEF2F4", "#1C2328"), ("Card", "#FFFFFF", "#252E35"), ("Ink", "#23323D", "#E6ECEF"), ("Muted", "#5F707C", "#9FB0BA"),
        ("Line", "#D8E0E5", "#37434C"), ("Teal", "#16897F", "#16897F"), ("TealBg", "#E3F3F1", "#1D4441"), ("TealInk", "#0F6F67", "#7FDCD1"),
        ("Warn", "#A35C00", "#F0B45A"), ("WarnBg", "#FFF4E0", "#3A2E17"), ("Err", "#B3261E", "#FF8A80"), ("ErrBg", "#FDE9E7", "#3D2321"),
        ("Ok", "#1D7A3E", "#7ED69A"), ("OkBg", "#E5F4EA", "#1D3826"), ("White", "#FFFFFF", "#FFFFFF"),
    };
    public static void Install(Application app)
    {
        var light = new ResourceDictionary(); var dark = new ResourceDictionary();
        foreach (var (key, l, d) in Tokens) { light[key] = new SolidColorBrush(Color.Parse(l)); dark[key] = new SolidColorBrush(Color.Parse(d)); }
        app.Resources.ThemeDictionaries[ThemeVariant.Light] = light;
        app.Resources.ThemeDictionaries[ThemeVariant.Dark] = dark;
        app.Resources["SystemAccentColor"] = Color.Parse("#16897F");
        app.Resources["SystemAccentColorDark1"] = Color.Parse("#127A71");
        app.Resources["SystemAccentColorLight1"] = Color.Parse("#2A9D92");
        app.Styles.Add(new Style(x => x.OfType<Button>().Class("og").Class(":pointerover")) { Setters = { new Setter(Visual.OpacityProperty, 0.88) } });
        app.Styles.Add(new Style(x => x.OfType<Button>().Class("og").Class(":pressed")) { Setters = { new Setter(Visual.OpacityProperty, 0.75) } });
        app.Styles.Add(new Style(x => x.OfType<Button>().Class("og").Class(":disabled")) { Setters = { new Setter(Visual.OpacityProperty, 0.45) } });
        app.Styles.Add(new Style(x => x.OfType<Expander>()) { Setters = { new Setter(Layoutable.HorizontalAlignmentProperty, HorizontalAlignment.Stretch) } });
    }
    public static T Res<T>(this T control, AvaloniaProperty property, string key) where T : Control
    {
        control.Bind(property, control.GetResourceObservable(key));
        return control;
    }
}

public enum Kind { Primary, Secondary, Danger }
public enum PillKind { Good, Mid, Bad, Plain }

public static class Ui
{
    static readonly IControlTemplate ButtonTemplate = new FuncControlTemplate<Button>((b, _) => new Border
    {
        CornerRadius = new CornerRadius(9),
        Padding = new Thickness(14, 7),
        [!Border.BackgroundProperty] = b[!TemplatedControl.BackgroundProperty],
        Child = new ContentPresenter
        {
            Name = "OgContent",
            HorizontalContentAlignment = HorizontalAlignment.Center,
            VerticalContentAlignment = VerticalAlignment.Center,
            [!ContentPresenter.ContentProperty] = b[!ContentControl.ContentProperty],
        },
    });

    public static Button Btn(string text, Kind kind, Action click)
    {
        var b = new Button { Content = text, Template = ButtonTemplate, Cursor = new Cursor(StandardCursorType.Hand), FontSize = 14, FontWeight = FontWeight.SemiBold };
        b.Classes.Add("og");
        var (fg, bg) = kind switch { Kind.Primary => ("White", "Teal"), Kind.Secondary => ("TealInk", "TealBg"), _ => ("Err", "ErrBg") };
        b.Res(TemplatedControl.BackgroundProperty, bg).Res(TemplatedControl.ForegroundProperty, fg);
        b.Click += (_, _) => click();
        return b;
    }
    public static Button Link(string text, Action click)
    {
        var b = Btn(text, Kind.Secondary, click);
        b.Background = Brushes.Transparent; b.Padding = new Thickness(0);
        b.Template = new FuncControlTemplate<Button>((x, _) => new ContentPresenter { [!ContentPresenter.ContentProperty] = x[!ContentControl.ContentProperty] });
        return b;
    }

    public static TextBlock Text(string text, double size = 14, FontWeight? weight = null) =>
        new TextBlock { Text = text, FontSize = size, FontWeight = weight ?? FontWeight.Normal, TextWrapping = TextWrapping.Wrap }.Res(TextBlock.ForegroundProperty, "Ink");
    public static TextBlock Note(string text = "") =>
        new TextBlock { Text = text, FontSize = 13, TextWrapping = TextWrapping.Wrap }.Res(TextBlock.ForegroundProperty, "Muted");
    public static TextBlock Big(string text = "") => Text(text, 26, FontWeight.SemiBold);

    public static Border Callout(TextBlock text, string fg = "Warn", string bg = "WarnBg")
    {
        text.Res(TextBlock.ForegroundProperty, fg); text.FontSize = 13;
        return new Border { CornerRadius = new CornerRadius(10), Padding = new Thickness(10), Child = text }.Res(Border.BackgroundProperty, bg);
    }

    public static StackPanel Row(params Control[] children)
    {
        var p = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        foreach (var c in children) { c.VerticalAlignment = VerticalAlignment.Center; p.Children.Add(c); }
        return p;
    }
    public static WrapPanel Wrap(params Control[] children)
    {
        var p = new WrapPanel { Orientation = Orientation.Horizontal };
        foreach (var c in children) { c.Margin = new Thickness(0, 0, 8, 6); c.VerticalAlignment = VerticalAlignment.Center; p.Children.Add(c); }
        return p;
    }

    public static Border Card(string title, params Control[] children)
    {
        var stack = new StackPanel { Spacing = 10 };
        stack.Children.Add(Text(title, 15, FontWeight.SemiBold));
        foreach (var c in children) stack.Children.Add(c);
        return new Border { CornerRadius = new CornerRadius(14), BorderThickness = new Thickness(1), Padding = new Thickness(16), Child = stack }
            .Res(Border.BackgroundProperty, "Card").Res(Border.BorderBrushProperty, "Line");
    }
    public static StackPanel Body(this Border card) => (StackPanel)card.Child!;

    /// Two-column key/value list. Returns the value text blocks so callers can update them.
    public static (Grid Grid, TextBlock[] Values) KV(params string[] keys)
    {
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*") };
        var values = new TextBlock[keys.Length];
        for (int i = 0; i < keys.Length; i++)
        {
            g.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            var k = Note(keys[i]); k.FontSize = 14; k.Margin = new Thickness(0, 0, 14, 4); Grid.SetRow(k, i);
            var v = Text("—"); v.Margin = new Thickness(0, 0, 0, 4); Grid.SetRow(v, i); Grid.SetColumn(v, 1);
            g.Children.Add(k); g.Children.Add(v); values[i] = v;
        }
        return (g, values);
    }

    /// Title + optional detail on the left, a checkbox on the right, with a divider underneath.
    public static (Control View, CheckBox Box) Switch(string title, string? detail, Action<bool> set)
    {
        var box = new CheckBox { VerticalAlignment = VerticalAlignment.Center };
        box.IsCheckedChanged += (_, _) => { if (box.Tag is null) set(box.IsChecked == true); };
        var left = new StackPanel { Spacing = 2 };
        left.Children.Add(Text(title));
        if (detail is not null) left.Children.Add(Note(detail));
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), Margin = new Thickness(0, 2, 0, 0) };
        Grid.SetColumn(box, 1); g.Children.Add(left); g.Children.Add(box);
        var line = new Border { Height = 1, Margin = new Thickness(0, 6, 0, 0) }.Res(Border.BackgroundProperty, "Line");
        var stack = new StackPanel(); stack.Children.Add(g); stack.Children.Add(line);
        return (stack, box);
    }
    /// Sets a checkbox without firing its change handler.
    public static void SetQuietly(this CheckBox box, bool value)
    {
        if (box.IsChecked == value) return;
        box.Tag = "quiet"; box.IsChecked = value; box.Tag = null;
    }

    public static ComboBox Combo<T>(IEnumerable<(T Value, string Label)> items, T selected, Action<T> set) where T : notnull
    {
        var list = items.ToList();
        var c = new ComboBox { ItemsSource = list.Select(i => i.Label).ToList(), MinWidth = 140 };
        c.SelectedIndex = Math.Max(0, list.FindIndex(i => i.Value.Equals(selected)));
        c.SelectionChanged += (_, _) => { if (c.Tag is null && c.SelectedIndex >= 0) set(list[c.SelectedIndex].Value); };
        return c;
    }
    public static void SelectQuietly<T>(this ComboBox c, IEnumerable<(T Value, string Label)> items, T value) where T : notnull
    {
        int i = items.ToList().FindIndex(x => x.Value.Equals(value));
        if (i < 0 || c.SelectedIndex == i) return;
        c.Tag = "quiet"; c.SelectedIndex = i; c.Tag = null;
    }

    public static Border Pill(out TextBlock text, out Ellipse dot)
    {
        dot = new Ellipse { Width = 8, Height = 8 };
        text = new TextBlock { FontSize = 13, FontWeight = FontWeight.SemiBold, VerticalAlignment = VerticalAlignment.Center };
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { dot, text } };
        dot.VerticalAlignment = VerticalAlignment.Center;
        return new Border { CornerRadius = new CornerRadius(20), Padding = new Thickness(10, 4), Child = row, Margin = new Thickness(6, 0, 0, 0) };
    }
    public static void SetPill(Border pill, TextBlock text, Ellipse dot, string label, PillKind kind)
    {
        var (fg, bg) = kind switch { PillKind.Good => ("Ok", "OkBg"), PillKind.Mid => ("Warn", "WarnBg"), PillKind.Bad => ("Err", "ErrBg"), _ => ("TealInk", "TealBg") };
        text.Text = label;
        text.Res(TextBlock.ForegroundProperty, fg); dot.Res(Shape.FillProperty, fg); pill.Res(Border.BackgroundProperty, bg);
    }

    public static Control SoundPicker(int selected, bool allowNone, Action<int> set, ClockController clock, Func<int> current, out ComboBox combo)
    {
        var items = Enumerable.Range(allowNone ? 0 : 1, allowNone ? 18 : 17).Select(i => (i, GlanceProtocol.Glance.Sounds[i]));
        combo = Combo(items, selected, set);
        var test = Btn("Test", Kind.Secondary, () => clock.TestSound(current()));
        test.Tag = "needs-clock";
        return Row(combo, test);
    }

    public static Control LogList(IEnumerable<ActivityItem> events)
    {
        var stack = new StackPanel { Spacing = 3 };
        var any = false;
        foreach (var e in events)
        {
            any = true;
            var t = new SelectableTextBlock { FontFamily = new FontFamily("Cascadia Mono, Consolas, Menlo, monospace"), FontSize = 12.5, TextWrapping = TextWrapping.Wrap };
            var time = new Avalonia.Controls.Documents.Run(e.Date.ToString("HH:mm:ss") + "  ");
            time.Bind(Avalonia.Controls.Documents.TextElement.ForegroundProperty, t.GetResourceObservable("Muted"));
            var body = new Avalonia.Controls.Documents.Run(string.IsNullOrEmpty(e.Detail) ? e.Title : $"{e.Title} — {e.Detail}");
            body.Bind(Avalonia.Controls.Documents.TextElement.ForegroundProperty, t.GetResourceObservable(e.IsError ? "Err" : "Ink"));
            t.Inlines = new Avalonia.Controls.Documents.InlineCollection { time, body };
            stack.Children.Add(t);
        }
        if (!any) stack.Children.Add(Note("No activity yet."));
        return new Border { CornerRadius = new CornerRadius(10), Padding = new Thickness(10), Child = stack }.Res(Border.BackgroundProperty, "Bg");
    }
}

/// Cards in as many 360 px+ columns as fit, row by row (like the Mac app's adaptive grid).
public sealed class CardGrid : Panel
{
    const double MinColumn = 360, Gap = 16;
    int Columns(double width) => Math.Max(1, (int)((width + Gap) / (MinColumn + Gap)));
    double lastWidth = 1100;

    protected override Size MeasureOverride(Size available)
    {
        double width = double.IsInfinity(available.Width) ? lastWidth : available.Width;
        lastWidth = width;
        int cols = Columns(width);
        double cw = (width - Gap * (cols - 1)) / cols, total = 0;
        var visible = Children.Where(c => c.IsVisible).ToList();
        for (int i = 0; i < visible.Count; i += cols)
        {
            double rowHeight = 0;
            for (int j = i; j < Math.Min(i + cols, visible.Count); j++)
            {
                visible[j].Measure(new Size(cw, double.PositiveInfinity));
                rowHeight = Math.Max(rowHeight, visible[j].DesiredSize.Height);
            }
            total += rowHeight + Gap;
        }
        return new Size(width, Math.Max(0, total - Gap));
    }

    protected override Size ArrangeOverride(Size final)
    {
        int cols = Columns(final.Width);
        double cw = (final.Width - Gap * (cols - 1)) / cols, y = 0;
        var visible = Children.Where(c => c.IsVisible).ToList();
        for (int i = 0; i < visible.Count; i += cols)
        {
            double rowHeight = 0;
            for (int j = i; j < Math.Min(i + cols, visible.Count); j++)
            {
                var c = visible[j];
                c.Arrange(new Rect((j - i) * (cw + Gap), y, cw, c.DesiredSize.Height));
                rowHeight = Math.Max(rowHeight, c.DesiredSize.Height);
            }
            y += rowHeight + Gap;
        }
        return final;
    }
}

public static class Dialogs
{
    static Window Frame(string title, Control body, params Button[] buttons)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right };
        foreach (var b in buttons) row.Children.Add(b);
        var stack = new StackPanel { Spacing = 14, Margin = new Thickness(20), Children = { Ui.Text(title, 16, FontWeight.SemiBold), body, row } };
        return new Window
        {
            Title = "OpenGlance Clock", Width = 440, SizeToContent = SizeToContent.Height, CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner, Content = stack, ShowInTaskbar = false,
        }.Res(TemplatedControl.BackgroundProperty, "Card");
    }

    public static async Task Alert(Window owner, string message)
    {
        Window? w = null;
        w = Frame("OpenGlance Clock", Ui.Text(message), Ui.Btn("OK", Kind.Primary, () => w!.Close()));
        await w.ShowDialog(owner);
    }

    public static async Task<bool> Confirm(Window owner, string title, string message, string action, bool danger = false)
    {
        Window? w = null;
        w = Frame(title, Ui.Note(message), Ui.Btn("Cancel", Kind.Secondary, () => w!.Close(false)), Ui.Btn(action, danger ? Kind.Danger : Kind.Primary, () => w!.Close(true)));
        return await w.ShowDialog<bool>(owner);
    }

    public static async Task<string?> Ask(Window owner, string title, string message, string watermark)
    {
        var box = new TextBox { Watermark = watermark, FontSize = 18 };
        var body = new StackPanel { Spacing = 10, Children = { Ui.Note(message), box } };
        Window? w = null;
        w = Frame(title, body, Ui.Btn("Cancel", Kind.Secondary, () => w!.Close(null)), Ui.Btn("OK", Kind.Primary, () => w!.Close(box.Text)));
        box.KeyDown += (_, e) => { if (e.Key == Key.Enter) w.Close(box.Text); };
        w.Opened += (_, _) => box.Focus();
        return await w.ShowDialog<string?>(owner);
    }
}
