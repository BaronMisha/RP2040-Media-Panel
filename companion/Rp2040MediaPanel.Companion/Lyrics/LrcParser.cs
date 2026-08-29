using System.Globalization;
using System.Text.RegularExpressions;

namespace Rp2040MediaPanel.Companion.Lyrics;

internal static partial class LrcParser
{
    private static readonly Regex TimestampPattern = new(
        @"\[(?<minutes>\d{1,4}):(?<seconds>\d{1,2}(?:[\.,]\d{1,3})?)\]",
        RegexOptions.CultureInvariant);
    private static readonly Regex OffsetPattern = new(
        @"\[offset:\s*(?<offset>[+-]?\d+)\s*\]",
        RegexOptions.CultureInvariant |
        RegexOptions.IgnoreCase);
    private static readonly Regex EnhancedTimestampPattern = new(
        @"<\d{1,4}:\d{1,2}(?:[\.,]\d{1,3})?>",
        RegexOptions.CultureInvariant);

    public static SyncedLyricsTrack? Parse(string? lrc)
    {
        if (string.IsNullOrWhiteSpace(lrc))
        {
            return null;
        }

        var offsetMs = 0L;
        var offsetMatch = OffsetPattern.Match(lrc);
        if (offsetMatch.Success)
        {
            long.TryParse(
                offsetMatch.Groups["offset"].Value,
                NumberStyles.AllowLeadingSign,
                CultureInfo.InvariantCulture,
                out offsetMs);
        }

        var rawLines = new List<(long StartMs, string Text)>();
        foreach (var sourceLine in lrc.Split('\n'))
        {
            var line = sourceLine.TrimEnd('\r');
            var matches = TimestampPattern.Matches(line);
            if (matches.Count == 0)
            {
                continue;
            }

            var lastMatch = matches[^1];
            var text = EnhancedTimestampPattern.Replace(
                line[(lastMatch.Index + lastMatch.Length)..],
                string.Empty).Trim();
            if (text.Length == 0)
            {
                continue;
            }

            foreach (Match match in matches)
            {
                if (!TryParseTimestamp(match, out var startMs))
                {
                    continue;
                }
                rawLines.Add((startMs + offsetMs, text));
            }
        }

        if (rawLines.Count == 0)
        {
            return null;
        }

        var uniqueLines = new Dictionary<uint, string>();
        foreach (var rawLine in rawLines)
        {
            var clamped = Math.Clamp(
                rawLine.StartMs,
                0L,
                uint.MaxValue);
            uniqueLines[(uint)clamped] = rawLine.Text;
        }

        var lines = uniqueLines
            .OrderBy(pair => pair.Key)
            .Select(pair => new SyncedLyricsLine(
                pair.Key,
                pair.Value))
            .ToArray();
        return lines.Length == 0
            ? null
            : new SyncedLyricsTrack(lines);
    }

    private static bool TryParseTimestamp(
        Match match,
        out long milliseconds)
    {
        milliseconds = 0;
        if (!long.TryParse(
                match.Groups["minutes"].Value,
                NumberStyles.None,
                CultureInfo.InvariantCulture,
                out var minutes) ||
            !decimal.TryParse(
                match.Groups["seconds"].Value.Replace(',', '.'),
                NumberStyles.AllowDecimalPoint,
                CultureInfo.InvariantCulture,
                out var seconds) ||
            seconds < 0 ||
            seconds >= 60)
        {
            return false;
        }

        var total =
            minutes * 60_000M + seconds * 1_000M;
        if (total > long.MaxValue)
        {
            return false;
        }

        milliseconds = decimal.ToInt64(
            decimal.Round(
                total,
                0,
                MidpointRounding.AwayFromZero));
        return true;
    }
}
