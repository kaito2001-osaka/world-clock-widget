using System.Collections.ObjectModel;
using System.Windows;
using Wpf.Ui.Controls;

namespace WorldClockSettings;

public partial class MainWindow : FluentWindow
{
    private readonly ObservableCollection<CityModel> _cities = new();

    public MainWindow()
    {
        InitializeComponent();

        CitiesList.ItemsSource = _cities;
        CityPicker.ItemsSource = Cities.Major;
        DateFormatBox.ItemsSource = DateFormats.Presets;
        // Live preview while typing a custom pattern (SelectionChanged only fires on pick).
        DateFormatBox.AddHandler(
            System.Windows.Controls.Primitives.TextBoxBase.TextChangedEvent,
            new System.Windows.Controls.TextChangedEventHandler((_, _) => UpdateDatePreview()));

        LoadFromConfig(ConfigModel.Load());
        _cities.CollectionChanged += (_, _) => UpdateAnalogWarning();
    }

    // ---- Populate UI from a config model ----
    private void LoadFromConfig(ConfigModel c)
    {
        _cities.Clear();
        foreach (var city in c.Cities)
            _cities.Add(new CityModel { Label = city.Label, Tz = city.Tz });

        ModeDigital.IsChecked = c.DisplayMode != "analog";
        ModeAnalog.IsChecked  = c.DisplayMode == "analog";

        LayoutVertical.IsChecked   = c.Layout != "horizontal";
        LayoutHorizontal.IsChecked = c.Layout == "horizontal";

        Toggle24.IsChecked      = c.HourFormat != 12;
        ToggleDate.IsChecked    = c.ShowDate;
        DateFormatBox.Text      = string.IsNullOrWhiteSpace(c.DateFormat) ? "ddd, MMM d" : c.DateFormat;
        DateFormatPanel.IsEnabled = c.ShowDate;
        UpdateDatePreview();
        ToggleSeconds.IsChecked = c.ShowSeconds;

        SizeSmall.IsChecked  = c.Size == "small";
        SizeMedium.IsChecked = c.Size is not ("small" or "large");
        SizeLarge.IsChecked  = c.Size == "large";

        OpacitySlider.Value = Math.Clamp(c.Opacity, 10, 100);
        OpacityValue.Text   = $"{(int)OpacitySlider.Value}%";

        ToggleTopmost.IsChecked = c.AlwaysOnTop;
        ToggleLock.IsChecked    = c.LockPosition;
        ToggleStartup.IsChecked = c.LaunchAtStartup;

        UpdateAnalogWarning();
    }

    // ---- Gather a config model from the UI ----
    private ConfigModel BuildConfig()
    {
        return new ConfigModel
        {
            Cities       = _cities.Select(c => new CityModel { Label = c.Label, Tz = c.Tz }).ToList(),
            DisplayMode  = ModeAnalog.IsChecked == true ? "analog" : "digital",
            Layout       = LayoutHorizontal.IsChecked == true ? "horizontal" : "vertical",
            HourFormat   = Toggle24.IsChecked == true ? 24 : 12,
            ShowDate     = ToggleDate.IsChecked == true,
            DateFormat   = string.IsNullOrWhiteSpace(DateFormatBox.Text) ? "ddd, MMM d" : DateFormatBox.Text.Trim(),
            ShowSeconds  = ToggleSeconds.IsChecked == true,
            Size         = SizeSmall.IsChecked == true ? "small"
                         : SizeLarge.IsChecked == true ? "large" : "medium",
            Opacity      = (int)OpacitySlider.Value,
            AlwaysOnTop  = ToggleTopmost.IsChecked == true,
            LockPosition = ToggleLock.IsChecked == true,
            LaunchAtStartup = ToggleStartup.IsChecked == true,
            Theme        = "dark",
        };
    }

    // Writing config == applying to the gadget == persisting (single action).
    private bool ApplyConfig()
    {
        if (_cities.Count == 0)
        {
            System.Windows.MessageBox.Show(this,
                "少なくとも1つの都市が必要です。", "World Clock の設定",
                System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Warning);
            return false;
        }
        try
        {
            var cfg = BuildConfig();
            cfg.Save();
            // Apply autostart immediately so it works even if the gadget isn't running.
            Startup.Apply(cfg.LaunchAtStartup);
            return true;
        }
        catch (Exception ex)
        {
            System.Windows.MessageBox.Show(this,
                $"設定の保存に失敗しました:\n{ex.Message}", "World Clock の設定",
                System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Error);
            return false;
        }
    }

    // ---- City list editing ----
    private void OnAddCity(object sender, RoutedEventArgs e)
    {
        CityModel? toAdd = null;

        if (CityPicker.SelectedItem is Cities.Entry entry)
        {
            toAdd = new CityModel { Label = entry.Label, Tz = entry.Tz };
        }
        else
        {
            var text = (CityPicker.Text ?? "").Trim();
            if (text.Length == 0) return;

            // Match a known major city by label (case-insensitive)...
            var known = Cities.Major.FirstOrDefault(
                m => string.Equals(m.Label, text, StringComparison.OrdinalIgnoreCase));
            if (known != null)
                toAdd = new CityModel { Label = known.Label, Tz = known.Tz };
            else
                // ...otherwise treat the text as a raw IANA id (e.g. "Asia/Tokyo").
                toAdd = new CityModel { Label = LabelFromTz(text), Tz = text };
        }

        if (toAdd != null && !string.IsNullOrWhiteSpace(toAdd.Tz))
        {
            _cities.Add(toAdd);
            CitiesList.SelectedItem = toAdd;
            CityPicker.Text = "";
            CityPicker.SelectedItem = null;
        }
    }

    private static string LabelFromTz(string tz)
    {
        var leaf = tz.Contains('/') ? tz[(tz.LastIndexOf('/') + 1)..] : tz;
        return leaf.Replace('_', ' ');
    }

    private void OnRemoveCity(object sender, RoutedEventArgs e)
    {
        if (CitiesList.SelectedItem is CityModel c)
        {
            int idx = _cities.IndexOf(c);
            _cities.Remove(c);
            if (_cities.Count > 0)
                CitiesList.SelectedIndex = Math.Min(idx, _cities.Count - 1);
        }
    }

    private void OnMoveUp(object sender, RoutedEventArgs e)   => Move(-1);
    private void OnMoveDown(object sender, RoutedEventArgs e) => Move(+1);

    private void Move(int delta)
    {
        int i = CitiesList.SelectedIndex;
        int j = i + delta;
        if (i < 0 || j < 0 || j >= _cities.Count) return;
        _cities.Move(i, j);
        CitiesList.SelectedIndex = j;
    }

    // ---- Misc UI events ----
    private void OnOpacityChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (OpacityValue != null) OpacityValue.Text = $"{(int)e.NewValue}%";
    }

    private void OnModeChanged(object sender, RoutedEventArgs e) => UpdateAnalogWarning();

    private void OnShowDateChanged(object sender, RoutedEventArgs e)
    {
        if (DateFormatPanel != null)
            DateFormatPanel.IsEnabled = ToggleDate.IsChecked == true;
    }

    private void OnDateFormatChanged(object sender, RoutedEventArgs e) => UpdateDatePreview();

    private void UpdateDatePreview()
    {
        if (DatePreview == null || DateFormatBox == null) return;
        var pattern = DateFormatBox.Text ?? "";
        DatePreview.Text = "プレビュー: " + DateFormats.Format(pattern, DateTime.Now);
    }

    private void UpdateAnalogWarning()
    {
        if (AnalogWarning == null) return;
        AnalogWarning.IsOpen = ModeAnalog.IsChecked == true && _cities.Count > 3;
    }

    // ---- OK / Cancel / Apply ----
    private void OnOk(object sender, RoutedEventArgs e)
    {
        if (ApplyConfig()) Close();
    }

    private void OnCancel(object sender, RoutedEventArgs e) => Close();

    private void OnApply(object sender, RoutedEventArgs e) => ApplyConfig();
}
