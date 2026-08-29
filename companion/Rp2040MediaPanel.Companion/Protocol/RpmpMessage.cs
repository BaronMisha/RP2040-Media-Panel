namespace Rp2040MediaPanel.Companion.Protocol;

internal sealed record RpmpMessage(
    int ProtocolVersion,
    string Type,
    ushort Sequence,
    string[] Fields);

internal sealed record DeviceHello(
    ushort Sequence,
    int ProtocolVersion,
    string Board,
    string DisplayController,
    int Width,
    int Height,
    IReadOnlyList<string> Capabilities)
{
    public bool SupportsRawCover =>
        Capabilities.Contains("RAW_COVER", StringComparer.Ordinal);
}

internal sealed record DeviceStatus(
    ushort Sequence,
    string Mode,
    uint UptimeMs,
    byte Track,
    byte TrackCount,
    uint PositionMs,
    uint DurationMs,
    byte Volume,
    bool Playing);