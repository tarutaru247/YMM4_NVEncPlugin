using System.Windows;
using System.Windows.Controls;

namespace NVEncVideoWriterPlugin;

internal sealed class NvencConfigView : UserControl
{
    private readonly ComboBox _codecComboBox;
    private readonly ComboBox _rateControlComboBox;
    private readonly TextBox _bitrateTextBox;
    private readonly ComboBox _qualityComboBox;
    private readonly CheckBox _fastPresetCheckBox;
    private readonly NvencSettings _settings;

    public NvencConfigView(NvencSettings settings)
    {
        _settings = settings;
        var panel = new StackPanel
        {
            Margin = new Thickness(8),
        };

        panel.Children.Add(new TextBlock
        {
            Text = "コーデック",
            Margin = new Thickness(0, 0, 0, 4),
        });

        _codecComboBox = new ComboBox
        {
            Margin = new Thickness(0, 0, 0, 12),
            ItemsSource = new[] { "H.264", "H.265 (HEVC)" },
            SelectedIndex = _settings.Codec == NvencCodec.H265 ? 1 : 0,
        };
        _codecComboBox.SelectionChanged += (_, _) =>
        {
            _settings.Codec = _codecComboBox.SelectedIndex == 1 ? NvencCodec.H265 : NvencCodec.H264;
        };
        panel.Children.Add(_codecComboBox);

        panel.Children.Add(new TextBlock
        {
            Text = "ビットレート方式",
            Margin = new Thickness(0, 0, 0, 4),
        });

        _rateControlComboBox = new ComboBox
        {
            Margin = new Thickness(0, 0, 0, 12),
            ItemsSource = new[] { "固定 (CBR)", "可変 (VBR)", "自動 (YouTube 推奨)" },
            SelectedIndex = _settings.RateControl switch
            {
                NvencRateControl.Variable => 1,
                NvencRateControl.YouTubeRecommended => 2,
                _ => 0,
            },
        };
        panel.Children.Add(_rateControlComboBox);

        panel.Children.Add(new TextBlock
        {
            Text = "出力品質",
            Margin = new Thickness(0, 0, 0, 4),
        });

        _qualityComboBox = new ComboBox
        {
            Margin = new Thickness(0, 0, 0, 12),
            ItemsSource = new[] { "高速", "標準", "高品質" },
            SelectedIndex = (int)_settings.Quality,
        };
        _qualityComboBox.SelectionChanged += (_, _) =>
        {
            _settings.Quality = (NvencQuality)Math.Clamp(_qualityComboBox.SelectedIndex, 0, 2);
        };
        panel.Children.Add(_qualityComboBox);

        _fastPresetCheckBox = new CheckBox
        {
            Content = "高速書き出し",
            IsChecked = _settings.FastPreset,
            Margin = new Thickness(0, 0, 0, 12),
        };
        _fastPresetCheckBox.Checked += (_, _) => _settings.FastPreset = true;
        _fastPresetCheckBox.Unchecked += (_, _) => _settings.FastPreset = false;
        panel.Children.Add(_fastPresetCheckBox);

        panel.Children.Add(new TextBlock
        {
            Text = "ビットレート（kbps）",
            Margin = new Thickness(0, 0, 0, 4),
        });

        _bitrateTextBox = new TextBox
        {
            Text = _settings.BitrateKbps.ToString(),
            Margin = new Thickness(0, 0, 0, 8),
            IsEnabled = _settings.RateControl != NvencRateControl.YouTubeRecommended,
        };
        _rateControlComboBox.SelectionChanged += (_, _) =>
        {
            _settings.RateControl = _rateControlComboBox.SelectedIndex switch
            {
                1 => NvencRateControl.Variable,
                2 => NvencRateControl.YouTubeRecommended,
                _ => NvencRateControl.Fixed,
            };
            if (_settings.RateControl == NvencRateControl.YouTubeRecommended)
            {
                _bitrateTextBox.IsEnabled = false;
                return;
            }

            _bitrateTextBox.IsEnabled = true;
            _settings.BitrateKbps = 12000;
            _bitrateTextBox.Text = _settings.BitrateKbps.ToString();
        };
        _bitrateTextBox.TextChanged += (_, _) =>
        {
            if (int.TryParse(_bitrateTextBox.Text, out var value))
            {
                _settings.BitrateKbps = Math.Clamp(value, 100, 200000);
            }
        };
        panel.Children.Add(_bitrateTextBox);

        Content = panel;
    }
}
