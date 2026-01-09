using System.Diagnostics;
using System.Text;
using System.IO;
using YukkuriMovieMaker.Plugin.FileWriter;
using YukkuriMovieMaker.Project;

namespace NVEncVideoWriterPlugin;

internal sealed class NvencVideoFileWriter : IVideoFileWriter, IDisposable
{
    private readonly string _outputPath;
    private readonly VideoInfo _videoInfo;
    private readonly NvencSettings _settings;
    private readonly string _rawPath;
    private readonly FileStream _rawStream;
    private readonly int _width;
    private readonly int _height;
    private readonly int _fps;
    private byte[]? _nv12Buffer;
    private bool _disposed;

    public NvencVideoFileWriter(string outputPath, VideoInfo videoInfo, NvencSettings settings)
    {
        _outputPath = outputPath;
        _videoInfo = videoInfo;
        _settings = settings;
        _width = _videoInfo.Width;
        _height = _videoInfo.Height;
        _fps = Math.Max(1, _videoInfo.FPS);

        _rawPath = Path.ChangeExtension(_outputPath, ".nv12");
        _rawStream = new FileStream(_rawPath, FileMode.Create, FileAccess.Write, FileShare.Read);
    }

    public VideoFileWriterSupportedStreams SupportedStreams => VideoFileWriterSupportedStreams.Video;

    public void WriteAudio(float[] samples)
    {
        // Audio is intentionally ignored in this minimal implementation.
    }

    public void WriteVideo(byte[] frame)
    {
        EnsureNotDisposed();

        if (_width <= 0 || _height <= 0)
        {
            return;
        }

        var expectedBytes = _width * _height * 4;
        if (frame.Length < expectedBytes)
        {
            throw new InvalidOperationException($"Unexpected frame size. expected={expectedBytes}, actual={frame.Length}");
        }

        _nv12Buffer ??= new byte[_width * _height + (_width * _height) / 2];
        ConvertBgraToNv12(frame, _nv12Buffer, _width, _height);
        _rawStream.Write(_nv12Buffer, 0, _nv12Buffer.Length);
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _rawStream.Flush();
        _rawStream.Dispose();

        RunNvenc();
    }

    private void RunNvenc()
    {
        var nvencExe = ResolveNvencPath();
        if (string.IsNullOrWhiteSpace(nvencExe))
        {
            throw new FileNotFoundException("NVEncC が見つかりません。NVEncC.exe をプラグインと同じフォルダに置くか、GUIでパスを指定してください。");
        }

        var codec = _settings.Codec == NvencCodec.H265 ? "hevc" : "h264";
        var bitrate = Math.Clamp(_settings.BitrateKbps, 100, 200000);

        var args = new StringBuilder();
        args.Append("--raw ");
        args.Append("--input-res ").Append(_width).Append('x').Append(_height).Append(' ');
        args.Append("--fps ").Append(_fps).Append(' ');
        args.Append("--input-csp nv12 ");
        args.Append("-c ").Append(codec).Append(' ');
        args.Append("--cbr ").Append(bitrate).Append(' ');
        args.Append("-i ").Append('"').Append(_rawPath).Append('"').Append(' ');
        args.Append("-o ").Append('"').Append(_outputPath).Append('"');

        var startInfo = new ProcessStartInfo
        {
            FileName = nvencExe,
            Arguments = args.ToString(),
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };

        using var process = Process.Start(startInfo);
        if (process is null)
        {
            throw new InvalidOperationException("NVEncC の起動に失敗しました。");
        }

        process.WaitForExit();
        if (process.ExitCode != 0)
        {
            var error = process.StandardError.ReadToEnd();
            throw new InvalidOperationException($"NVEncC でエラーが発生しました。{Environment.NewLine}{error}");
        }

        TryDeleteRaw();
    }

    private string? ResolveNvencPath()
    {
        if (!string.IsNullOrWhiteSpace(_settings.NvencPath) && File.Exists(_settings.NvencPath))
        {
            return _settings.NvencPath;
        }

        var baseDir = AppContext.BaseDirectory;
        var localCandidates = new[]
        {
            Path.Combine(baseDir, "NVEncC64.exe"),
            Path.Combine(baseDir, "NVEncC.exe"),
        };
        foreach (var candidate in localCandidates)
        {
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return FindOnPath("NVEncC64.exe") ?? FindOnPath("NVEncC.exe");
    }

    private static string? FindOnPath(string exeName)
    {
        var pathEnv = Environment.GetEnvironmentVariable("PATH");
        if (string.IsNullOrWhiteSpace(pathEnv))
        {
            return null;
        }

        foreach (var dir in pathEnv.Split(Path.PathSeparator))
        {
            if (string.IsNullOrWhiteSpace(dir))
            {
                continue;
            }
            var candidate = Path.Combine(dir.Trim(), exeName);
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }
        return null;
    }

    private static void ConvertBgraToNv12(byte[] bgra, byte[] nv12, int width, int height)
    {
        var yPlaneSize = width * height;
        var uvStart = yPlaneSize;

        for (var y = 0; y < height; y += 2)
        {
            for (var x = 0; x < width; x += 2)
            {
                int sumR = 0;
                int sumG = 0;
                int sumB = 0;

                for (var dy = 0; dy < 2; dy++)
                {
                    var yy = y + dy;
                    if (yy >= height)
                    {
                        continue;
                    }

                    for (var dx = 0; dx < 2; dx++)
                    {
                        var xx = x + dx;
                        if (xx >= width)
                        {
                            continue;
                        }

                        var srcIndex = (yy * width + xx) * 4;
                        var b = bgra[srcIndex + 0];
                        var g = bgra[srcIndex + 1];
                        var r = bgra[srcIndex + 2];

                        var yValue = (byte)((77 * r + 150 * g + 29 * b) >> 8);
                        nv12[yy * width + xx] = yValue;

                        sumR += r;
                        sumG += g;
                        sumB += b;
                    }
                }

                var pixels = Math.Min(2, width - x) * Math.Min(2, height - y);
                if (pixels <= 0)
                {
                    continue;
                }

                var avgR = sumR / pixels;
                var avgG = sumG / pixels;
                var avgB = sumB / pixels;

                var u = (byte)Math.Clamp((( -43 * avgR - 85 * avgG + 128 * avgB) >> 8) + 128, 0, 255);
                var v = (byte)Math.Clamp((( 128 * avgR - 107 * avgG - 21 * avgB) >> 8) + 128, 0, 255);

                var uvIndex = uvStart + (y / 2) * width + x;
                if (uvIndex + 1 < nv12.Length)
                {
                    nv12[uvIndex] = u;
                    nv12[uvIndex + 1] = v;
                }
            }
        }
    }

    private void EnsureNotDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(NvencVideoFileWriter));
        }
    }

    private void TryDeleteRaw()
    {
        try
        {
            if (File.Exists(_rawPath))
            {
                File.Delete(_rawPath);
            }
        }
        catch
        {
            // Ignore cleanup errors.
        }
    }
}
