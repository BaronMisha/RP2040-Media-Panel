namespace Rp2040MediaPanel.Companion.Lyrics;

internal sealed record SyncedLyricsLine(
    uint StartMs,
    string Text);

internal sealed record KaraokeFrame(
    uint StartMs,
    uint EndMs,
    string PreviousLine,
    string CurrentLine,
    string NextLine);

internal sealed class SyncedLyricsTrack
{
    public SyncedLyricsTrack(
        IReadOnlyList<SyncedLyricsLine> lines)
    {
        ArgumentNullException.ThrowIfNull(lines);
        Lines = lines;
    }

    public IReadOnlyList<SyncedLyricsLine> Lines { get; }

    public bool TryGetFrame(
        uint positionMs,
        uint durationMs,
        out KaraokeFrame? frame)
    {
        frame = null;
        if (Lines.Count == 0)
        {
            return false;
        }

        var left = 0;
        var right = Lines.Count - 1;
        var currentIndex = 0;
        while (left <= right)
        {
            var middle = left + (right - left) / 2;
            if (Lines[middle].StartMs <= positionMs)
            {
                currentIndex = middle;
                left = middle + 1;
            }
            else
            {
                right = middle - 1;
            }
        }

        var current = Lines[currentIndex];
        var fallbackEnd = current.StartMs <= uint.MaxValue - 4_000U
            ? current.StartMs + 4_000U
            : uint.MaxValue;
        var endMs = currentIndex + 1 < Lines.Count
            ? Lines[currentIndex + 1].StartMs
            : durationMs > current.StartMs
                ? durationMs
                : fallbackEnd;
        if (endMs <= current.StartMs)
        {
            endMs = fallbackEnd;
        }

        frame = new KaraokeFrame(
            current.StartMs,
            endMs,
            currentIndex > 0
                ? Lines[currentIndex - 1].Text
                : string.Empty,
            current.Text,
            currentIndex + 1 < Lines.Count
                ? Lines[currentIndex + 1].Text
                : string.Empty);
        return true;
    }
}
