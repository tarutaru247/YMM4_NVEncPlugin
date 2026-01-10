namespace NVEncVideoWriterPlugin;

internal sealed class NvencSettings
{
    public NvencCodec Codec { get; set; } = NvencCodec.H264;
    public int BitrateKbps { get; set; } = 12000;
    public NvencQuality Quality { get; set; } = NvencQuality.Balanced;
    public NvencRateControl RateControl { get; set; } = NvencRateControl.YouTubeRecommended;
}

internal enum NvencCodec
{
    H264,
    H265,
}

internal enum NvencQuality
{
    Speed,
    Balanced,
    Quality,
}

internal enum NvencRateControl
{
    Fixed,
    Variable,
    YouTubeRecommended,
}
