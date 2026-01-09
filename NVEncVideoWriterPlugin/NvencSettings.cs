namespace NVEncVideoWriterPlugin;

internal sealed class NvencSettings
{
    public NvencCodec Codec { get; set; } = NvencCodec.H264;
    public int BitrateKbps { get; set; } = 8000;
    public string? NvencPath { get; set; }
}

internal enum NvencCodec
{
    H264,
    H265,
}
