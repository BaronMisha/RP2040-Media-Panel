using System.Diagnostics;
using System.IO.Ports;
using Rp2040MediaPanel.Companion.Lyrics;
using Rp2040MediaPanel.Companion.Media;
using Rp2040MediaPanel.Companion.Protocol;
using Rp2040MediaPanel.Companion.Serial;

namespace Rp2040MediaPanel.Companion;

internal static class CompanionRunner
{
    private static readonly TimeSpan ReconnectDelay =
        TimeSpan.FromSeconds(2);
    private static readonly TimeSpan StatusPollInterval =
        TimeSpan.FromSeconds(2);
    private static readonly TimeSpan SimulatedStateInterval =
        TimeSpan.FromSeconds(1);
    private static readonly TimeSpan MediaSessionPollInterval =
        TimeSpan.FromMilliseconds(200);
    private static readonly TimeSpan MediaStateTransmitInterval =
        TimeSpan.FromSeconds(1);
    private static readonly TimeSpan DiscoveryResponseTimeout =
        TimeSpan.FromMilliseconds(750);
    private static readonly TimeSpan HealthReportInterval =
        TimeSpan.FromMinutes(5);
    private const uint SimulatedDurationMs = 60_000;

    public static string[] GetPorts()
    {
        return SerialPort.GetPortNames()
            .OrderBy(PortSortKey)
            .ThenBy(port => port, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    public static async Task RunOnce(
        string portName,
        CancellationToken cancellationToken)
    {
        using var client = new SerialDeviceClient(portName);
        client.Open();
        Console.WriteLine($"[INFO] Подключено: {client.PortName}");

        var hello = client.Hello(cancellationToken);
        PrintHello(hello);

        var latency = client.Ping(cancellationToken);
        Console.WriteLine(
            $"[INFO] PING: {latency.TotalMilliseconds:0.0} мс");

        var status = client.GetStatus(cancellationToken);
        PrintStatus(status);
        await Task.CompletedTask;
    }

    public static async Task Watch(
        string portName,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                using var client = new SerialDeviceClient(portName);
                client.Open();
                Console.WriteLine($"[INFO] Подключено: {client.PortName}");
                PrintHello(client.Hello(cancellationToken));
                Console.WriteLine("[INFO] Мониторинг запущен. Ctrl+C — выход.");

                while (!cancellationToken.IsCancellationRequested)
                {
                    PrintStatus(client.GetStatus(cancellationToken));
                    await Task.Delay(
                        StatusPollInterval,
                        cancellationToken);
                }
            }
            catch (OperationCanceledException)
                when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (Exception exception)
                when (exception is IOException or
                      InvalidOperationException or
                      TimeoutException or
                      UnauthorizedAccessException)
            {
                Console.Error.WriteLine(
                    $"[WARN] Связь потеряна: {exception.Message}");
                Console.Error.WriteLine(
                    $"[INFO] Повтор через {ReconnectDelay.TotalSeconds:0} с.");
                await Task.Delay(ReconnectDelay, cancellationToken);
            }
        }
    }

    public static async Task Simulate(
        string portName,
        CancellationToken cancellationToken)
    {
        var playbackTimer = Stopwatch.StartNew();

        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                using var client = new SerialDeviceClient(portName);
                client.Open();
                Console.WriteLine($"[INFO] Подключено: {client.PortName}");
                PrintHello(client.Hello(cancellationToken));

                var remoteMediaStarted = false;
                try
                {
                    client.SendMedia(
                        1,
                        SimulatedDurationMs,
                        "PC Link Test",
                        "Windows Companion",
                        "RPMP | Transport",
                        cancellationToken);
                    remoteMediaStarted = true;
                    Console.WriteLine(
                        "[INFO] Тестовый PC-трек передаётся. Ctrl+C — выход.");

                    var stateCount = 0;
                    while (!cancellationToken.IsCancellationRequested)
                    {
                        var positionMs = (uint)(
                            playbackTimer.ElapsedMilliseconds %
                            SimulatedDurationMs);
                        client.SendState(
                            positionMs,
                            SimulatedDurationMs,
                            80,
                            true,
                            cancellationToken);

                        stateCount++;
                        if (stateCount % 5 == 0)
                        {
                            PrintStatus(
                                client.GetStatus(cancellationToken));
                        }

                        await Task.Delay(
                            SimulatedStateInterval,
                            cancellationToken);
                    }
                }
                finally
                {
                    if (remoteMediaStarted && client.IsOpen)
                    {
                        TryRelease(client);
                    }
                }
            }
            catch (OperationCanceledException)
                when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (Exception exception)
                when (exception is IOException or
                      InvalidOperationException or
                      TimeoutException or
                      UnauthorizedAccessException)
            {
                Console.Error.WriteLine(
                    $"[WARN] Связь потеряна: {exception.Message}");
                Console.Error.WriteLine(
                    $"[INFO] Повтор через {ReconnectDelay.TotalSeconds:0} с.");
                await Task.Delay(ReconnectDelay, cancellationToken);
            }
        }
    }

    public static async Task FollowWindowsMediaSession(
        string? portName,
        bool autoDiscover,
        bool lyricsEnabled,
        CancellationToken cancellationToken)
    {
        using var mediaProvider =
            await WindowsMediaSessionProvider.CreateAsync();
        var lyricsProvider = lyricsEnabled
            ? new LrcLibLyricsProvider()
            : null;
        Console.WriteLine("[INFO] Windows Media Session подключена.");
        Console.WriteLine(
            lyricsEnabled
                ? "[INFO] Автоматический поиск текста включён."
                : "[INFO] Автоматический поиск текста отключён.");
        var missingDeviceLogged = false;
        var healthTimer = Stopwatch.StartNew();
        uint mediaDeliveries = 0;
        uint coverDeliveries = 0;
        uint lateCoverDeliveries = 0;
        uint coverFailures = 0;
        uint sessionGaps = 0;
        uint mediaProviderFailures = 0;

        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var selectedPort = autoDiscover
                    ? FindRpmpPort(portName, cancellationToken)
                    : portName;
                if (selectedPort is null)
                {
                    if (!missingDeviceLogged)
                    {
                        Console.WriteLine(
                            "[INFO] Панель RPMP не найдена; " +
                            "поиск продолжается.");
                        missingDeviceLogged = true;
                    }
                    await Task.Delay(
                        ReconnectDelay,
                        cancellationToken);
                    continue;
                }

                missingDeviceLogged = false;
                using var client =
                    new SerialDeviceClient(selectedPort);
                client.Open();
                Console.WriteLine($"[INFO] Подключено: {client.PortName}");
                PrintHello(client.Hello(cancellationToken));

                var remoteMediaStarted = false;
                var deliveryState = new MediaDeliveryState();
                var continuityState =
                    new MediaSessionContinuityState();
                byte? reportedVolume = null;
                var missingSessionLogged = false;
                var transientFailureLogged = false;
                Task<LyricsLookupResult>? lyricsLookup = null;
                CancellationTokenSource?
                    lyricsLookupCancellation = null;
                SyncedLyricsTrack? activeLyrics = null;
                uint? lyricsMediaId = null;
                var nextLyricsLookupAt = DateTimeOffset.MaxValue;
                var lyricsRetryAttempt = 0;
                uint? sentKaraokeStartMs = null;
                var karaokeClearSent = false;
                var stateTransmitTimer = Stopwatch.StartNew();

                try
                {
                    while (!cancellationToken.IsCancellationRequested)
                    {
                        var pollResult =
                            await mediaProvider.GetCurrentAsync(
                                cancellationToken);
                        var now = DateTimeOffset.UtcNow;
                        if (pollResult.Status ==
                            MediaSessionPollStatus.Absent)
                        {
                            if (!missingSessionLogged)
                            {
                                Console.WriteLine(
                                    "[INFO] Активная медиасессия временно " +
                                    "отсутствует; ожидание 2 с.");
                                missingSessionLogged = true;
                                sessionGaps++;
                            }
                            transientFailureLogged = false;

                            if (remoteMediaStarted &&
                                continuityState.ShouldReleaseAbsent(now))
                            {
                                client.Release(cancellationToken);
                                remoteMediaStarted = false;
                                deliveryState.Reset();
                                CancelLyricsLookup(
                                    ref lyricsLookupCancellation);
                                lyricsLookup = null;
                                activeLyrics = null;
                                lyricsMediaId = null;
                                nextLyricsLookupAt = DateTimeOffset.MaxValue;
                                lyricsRetryAttempt = 0;
                                sentKaraokeStartMs = null;
                                karaokeClearSent = false;
                                Console.WriteLine(
                                    "[INFO] Устройство переключено на GIF-заставку.");
                            }
                        }
                        else if (pollResult.Status ==
                                 MediaSessionPollStatus.TransientFailure)
                        {
                            missingSessionLogged = false;
                            if (!transientFailureLogged)
                            {
                                Console.Error.WriteLine(
                                    $"[WARN] Временная ошибка медиасессии: " +
                                    pollResult.Error);
                                transientFailureLogged = true;
                                mediaProviderFailures++;
                            }

                            if (continuityState
                                .ShouldRecoverTransientFailure(now))
                            {
                                if (remoteMediaStarted)
                                {
                                    client.Release(cancellationToken);
                                    remoteMediaStarted = false;
                                    deliveryState.Reset();
                                    CancelLyricsLookup(
                                        ref lyricsLookupCancellation);
                                    lyricsLookup = null;
                                    activeLyrics = null;
                                    lyricsMediaId = null;
                                    nextLyricsLookupAt =
                                        DateTimeOffset.MaxValue;
                                    lyricsRetryAttempt = 0;
                                    sentKaraokeStartMs = null;
                                    karaokeClearSent = false;
                                }

                                try
                                {
                                    await mediaProvider.ReinitializeAsync(
                                        cancellationToken);
                                    Console.WriteLine(
                                        "[INFO] Подключение к Windows Media " +
                                        "Session пересоздано.");
                                }
                                catch (Exception exception)
                                    when (exception is not
                                        OperationCanceledException)
                                {
                                    Console.Error.WriteLine(
                                        "[WARN] Не удалось пересоздать " +
                                        $"медиасессию: {exception.Message}");
                                }
                            }
                        }
                        else
                        {
                            var snapshot = pollResult.Snapshot!;
                            if (missingSessionLogged ||
                                transientFailureLogged)
                            {
                                Console.WriteLine(
                                    "[INFO] Медиасессия восстановлена.");
                            }
                            missingSessionLogged = false;
                            transientFailureLogged = false;
                            continuityState.MarkAvailable();
                            var mediaChanged =
                                deliveryState.NeedsMedia(
                                    snapshot.MediaId);
                            if (mediaChanged)
                            {
                                client.SendMedia(
                                    snapshot.MediaId,
                                    snapshot.DurationMs,
                                    RpmpProtocol.FitTitle(snapshot.Title),
                                    RpmpProtocol.FitArtist(snapshot.Artist),
                                    RpmpProtocol.FitAlbum(snapshot.Album),
                                    cancellationToken);
                                deliveryState.MarkMediaSent(
                                    snapshot.MediaId);
                                mediaDeliveries++;
                                remoteMediaStarted = true;
                                CancelLyricsLookup(
                                    ref lyricsLookupCancellation);
                                activeLyrics = null;
                                nextLyricsLookupAt = DateTimeOffset.MaxValue;
                                lyricsRetryAttempt = 0;
                                sentKaraokeStartMs = null;
                                karaokeClearSent = false;
                                if (lyricsProvider is not null)
                                {
                                    lyricsLookupCancellation =
                                        CancellationTokenSource
                                            .CreateLinkedTokenSource(
                                                cancellationToken);
                                    lyricsLookup =
                                        lyricsProvider.GetAsync(
                                            snapshot,
                                            lyricsLookupCancellation.Token);
                                    lyricsMediaId = snapshot.MediaId;
                                }
                                else
                                {
                                    lyricsLookup = null;
                                    lyricsMediaId = null;
                                }
                                PrintWindowsMedia(snapshot);
                                if (snapshot.CoverRgb565 is null)
                                {
                                    Console.WriteLine(
                                        $"[COVER] media={snapshot.MediaId} " +
                                        "обложка пока недоступна; " +
                                        "повтор запланирован.");
                                }
                            }

                            if (mediaChanged ||
                                stateTransmitTimer.Elapsed >=
                                    MediaStateTransmitInterval)
                            {
                                client.SendState(
                                    snapshot.PositionMs,
                                    snapshot.DurationMs,
                                    snapshot.Volume,
                                    snapshot.Playing,
                                    cancellationToken);
                                stateTransmitTimer.Restart();
                                if (reportedVolume != snapshot.Volume)
                                {
                                    var volumeText =
                                        snapshot.Volume == byte.MaxValue
                                            ? "недоступна"
                                            : $"{snapshot.Volume}%";
                                    Console.WriteLine(
                                        $"[AUDIO] Системная громкость: " +
                                        volumeText);
                                    reportedVolume = snapshot.Volume;
                                }
                            }

                            if (deliveryState.ShouldSendCover(
                                    snapshot,
                                    now,
                                    out var coverIdentity))
                            {
                                try
                                {
                                    var lateCover = !mediaChanged;
                                    Console.WriteLine(
                                        $"[COVER] media={coverIdentity.MediaId} " +
                                        $"crc={coverIdentity.Crc32:X8} " +
                                        (lateCover
                                            ? "поздняя обложка готова."
                                            : "обложка готова."));
                                    client.SendCover(
                                        snapshot.MediaId,
                                        snapshot.CoverRgb565!,
                                        cancellationToken);
                                    deliveryState.MarkCoverSent(
                                        coverIdentity);
                                    coverDeliveries++;
                                    if (lateCover)
                                    {
                                        lateCoverDeliveries++;
                                        Console.WriteLine(
                                            $"[COVER] media=" +
                                            $"{coverIdentity.MediaId} " +
                                            "поздняя обложка активирована.");
                                    }
                                }
                                catch (Exception exception)
                                    when (exception is TimeoutException or
                                          ProtocolException)
                                {
                                    var attempt =
                                        deliveryState.MarkCoverFailed(
                                            coverIdentity,
                                            now);
                                    coverFailures++;
                                    Console.Error.WriteLine(
                                        $"[WARN] media=" +
                                        $"{coverIdentity.MediaId} " +
                                        $"передача обложки не удалась, " +
                                        $"попытка {attempt}: " +
                                        exception.Message);
                                }
                            }

                            if (lyricsLookup is not null &&
                                lyricsMediaId == snapshot.MediaId &&
                                lyricsLookup.IsCompleted)
                            {
                                var lookupResult = await lyricsLookup;
                                activeLyrics = lookupResult.Track;
                                lyricsLookup = null;
                                lyricsLookupCancellation?.Dispose();
                                lyricsLookupCancellation = null;
                                if (lookupResult.ShouldRetry)
                                {
                                    var backgroundDelay =
                                        LrcLibLyricsProvider
                                            .GetBackgroundRetryDelay(
                                                ++lyricsRetryAttempt);
                                    var serverDelay =
                                        lookupResult.RetryAfter ??
                                        TimeSpan.Zero;
                                    var retryDelay =
                                        serverDelay > backgroundDelay
                                            ? serverDelay
                                            : backgroundDelay;
                                    nextLyricsLookupAt = now + retryDelay;
                                    Console.Error.WriteLine(
                                        $"[WARN] Повтор поиска текста через " +
                                        $"{retryDelay.TotalSeconds:0} с.");
                                }
                                else
                                {
                                    nextLyricsLookupAt =
                                        DateTimeOffset.MaxValue;
                                }
                            }

                            if (lyricsProvider is not null &&
                                lyricsLookup is null &&
                                activeLyrics is null &&
                                lyricsMediaId == snapshot.MediaId &&
                                now >= nextLyricsLookupAt)
                            {
                                lyricsLookupCancellation =
                                    CancellationTokenSource
                                        .CreateLinkedTokenSource(
                                            cancellationToken);
                                lyricsLookup = lyricsProvider.GetAsync(
                                    snapshot,
                                    lyricsLookupCancellation.Token);
                                nextLyricsLookupAt =
                                    DateTimeOffset.MaxValue;
                                Console.WriteLine(
                                    "[LYRICS] Повторный поиск текста.");
                            }

                            if (activeLyrics is not null &&
                                lyricsMediaId == snapshot.MediaId &&
                                activeLyrics.TryGetFrame(
                                    snapshot.PositionMs,
                                    snapshot.DurationMs,
                                    out var frame) &&
                                frame is not null &&
                                sentKaraokeStartMs != frame.StartMs)
                            {
                                client.SendKaraoke(
                                    snapshot.MediaId,
                                    frame.StartMs,
                                    frame.EndMs,
                                    RpmpProtocol.FitKaraokePrevious(
                                        frame.PreviousLine),
                                    RpmpProtocol.FitKaraokeCurrent(
                                        frame.CurrentLine),
                                    RpmpProtocol.FitKaraokeNext(
                                        frame.NextLine),
                                    cancellationToken);
                                sentKaraokeStartMs = frame.StartMs;
                                Console.WriteLine(
                                    $"[LYRICS] Строка " +
                                    $"{frame.StartMs}...{frame.EndMs} мс " +
                                    "передана.");
                            }
                            else if (lyricsLookup is null &&
                                     activeLyrics is null &&
                                     lyricsMediaId ==
                                         snapshot.MediaId &&
                                     !karaokeClearSent)
                            {
                                client.ClearKaraoke(
                                    snapshot.MediaId,
                                    cancellationToken);
                                karaokeClearSent = true;
                            }
                        }

                        if (healthTimer.Elapsed >=
                            HealthReportInterval)
                        {
                            Console.WriteLine(
                                $"[HEALTH] media={mediaDeliveries} " +
                                $"covers={coverDeliveries} " +
                                $"late_covers={lateCoverDeliveries} " +
                                $"cover_failures={coverFailures} " +
                                $"session_gaps={sessionGaps} " +
                                $"media_errors={mediaProviderFailures}");
                            healthTimer.Restart();
                        }

                        await Task.Delay(
                            MediaSessionPollInterval,
                            cancellationToken);
                    }
                }
                finally
                {
                    CancelLyricsLookup(
                        ref lyricsLookupCancellation);
                    if (remoteMediaStarted && client.IsOpen)
                    {
                        TryRelease(client);
                    }
                }
            }
            catch (OperationCanceledException)
                when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (Exception exception)
                when (exception is IOException or
                      InvalidOperationException or
                      TimeoutException or
                      UnauthorizedAccessException or
                      ProtocolException)
            {
                Console.Error.WriteLine(
                    $"[WARN] Связь потеряна: {exception.Message}");
                Console.Error.WriteLine(
                    $"[INFO] Повтор через {ReconnectDelay.TotalSeconds:0} с.");
                await Task.Delay(ReconnectDelay, cancellationToken);
            }
        }
    }

    private static void CancelLyricsLookup(
        ref CancellationTokenSource? cancellation)
    {
        cancellation?.Cancel();
        cancellation?.Dispose();
        cancellation = null;
    }

    private static string? FindRpmpPort(
        string? preferredPort,
        CancellationToken cancellationToken)
    {
        var ports = GetPorts();
        IEnumerable<string> candidates = ports;
        if (!string.IsNullOrWhiteSpace(preferredPort))
        {
            candidates = new[] { preferredPort }
                .Concat(ports)
                .Distinct(StringComparer.OrdinalIgnoreCase);
        }

        foreach (var candidate in candidates)
        {
            cancellationToken.ThrowIfCancellationRequested();
            try
            {
                using var client = new SerialDeviceClient(
                    candidate,
                    DiscoveryResponseTimeout);
                client.Open();
                var hello = client.Hello(
                    cancellationToken,
                    logTraffic: false);
                if ((hello.ProtocolVersion == 1 ||
                     hello.ProtocolVersion == 2) &&
                    hello.Board == "YD-RP2040" &&
                    hello.DisplayController == "ILI9341" &&
                    hello.Width == 240 &&
                    hello.Height == 320)
                {
                    Console.WriteLine(
                        $"[INFO] Найдена панель RPMP: {candidate}");
                    return candidate;
                }
            }
            catch (Exception exception)
                when (exception is IOException or
                      InvalidOperationException or
                      TimeoutException or
                      UnauthorizedAccessException or
                      ProtocolException)
            {
                continue;
            }
        }

        return null;
    }

    private static void PrintHello(DeviceHello hello)
    {
        Console.WriteLine(
            $"[INFO] RPMP{hello.ProtocolVersion}: " +
            $"{hello.Board}, {hello.DisplayController}, " +
            $"{hello.Width}x{hello.Height}");
    }

    private static void PrintStatus(DeviceStatus status)
    {
        var volume = status.Volume == byte.MaxValue
            ? "--"
            : $"{status.Volume}%";
        Console.WriteLine(
            $"[STATUS] mode={status.Mode} " +
            $"uptime={status.UptimeMs / 1000.0:0.0}s " +
            $"track={status.Track}/{status.TrackCount} " +
            $"position={status.PositionMs}/{status.DurationMs}ms " +
            $"volume={volume} " +
            $"playing={(status.Playing ? 1 : 0)}");
    }

    private static void PrintWindowsMedia(WindowsMediaSnapshot snapshot)
    {
        Console.WriteLine(
            $"[MEDIA] {snapshot.Artist} — {snapshot.Title}");
        if (!string.IsNullOrWhiteSpace(snapshot.Album))
        {
            Console.WriteLine($"[MEDIA] Альбом: {snapshot.Album}");
        }
        Console.WriteLine(
            $"[MEDIA] Источник: {snapshot.SourceAppId}");
    }

    private static int PortSortKey(string portName)
    {
        return portName.StartsWith("COM", StringComparison.OrdinalIgnoreCase) &&
               int.TryParse(portName.AsSpan(3), out var number)
            ? number
            : int.MaxValue;
    }

    private static void TryRelease(SerialDeviceClient client)
    {
        try
        {
            client.Release(CancellationToken.None);
            Console.WriteLine(
                "[INFO] Устройство переключено на GIF-заставку.");
        }
        catch (Exception exception)
            when (exception is IOException or
                  InvalidOperationException or
                  TimeoutException or
                  UnauthorizedAccessException or
                  ProtocolException)
        {
            Console.Error.WriteLine(
                $"[WARN] Не удалось отправить RELEASE: {exception.Message}");
        }
    }
}
