using System.Globalization;
using System.Text;

namespace Rp2040MediaPanel.Companion.Protocol;

internal static class RpmpProtocol
{
    internal const int MaximumLineLength = 1400;
    internal const int CoverChunkBytes = 960;

    private const string PrefixV1 = "@RPMP1";
    private const string PrefixV2 = "@RPMP2";
    private const int MaxTitleBytes = 95;
    private const int MaxArtistBytes = 63;
    private const int MaxAlbumBytes = 63;
    private const int MaxKaraokePreviousBytes = 95;
    private const int MaxKaraokeCurrentBytes = 127;
    private const int MaxKaraokeNextBytes = 95;

    public static string CreateRequest(
        string type,
        ushort sequence,
        params string[] arguments) =>
        CreateVersionedRequest(1, type, sequence, arguments);

    public static string CreateVersionedRequest(
        int protocolVersion,
        string type,
        ushort sequence,
        params string[] arguments)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(type);
        var prefix = protocolVersion switch
        {
            1 => PrefixV1,
            2 => PrefixV2,
            _ => throw new ArgumentOutOfRangeException(
                nameof(protocolVersion)),
        };
        var header = $"{prefix}|{type}|{sequence}";
        var request = arguments.Length == 0
            ? header
            : $"{header}|{string.Join("|", arguments)}";
        if (Encoding.UTF8.GetByteCount(request) > MaximumLineLength)
        {
            throw new ArgumentException(
                $"Command exceeds {MaximumLineLength} bytes.");
        }
        return request;
    }

    public static string CreateMediaRequest(
        ushort sequence,
        uint mediaId,
        uint durationMs,
        string title,
        string artist,
        string album,
        int protocolVersion = 1) =>
        CreateVersionedRequest(
            protocolVersion,
            "MEDIA",
            sequence,
            mediaId.ToString(CultureInfo.InvariantCulture),
            durationMs.ToString(CultureInfo.InvariantCulture),
            EncodeText(title, MaxTitleBytes, "title"),
            EncodeText(artist, MaxArtistBytes, "artist"),
            EncodeText(album, MaxAlbumBytes, "album"));

    public static string CreateStateRequest(
        ushort sequence,
        uint positionMs,
        uint durationMs,
        byte volume,
        bool playing,
        int protocolVersion = 1)
    {
        if (volume > 100 && volume != byte.MaxValue)
        {
            throw new ArgumentOutOfRangeException(nameof(volume));
        }
        return CreateVersionedRequest(
            protocolVersion,
            "STATE",
            sequence,
            positionMs.ToString(CultureInfo.InvariantCulture),
            durationMs.ToString(CultureInfo.InvariantCulture),
            volume.ToString(CultureInfo.InvariantCulture),
            playing ? "1" : "0");
    }

    public static string CreateReleaseRequest(
        ushort sequence,
        int protocolVersion = 1) =>
        CreateVersionedRequest(protocolVersion, "RELEASE", sequence);

    public static string CreateCoverBeginRequest(
        ushort sequence,
        uint mediaId,
        ushort width,
        ushort height,
        uint byteCount,
        uint crc32,
        int protocolVersion = 1) =>
        CreateVersionedRequest(
            protocolVersion,
            "COVER_BEGIN",
            sequence,
            mediaId.ToString(CultureInfo.InvariantCulture),
            width.ToString(CultureInfo.InvariantCulture),
            height.ToString(CultureInfo.InvariantCulture),
            byteCount.ToString(CultureInfo.InvariantCulture),
            crc32.ToString(CultureInfo.InvariantCulture));

    public static string CreateCoverStreamRequest(
        ushort sequence,
        uint mediaId,
        ushort width,
        ushort height,
        uint byteCount,
        uint crc32) =>
        CreateVersionedRequest(
            2,
            "COVER_STREAM",
            sequence,
            mediaId.ToString(CultureInfo.InvariantCulture),
            width.ToString(CultureInfo.InvariantCulture),
            height.ToString(CultureInfo.InvariantCulture),
            byteCount.ToString(CultureInfo.InvariantCulture),
            crc32.ToString(CultureInfo.InvariantCulture));

    public static string CreateCoverDataRequest(
        ushort sequence,
        uint mediaId,
        uint offset,
        ReadOnlySpan<byte> data,
        int protocolVersion = 1) =>
        CreateVersionedRequest(
            protocolVersion,
            "COVER_DATA",
            sequence,
            mediaId.ToString(CultureInfo.InvariantCulture),
            offset.ToString(CultureInfo.InvariantCulture),
            Convert.ToBase64String(data));

    public static string CreateCoverEndRequest(
        ushort sequence,
        uint mediaId,
        int protocolVersion = 1) =>
        CreateVersionedRequest(
            protocolVersion,
            "COVER_END",
            sequence,
            mediaId.ToString(CultureInfo.InvariantCulture));

    public static string CreateKaraokeRequest(
        ushort sequence,
        uint mediaId,
        uint lineStartMs,
        uint lineEndMs,
        string previousLine,
        string currentLine,
        string nextLine,
        int protocolVersion = 1)
    {
        if (lineEndMs <= lineStartMs)
        {
            throw new ArgumentOutOfRangeException(nameof(lineEndMs));
        }
        if (string.IsNullOrWhiteSpace(currentLine))
        {
            throw new ArgumentException(
                "Current karaoke line cannot be empty.",
                nameof(currentLine));
        }
        return CreateVersionedRequest(
            protocolVersion,
            "KARAOKE",
            sequence,
            mediaId.ToString(CultureInfo.InvariantCulture),
            lineStartMs.ToString(CultureInfo.InvariantCulture),
            lineEndMs.ToString(CultureInfo.InvariantCulture),
            EncodeText(previousLine, MaxKaraokePreviousBytes, "previous_line"),
            EncodeText(currentLine, MaxKaraokeCurrentBytes, "current_line"),
            EncodeText(nextLine, MaxKaraokeNextBytes, "next_line"));
    }

    public static string CreateKaraokeClearRequest(
        ushort sequence,
        uint mediaId,
        int protocolVersion = 1) =>
        CreateVersionedRequest(
            protocolVersion,
            "KARAOKE_CLEAR",
            sequence,
            mediaId.ToString(CultureInfo.InvariantCulture));

    public static string FitTitle(string value) =>
        FitText(value, MaxTitleBytes);

    public static string FitArtist(string value) =>
        FitText(value, MaxArtistBytes);

    public static string FitAlbum(string value) =>
        FitText(value, MaxAlbumBytes);

    public static string FitKaraokePrevious(string value) =>
        FitText(value, MaxKaraokePreviousBytes);

    public static string FitKaraokeCurrent(string value) =>
        FitText(value, MaxKaraokeCurrentBytes);

    public static string FitKaraokeNext(string value) =>
        FitText(value, MaxKaraokeNextBytes);
    public static bool TryParse(string line, out RpmpMessage? message)
    {
        message = null;
        var protocolVersion = line.StartsWith(
            PrefixV1 + "|",
            StringComparison.Ordinal)
            ? 1
            : line.StartsWith(
                PrefixV2 + "|",
                StringComparison.Ordinal)
                ? 2
                : 0;
        if (protocolVersion == 0)
        {
            return false;
        }

        var fields = line.Split('|');
        if (fields.Length < 3 ||
            !ushort.TryParse(
                fields[2],
                NumberStyles.None,
                CultureInfo.InvariantCulture,
                out var sequence))
        {
            return false;
        }

        message = new RpmpMessage(
            protocolVersion,
            fields[1],
            sequence,
            fields);
        return true;
    }

    public static DeviceHello ParseHello(RpmpMessage message)
    {
        var expectedFields = message.ProtocolVersion == 2 ? 9 : 8;
        if (message.Type != "HELLO" ||
            message.Fields.Length != expectedFields)
        {
            throw new ProtocolException("Invalid HELLO response.");
        }

        var advertisedVersion =
            ParseInt(message.Fields[3], "protocol_version");
        if (advertisedVersion != message.ProtocolVersion)
        {
            throw new ProtocolException("HELLO version mismatch.");
        }

        return new DeviceHello(
            message.Sequence,
            advertisedVersion,
            message.Fields[4],
            message.Fields[5],
            ParseInt(message.Fields[6], "width"),
            ParseInt(message.Fields[7], "height"),
            message.ProtocolVersion == 2
                ? message.Fields[8].Split(
                    ',',
                    StringSplitOptions.RemoveEmptyEntries |
                    StringSplitOptions.TrimEntries)
                : []);
    }
    public static void ValidatePong(RpmpMessage message)
    {
        if (message.Type != "ACK" ||
            message.Fields.Length != 4 ||
            message.Fields[3] != "PONG")
        {
            throw new ProtocolException("Некорректный ответ PING.");
        }
    }

    public static void ValidateAck(
        RpmpMessage message,
        string expectedCommand)
    {
        if (message.Type != "ACK" ||
            message.Fields.Length != 4 ||
            message.Fields[3] != expectedCommand)
        {
            throw new ProtocolException(
                $"Некорректный ACK для {expectedCommand}.");
        }
    }

    public static DeviceStatus ParseStatus(RpmpMessage message)
    {
        if (message.Type != "STATUS" || message.Fields.Length != 11)
        {
            throw new ProtocolException("Некорректный ответ STATUS.");
        }

        var playingValue = ParseByte(message.Fields[10], "playing");
        if (playingValue > 1)
        {
            throw new ProtocolException("Поле playing должно быть 0 или 1.");
        }

        return new DeviceStatus(
            message.Sequence,
            message.Fields[3],
            ParseUInt(message.Fields[4], "uptime_ms"),
            ParseByte(message.Fields[5], "track"),
            ParseByte(message.Fields[6], "track_count"),
            ParseUInt(message.Fields[7], "position_ms"),
            ParseUInt(message.Fields[8], "duration_ms"),
            ParseByte(message.Fields[9], "volume"),
            playingValue == 1);
    }

    public static void ThrowIfError(RpmpMessage message)
    {
        if (message.Type != "ERR")
        {
            return;
        }

        var code = message.Fields.Length >= 4
            ? message.Fields[3]
            : "UNKNOWN_ERROR";
        throw new ProtocolException(
            $"Устройство вернуло ошибку протокола: {code}.");
    }

    private static string EncodeText(
        string value,
        int maxBytes,
        string field)
    {
        ArgumentNullException.ThrowIfNull(value);
        var bytes = System.Text.Encoding.UTF8.GetBytes(value);
        if (bytes.Length > maxBytes)
        {
            throw new ArgumentException(
                $"Поле {field} превышает лимит {maxBytes} байт UTF-8.",
                field);
        }

        return Convert.ToBase64String(bytes);
    }

    private static string FitText(string value, int maxBytes)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (Encoding.UTF8.GetByteCount(value) <= maxBytes)
        {
            return value;
        }

        var builder = new StringBuilder(value.Length);
        var usedBytes = 0;
        foreach (var rune in value.EnumerateRunes())
        {
            if (usedBytes + rune.Utf8SequenceLength > maxBytes)
            {
                break;
            }

            builder.Append(rune);
            usedBytes += rune.Utf8SequenceLength;
        }

        return builder.ToString();
    }

    private static int ParseInt(string value, string field)
    {
        if (int.TryParse(
                value,
                NumberStyles.None,
                CultureInfo.InvariantCulture,
                out var result))
        {
            return result;
        }

        throw new ProtocolException(
            $"Поле {field} не является числом.");
    }

    private static uint ParseUInt(string value, string field)
    {
        if (uint.TryParse(
                value,
                NumberStyles.None,
                CultureInfo.InvariantCulture,
                out var result))
        {
            return result;
        }

        throw new ProtocolException(
            $"Поле {field} не является uint32.");
    }

    private static byte ParseByte(string value, string field)
    {
        if (byte.TryParse(
                value,
                NumberStyles.None,
                CultureInfo.InvariantCulture,
                out var result))
        {
            return result;
        }

        throw new ProtocolException(
            $"Поле {field} не является uint8.");
    }
}

internal sealed class ProtocolException : Exception
{
    public ProtocolException(string message)
        : base(message)
    {
    }
}
