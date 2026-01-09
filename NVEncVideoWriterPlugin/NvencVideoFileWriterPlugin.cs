using YukkuriMovieMaker.Commons;
using YukkuriMovieMaker.Plugin;
using YukkuriMovieMaker.Plugin.FileWriter;
using YukkuriMovieMaker.Project;
using YukkuriMovieMaker.Plugin.Update;

namespace NVEncVideoWriterPlugin;

public sealed class NvencVideoFileWriterPlugin : IVideoFileWriterPlugin
{
    private readonly NvencSettings _settings = new();
    private readonly PluginDetailsAttribute _details = new()
    {
        AuthorName = "NVEncC GUI Plugin",
        ContentId = "NVEncVideoFileWriterPlugin",
    };

    public string Name => "NVENC プラグイン出力";

    public PluginDetailsAttribute Details => _details;

    public IPluginUpdater? Updater => null;

    public VideoFileWriterOutputPath OutputPathMode => VideoFileWriterOutputPath.File;

    public IVideoFileWriter CreateVideoFileWriter(string path, VideoInfo videoInfo)
    {
        var snapshot = new NvencSettings
        {
            Codec = _settings.Codec,
            BitrateKbps = _settings.BitrateKbps,
            Quality = _settings.Quality,
            RateControl = _settings.RateControl,
        };
        return new NvencVideoFileWriter(path, videoInfo, snapshot);
    }

    public string GetFileExtention()
    {
        return ".mp4";
    }

    public System.Windows.UIElement GetVideoConfigView(string projectName, VideoInfo videoInfo, int length)
    {
        return new NvencConfigView(_settings);
    }

    public bool NeedDownloadResources()
    {
        return false;
    }

    public Task DownloadResources(ProgressMessage progress, CancellationToken token)
    {
        return Task.CompletedTask;
    }
}
