using System.Runtime.InteropServices;
using Vortice.Direct2D1;
using Vortice.Direct3D11;
using Vortice.DXGI;
using YukkuriMovieMaker.Plugin.FileWriter;
using YukkuriMovieMaker.Project;

namespace NVEncVideoWriterPlugin;

internal sealed class NvencVideoFileWriter : IVideoFileWriter2, IDisposable
{
    private readonly string _outputPath;
    private readonly VideoInfo _videoInfo;
    private readonly NvencSettings _settings;
    private IntPtr _encoderHandle = IntPtr.Zero;
    private bool _disposed;

    public NvencVideoFileWriter(string outputPath, VideoInfo videoInfo, NvencSettings settings)
    {
        _outputPath = outputPath;
        _videoInfo = videoInfo;
        _settings = settings;
    }

    public VideoFileWriterSupportedStreams SupportedStreams => VideoFileWriterSupportedStreams.Video;

    public void WriteAudio(float[] samples)
    {
        // Audio is intentionally ignored for now.
    }

    public void WriteVideo(byte[] frame)
    {
        // Not used in IVideoFileWriter2 mode.
    }

    public void WriteVideo(ID2D1Bitmap1 frame)
    {
        EnsureNotDisposed();

        if (_videoInfo.HasErrors || _videoInfo.Width <= 0 || _videoInfo.Height <= 0)
        {
            return;
        }

        using var surface = frame.Surface;
        using var texture = surface.QueryInterface<ID3D11Texture2D>();
        if (texture is null)
        {
            throw new InvalidOperationException("D3D11 テクスチャを取得できませんでした。");
        }

        if (_encoderHandle == IntPtr.Zero)
        {
            InitializeEncoder(texture);
        }

        var result = NvencNativeMethods.NvencEncode(_encoderHandle, texture.NativePointer);
        if (result == 0)
        {
            throw new InvalidOperationException(GetNativeError());
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;

        if (_encoderHandle != IntPtr.Zero)
        {
            NvencNativeMethods.NvencFinalize(_encoderHandle);
            NvencNativeMethods.NvencDestroy(_encoderHandle);
            _encoderHandle = IntPtr.Zero;
        }
    }

    private void InitializeEncoder(ID3D11Texture2D texture)
    {
        var device = texture.Device;
        if (device is null)
        {
            throw new InvalidOperationException("D3D11 デバイスを取得できませんでした。");
        }

        if ((_videoInfo.Width & 1) != 0 || (_videoInfo.Height & 1) != 0)
        {
            throw new InvalidOperationException("NVENC は偶数サイズの解像度が必要です。");
        }

        var fps = Math.Max(1, _videoInfo.FPS);
        var bitrate = Math.Clamp(_settings.BitrateKbps, 100, 200000);
        var codec = _settings.Codec == NvencCodec.H265 ? 1 : 0;
        var bufferFormat = ResolveBufferFormat(texture);

        _encoderHandle = NvencNativeMethods.NvencCreate(
            device.NativePointer,
            _videoInfo.Width,
            _videoInfo.Height,
            fps,
            bitrate,
            codec,
            bufferFormat,
            _outputPath);

        if (_encoderHandle == IntPtr.Zero)
        {
            throw new InvalidOperationException("NVENC 初期化に失敗しました。");
        }

        var error = GetNativeError();
        if (!string.IsNullOrWhiteSpace(error))
        {
            NvencNativeMethods.NvencDestroy(_encoderHandle);
            _encoderHandle = IntPtr.Zero;
            throw new InvalidOperationException(error);
        }
    }

    private string GetNativeError()
    {
        if (_encoderHandle == IntPtr.Zero)
        {
            return string.Empty;
        }
        var ptr = NvencNativeMethods.NvencGetLastError(_encoderHandle);
        return ptr == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUni(ptr) ?? string.Empty;
    }

    private static int ResolveBufferFormat(ID3D11Texture2D texture)
    {
        var format = texture.Description.Format;
        return format switch
        {
            Format.B8G8R8A8_UNorm => NvencBufferFormat.ARGB,
            Format.B8G8R8A8_UNorm_SRgb => NvencBufferFormat.ARGB,
            Format.R8G8B8A8_UNorm => NvencBufferFormat.ABGR,
            Format.R8G8B8A8_UNorm_SRgb => NvencBufferFormat.ABGR,
            _ => NvencBufferFormat.ARGB,
        };
    }

    private void EnsureNotDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(NvencVideoFileWriter));
        }
    }

    private static class NvencBufferFormat
    {
        public const int ARGB = 0x01000000;
        public const int ABGR = 0x10000000;
    }

    
}
