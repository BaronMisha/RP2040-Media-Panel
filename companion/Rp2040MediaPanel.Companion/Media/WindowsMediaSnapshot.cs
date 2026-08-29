namespace Rp2040MediaPanel.Companion.Media;

internal sealed record WindowsMediaSnapshot(
    uint MediaId,
    string SourceAppId,
    string Title,
    string Artist,
    string Album,
    uint PositionMs,
    uint DurationMs,
    bool Playing,
    byte Volume,
    byte[]? CoverRgb565,
    uint? CoverCrc32);

internal enum MediaSessionPollStatus
{
    Available,
    Absent,
    TransientFailure,
}

internal sealed record MediaSessionPollResult(
    MediaSessionPollStatus Status,
    WindowsMediaSnapshot? Snapshot,
    string? Error)
{
    public static MediaSessionPollResult Available(
        WindowsMediaSnapshot snapshot) =>
        new(MediaSessionPollStatus.Available, snapshot, null);

    public static MediaSessionPollResult Absent() =>
        new(MediaSessionPollStatus.Absent, null, null);

    public static MediaSessionPollResult TransientFailure(
        Exception exception) =>
        new(
            MediaSessionPollStatus.TransientFailure,
            null,
            exception.Message);
}
