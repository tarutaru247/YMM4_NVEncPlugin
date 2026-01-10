using System.Runtime.InteropServices;

namespace NVEncVideoWriterPlugin;

internal static class NvencNativeMethods
{
    [DllImport("NvencNative.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr NvencCreate(
        IntPtr device,
        int width,
        int height,
        int fps,
        int bitrateKbps,
        int codec,
        int quality,
        int fastPreset,
        int rateControlMode,
        int maxBitrateKbps,
        int bufferFormat,
        int hevcAsync,
        int enableDebugLog,
        string outputPath);

    [DllImport("NvencNative.dll")]
    public static extern int NvencEncode(IntPtr handle, IntPtr texture);

    [DllImport("NvencNative.dll")]
    public static extern int NvencWriteAudio(IntPtr handle, float[] samples, int sampleCount, int sampleRate, int channels);

    [DllImport("NvencNative.dll")]
    public static extern int NvencFinalize(IntPtr handle);

    [DllImport("NvencNative.dll")]
    public static extern void NvencDestroy(IntPtr handle);

    [DllImport("NvencNative.dll")]
    public static extern IntPtr NvencGetLastError(IntPtr handle);
}
