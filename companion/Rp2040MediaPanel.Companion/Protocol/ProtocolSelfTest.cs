using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using Rp2040MediaPanel.Companion.Configuration;
using Rp2040MediaPanel.Companion.Lyrics;
using Rp2040MediaPanel.Companion.Media;
using Rp2040MediaPanel.Companion.Runtime;

namespace Rp2040MediaPanel.Companion.Protocol;

internal static class ProtocolSelfTest
{
    public static void Run()
    {
        AssertRequest();
        AssertHello();
        AssertHelloV2();
        AssertPong();
        AssertMediaRequest();
        AssertStateRequest();
        AssertUnknownVolume();
        AssertReleaseRequest();
        AssertCoverRequests();
        AssertKaraokeRequests();
        AssertLrcParser();
        AssertLrcLibJson();
        AssertLrcLibRetryPolicy();
        AssertKaraokeFrames();
        AssertCrc32();
        AssertSystemVolumeMapping();
        AssertCommandAck();
        AssertStatus();
        AssertPcStatus();
        AssertUtf8Fitting();
        AssertTextLimit();
        AssertDiagnosticsIgnored();
        AssertMalformedRejected();
        AssertBackgroundOptions();
        AssertConfiguration();
        AssertAutostartCommand();
        AssertLateCoverDelivery();
        AssertCoverRetry();
        AssertSessionContinuity();
        Console.WriteLine(
            "[SELFTEST] companion protocol/runtime PASS");
    }

    private static void AssertRequest()
    {
        Assert(
            RpmpProtocol.CreateRequest("PING", 42) ==
            "@RPMP1|PING|42",
            "request");
    }

    private static void AssertHello()
    {
        Assert(
            RpmpProtocol.TryParse(
                "@RPMP1|HELLO|1|1|YD-RP2040|ILI9341|240|320",
                out var message),
            "HELLO parse");
        var hello = RpmpProtocol.ParseHello(message!);
        Assert(
            hello.Sequence == 1 &&
            hello.ProtocolVersion == 1 &&
            hello.Width == 240 &&
            hello.Height == 320,
            "HELLO fields");
    }

    private static void AssertHelloV2()
    {
        Assert(
            RpmpProtocol.TryParse(
                "@RPMP2|HELLO|2|2|YD-RP2040|ILI9341|240|320|RAW_COVER",
                out var message),
            "RPMP2 HELLO parse");
        var hello = RpmpProtocol.ParseHello(message!);
        Assert(
            hello.Sequence == 2 &&
            hello.ProtocolVersion == 2 &&
            hello.SupportsRawCover,
            "RPMP2 HELLO capability");
    }
    private static void AssertPong()
    {
        Assert(
            RpmpProtocol.TryParse(
                "@RPMP1|ACK|2|PONG",
                out var message),
            "PONG parse");
        RpmpProtocol.ValidatePong(message!);
    }

    private static void AssertMediaRequest()
    {
        Assert(
            RpmpProtocol.CreateMediaRequest(
                7,
                1,
                60_000,
                "PC Link Test",
                "Windows Companion",
                "RPMP | Transport") ==
            "@RPMP1|MEDIA|7|1|60000|UEMgTGluayBUZXN0|" +
            "V2luZG93cyBDb21wYW5pb24=|UlBNUCB8IFRyYW5zcG9ydA==",
            "MEDIA request");
    }

    private static void AssertStateRequest()
    {
        Assert(
            RpmpProtocol.CreateStateRequest(
                8,
                1250,
                60_000,
                80,
                true) ==
            "@RPMP1|STATE|8|1250|60000|80|1",
            "STATE request");
    }

    private static void AssertUnknownVolume()
    {
        Assert(
            RpmpProtocol.CreateStateRequest(
                8,
                1250,
                60_000,
                byte.MaxValue,
                false) ==
            "@RPMP1|STATE|8|1250|60000|255|0",
            "unknown volume");
    }

    private static void AssertReleaseRequest()
    {
        Assert(
            RpmpProtocol.CreateReleaseRequest(9) ==
            "@RPMP1|RELEASE|9",
            "RELEASE request");
    }

    private static void AssertCoverRequests()
    {
        Assert(
            RpmpProtocol.CreateCoverBeginRequest(
                10,
                99,
                160,
                160,
                51_200,
                3_421_780_262) ==
            "@RPMP1|COVER_BEGIN|10|99|160|160|51200|3421780262",
            "COVER_BEGIN request");
        Assert(
            RpmpProtocol.CreateCoverDataRequest(
                11,
                99,
                0,
                new byte[] { 0, 1, 2 }) ==
            "@RPMP1|COVER_DATA|11|99|0|AAEC",
            "COVER_DATA request");
        var maximumChunkRequest =
            RpmpProtocol.CreateCoverDataRequest(
                11,
                uint.MaxValue,
                51_199,
                new byte[RpmpProtocol.CoverChunkBytes]);
        Assert(
            Encoding.UTF8.GetByteCount(maximumChunkRequest) <=
                RpmpProtocol.MaximumLineLength,
            "maximum COVER_DATA line");
        Assert(
            RpmpProtocol.CreateCoverEndRequest(12, 99) ==
            "@RPMP1|COVER_END|12|99",
            "COVER_END request");
        Assert(
            RpmpProtocol.CreateCoverStreamRequest(
                15,
                99,
                160,
                160,
                51_200,
                3_421_780_262) ==
            "@RPMP2|COVER_STREAM|15|99|160|160|51200|3421780262",
            "RPMP2 COVER_STREAM request");
    }

    private static void AssertKaraokeRequests()
    {
        Assert(
            RpmpProtocol.CreateKaraokeRequest(
                13,
                99,
                1_000,
                5_000,
                "Previous",
                "Current",
                "Next") ==
            "@RPMP1|KARAOKE|13|99|1000|5000|" +
            "UHJldmlvdXM=|Q3VycmVudA==|TmV4dA==",
            "KARAOKE request");
        Assert(
            RpmpProtocol.CreateKaraokeClearRequest(14, 99) ==
            "@RPMP1|KARAOKE_CLEAR|14|99",
            "KARAOKE_CLEAR request");
        AssertThrows<ArgumentOutOfRangeException>(
            () => RpmpProtocol.CreateKaraokeRequest(
                1,
                1,
                5_000,
                5_000,
                string.Empty,
                "Current",
                string.Empty),
            "KARAOKE timing");
    }

    private static void AssertLrcParser()
    {
        const string lrc =
            "[offset:+100]\n" +
            "[00:01.00]First\n" +
            "[00:05.25][00:06.00]Second <00:06.20>\n";
        var track = LrcParser.Parse(lrc);
        Assert(
            track is not null &&
            track.Lines.Count == 3 &&
            track.Lines[0] ==
                new SyncedLyricsLine(1_100, "First") &&
            track.Lines[1] ==
                new SyncedLyricsLine(5_350, "Second") &&
            track.Lines[2] ==
                new SyncedLyricsLine(6_100, "Second"),
            "LRC parser");
    }

    private static void AssertKaraokeFrames()
    {
        var track = new SyncedLyricsTrack(
        [
            new SyncedLyricsLine(1_000, "First"),
            new SyncedLyricsLine(5_000, "Second"),
            new SyncedLyricsLine(9_000, "Third"),
        ]);
        Assert(
            track.TryGetFrame(5_500, 12_000, out var frame) &&
            frame == new KaraokeFrame(
                5_000,
                9_000,
                "First",
                "Second",
                "Third"),
            "karaoke middle frame");
        Assert(
            track.TryGetFrame(10_000, 12_000, out frame) &&
            frame == new KaraokeFrame(
                9_000,
                12_000,
                "Second",
                "Third",
                string.Empty),
            "karaoke final frame");
    }

    private static void AssertLrcLibJson()
    {
        const string json =
            """
            [{
              "trackName": "Test Track",
              "artistName": "Test Artist",
              "albumName": "Test Album",
              "duration": 123.5,
              "instrumental": false,
              "syncedLyrics": "[00:01.00]First"
            }]
            """;
        var tracks =
            JsonSerializer.Deserialize<LrcLibTrackDto[]>(json);
        Assert(
            tracks is
            [
                {
                    TrackName: "Test Track",
                    ArtistName: "Test Artist",
                    Duration: 123.5,
                    Instrumental: false,
                    SyncedLyrics: not null,
                },
            ],
            "LRCLIB JSON");
    }

    private static void AssertLrcLibRetryPolicy()
    {
        Assert(
            LrcLibLyricsProvider.IsTransientStatusCode(
                HttpStatusCode.TooManyRequests) &&
            LrcLibLyricsProvider.IsTransientStatusCode(
                HttpStatusCode.InternalServerError) &&
            LrcLibLyricsProvider.IsTransientStatusCode(
                HttpStatusCode.GatewayTimeout) &&
            LrcLibLyricsProvider.IsTransientStatusCode(
                (HttpStatusCode)521) &&
            !LrcLibLyricsProvider.IsTransientStatusCode(
                HttpStatusCode.NotFound) &&
            !LrcLibLyricsProvider.IsTransientStatusCode(
                HttpStatusCode.BadRequest),
            "LRCLIB transient status policy");

        using var response = new HttpResponseMessage(
            HttpStatusCode.TooManyRequests);
        response.Headers.RetryAfter =
            new RetryConditionHeaderValue(
                TimeSpan.FromSeconds(7));
        Assert(
            LrcLibLyricsProvider.GetRetryDelay(response) ==
                TimeSpan.FromSeconds(7),
            "LRCLIB Retry-After policy");

        Assert(
            LrcLibLyricsProvider.GetBackgroundRetryDelay(1) ==
                TimeSpan.FromSeconds(15) &&
            LrcLibLyricsProvider.GetBackgroundRetryDelay(2) ==
                TimeSpan.FromSeconds(30) &&
            LrcLibLyricsProvider.GetBackgroundRetryDelay(3) ==
                TimeSpan.FromSeconds(60) &&
            LrcLibLyricsProvider.GetBackgroundRetryDelay(10) ==
                TimeSpan.FromSeconds(120),
            "LRCLIB background retry backoff");
    }

    private static void AssertCrc32()
    {
        Assert(
            Crc32.Compute("123456789"u8) == 0xCBF43926U,
            "CRC32");
    }

    private static void AssertSystemVolumeMapping()
    {
        Assert(
            WindowsSystemVolume.ToPercentage(0.636F, false) == 64,
            "system volume rounding");
        Assert(
            WindowsSystemVolume.ToPercentage(0.8F, true) == 0,
            "muted system volume");
        Assert(
            WindowsSystemVolume.ToPercentage(float.NaN, false) ==
                byte.MaxValue,
            "invalid system volume");
    }

    private static void AssertCommandAck()
    {
        Assert(
            RpmpProtocol.TryParse(
                "@RPMP1|ACK|7|MEDIA",
                out var message),
            "command ACK parse");
        RpmpProtocol.ValidateAck(message!, "MEDIA");
    }

    private static void AssertStatus()
    {
        Assert(
            RpmpProtocol.TryParse(
                "@RPMP1|STATUS|3|GIF|37641|0|0|0|0|0|0",
                out var message),
            "STATUS parse");
        var status = RpmpProtocol.ParseStatus(message!);
        Assert(
            status.Sequence == 3 &&
            status.Mode == "GIF" &&
            status.UptimeMs == 37641 &&
            status.Track == 0 &&
            status.TrackCount == 0 &&
            status.PositionMs == 0 &&
            status.DurationMs == 0 &&
            status.Volume == 0 &&
            !status.Playing,
            "STATUS fields");
    }

    private static void AssertPcStatus()
    {
        Assert(
            RpmpProtocol.TryParse(
                "@RPMP1|STATUS|12|PC|1000|1|1|500|60000|80|1",
                out var message),
            "PC STATUS parse");
        var status = RpmpProtocol.ParseStatus(message!);
        Assert(
            status.Mode == "PC" &&
            status.Track == 1 &&
            status.TrackCount == 1,
            "PC STATUS fields");
    }

    private static void AssertTextLimit()
    {
        AssertThrows<ArgumentException>(
            () => RpmpProtocol.CreateMediaRequest(
                1,
                1,
                1000,
                new string('Я', 48),
                "Artist",
                "Album"),
            "UTF-8 text limit");
    }

    private static void AssertUtf8Fitting()
    {
        var fitted = RpmpProtocol.FitTitle(new string('Я', 100));
        Assert(
            fitted.Length == 47 &&
            System.Text.Encoding.UTF8.GetByteCount(fitted) == 94,
            "UTF-8 safe fitting");
    }

    private static void AssertDiagnosticsIgnored()
    {
        Assert(
            !RpmpProtocol.TryParse(
                "[HEALTH] status=OK",
                out _),
            "diagnostic filtering");
    }

    private static void AssertMalformedRejected()
    {
        Assert(
            !RpmpProtocol.TryParse(
                "@RPMP1|PING|65536",
                out _),
            "sequence range");
    }

    private static void AssertBackgroundOptions()
    {
        Assert(
            CompanionOptions.TryParse(
                [],
                out var defaultOptions,
                out var defaultError),
            $"Default options rejected: {defaultError}");
        Assert(
            defaultOptions.Background &&
            defaultOptions.MediaSession,
            "No arguments must start background media mode.");

        Assert(
            CompanionOptions.TryParse(
                ["--once"],
                out var onceOptions,
                out var onceError),
            $"Once options rejected: {onceError}");
        Assert(
            !onceOptions.Background &&
            !onceOptions.MediaSession,
            "--once must keep the explicit diagnostic mode.");

        Assert(
            CompanionOptions.TryParse(
                ["--background", "--no-lyrics"],
                out var options,
                out var error) &&
            error is null &&
            options.Background &&
            options.MediaSession &&
            options.NoLyrics,
            "background options");
        Assert(
            !CompanionOptions.TryParse(
                [
                    "--install-autostart",
                    "--remove-autostart",
                ],
                out _,
                out _),
            "autostart option conflict");
        Assert(
            CompanionOptions.TryParse(
                ["--stop-background"],
                out options,
                out error) &&
            error is null &&
            options.StopBackground,
            "stop background option");
    }

    private static void AssertConfiguration()
    {
        var config = CompanionConfig.Parse(
            """
            {
              "preferredPort": " COM20 ",
              "lyricsEnabled": false
            }
            """);
        Assert(
            config.PreferredPort == "COM20" &&
            !config.LyricsEnabled,
            "configuration");
    }

    private static void AssertAutostartCommand()
    {
        Assert(
            WindowsAutostart.BuildCommand(
                @"C:\Program Files\RPMP\Companion.exe") ==
            "\"C:\\Program Files\\RPMP\\Companion.exe\" --background",
            "autostart command");
    }

    private static void AssertLateCoverDelivery()
    {
        var state = new MediaDeliveryState();
        var now = DateTimeOffset.UnixEpoch;
        var withoutCover = CreateSnapshot(
            42,
            null,
            null);

        Assert(
            state.NeedsMedia(withoutCover.MediaId),
            "new media delivery");
        state.MarkMediaSent(withoutCover.MediaId);
        Assert(
            !state.ShouldSendCover(
                withoutCover,
                now,
                out _),
            "missing cover must not send");

        var lateCover = CreateSnapshot(
            42,
            [0x12, 0x34],
            0x12345678);
        var canSendLateCover =
            state.ShouldSendCover(
                lateCover,
                now,
                out var identity);
        Assert(
            !state.NeedsMedia(lateCover.MediaId) &&
            canSendLateCover,
            "late cover delivery");
        state.MarkCoverSent(identity);
        Assert(
            !state.ShouldSendCover(
                lateCover,
                now,
                out _),
            "duplicate cover suppression");

        var changedCover = lateCover with
        {
            CoverRgb565 = [0x56, 0x78],
            CoverCrc32 = 0x87654321,
        };
        Assert(
            state.ShouldSendCover(
                changedCover,
                now,
                out _),
            "same-media cover revision");
    }

    private static void AssertCoverRetry()
    {
        var state = new MediaDeliveryState();
        var now = DateTimeOffset.UnixEpoch;
        var snapshot = CreateSnapshot(
            7,
            [0xAA],
            0xAABBCCDD);

        Assert(
            state.ShouldSendCover(
                snapshot,
                now,
                out var identity),
            "initial cover attempt");
        Assert(
            state.MarkCoverFailed(identity, now) == 1 &&
            !state.ShouldSendCover(
                snapshot,
                now + TimeSpan.FromMilliseconds(499),
                out _),
            "first cover retry delay");
        Assert(
            state.ShouldSendCover(
                snapshot,
                now + TimeSpan.FromMilliseconds(500),
                out identity),
            "first cover retry");

        var secondAttempt =
            now + TimeSpan.FromMilliseconds(500);
        Assert(
            state.MarkCoverFailed(
                identity,
                secondAttempt) == 2 &&
            !state.ShouldSendCover(
                snapshot,
                secondAttempt + TimeSpan.FromMilliseconds(1999),
                out _),
            "second cover retry delay");
        Assert(
            state.ShouldSendCover(
                snapshot,
                secondAttempt + TimeSpan.FromSeconds(2),
                out identity),
            "second cover retry");
        var thirdAttempt = secondAttempt + TimeSpan.FromSeconds(2);
        Assert(
            state.MarkCoverFailed(identity, thirdAttempt) == 3 &&
            !state.ShouldSendCover(
                snapshot,
                thirdAttempt + TimeSpan.FromMilliseconds(4999),
                out _),
            "third cover retry delay");
        Assert(
            state.ShouldSendCover(
                snapshot,
                thirdAttempt + TimeSpan.FromSeconds(5),
                out identity),
            "third cover retry");
        var backgroundAttempt = thirdAttempt + TimeSpan.FromSeconds(5);
        Assert(
            state.MarkCoverFailed(identity, backgroundAttempt) == 4 &&
            !state.ShouldSendCover(
                snapshot,
                backgroundAttempt + TimeSpan.FromSeconds(29),
                out _),
            "background cover retry delay");
        Assert(
            state.ShouldSendCover(
                snapshot,
                backgroundAttempt + TimeSpan.FromSeconds(30),
                out _),
            "cover retries must remain recoverable");
    }

    private static void AssertSessionContinuity()
    {
        var state = new MediaSessionContinuityState();
        var now = DateTimeOffset.UnixEpoch;

        Assert(
            !state.ShouldReleaseAbsent(now) &&
            !state.ShouldReleaseAbsent(
                now + TimeSpan.FromMilliseconds(1999)) &&
            state.ShouldReleaseAbsent(
                now + TimeSpan.FromSeconds(2)) &&
            !state.ShouldReleaseAbsent(
                now + TimeSpan.FromSeconds(3)),
            "media-session absence grace");

        state.MarkAvailable();
        Assert(
            !state.ShouldRecoverTransientFailure(now) &&
            !state.ShouldRecoverTransientFailure(
                now + TimeSpan.FromMilliseconds(4999)) &&
            state.ShouldRecoverTransientFailure(
                now + TimeSpan.FromSeconds(5)) &&
            !state.ShouldRecoverTransientFailure(
                now + TimeSpan.FromSeconds(6)),
            "media-session transient grace");
    }

    private static WindowsMediaSnapshot CreateSnapshot(
        uint mediaId,
        byte[]? cover,
        uint? coverCrc32) =>
        new(
            mediaId,
            "test.player",
            "Title",
            "Artist",
            "Album",
            1000,
            60_000,
            true,
            50,
            cover,
            coverCrc32);

    private static void Assert(bool condition, string check)
    {
        if (!condition)
        {
            throw new InvalidOperationException(
                $"Protocol self-test failed: {check}");
        }
    }

    private static void AssertThrows<TException>(
        Action action,
        string check)
        where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }

        throw new InvalidOperationException(
            $"Protocol self-test failed: {check}");
    }
}
