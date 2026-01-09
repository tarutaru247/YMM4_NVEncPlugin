using System.Windows;
using System.Windows.Controls;

namespace NVEncVideoWriterPlugin;

internal sealed class NvencConfigView : UserControl
{
    private readonly ComboBox _codecComboBox;
    private readonly TextBox _bitrateTextBox;
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
            Text = "ビットレート（kbps）",
            Margin = new Thickness(0, 0, 0, 4),
        });

        _bitrateTextBox = new TextBox
        {
            Text = _settings.BitrateKbps.ToString(),
            Margin = new Thickness(0, 0, 0, 8),
        };
        _bitrateTextBox.TextChanged += (_, _) =>
        {
            if (int.TryParse(_bitrateTextBox.Text, out var value))
            {
                _settings.BitrateKbps = Math.Clamp(value, 100, 200000);
            }
        };
        panel.Children.Add(_bitrateTextBox);

        panel.Children.Add(new TextBlock
        {
            Text = "※ 現時点は映像のみ出力（音声は無視）",
            Opacity = 0.7,
        });

        Content = panel;
    }
}
