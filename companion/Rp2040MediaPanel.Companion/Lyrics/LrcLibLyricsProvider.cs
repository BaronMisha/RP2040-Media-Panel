using System.Net;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Rp2040MediaPanel.Companion.Media;

namespace Rp2040MediaPanel.Companion.Lyrics;

internal sealed class LrcLibLyricsProvider
{
    private static readonly HttpClient SharedClient =
        CreateHttpClient();

    private const int MaximumResolvedTracks = 256;
    private const int MaximumRequestAttempts = 2;
    private static readonly TimeSpan MinimumRequestInterval =
        TimeSpan.FromMilliseconds(350);

    private readonly object cacheGate = new();
    private readonly Dictionary<uint, SyncedLyricsTrack> resolved = [];
    private readonly Queue<uint> resolvedOrder = [];
    private readonly Dictionary<
        uint,
        (Task<LyricsLookupResult> Task, CancellationToken Token)> inFlight = [];
    private readonly SemaphoreSlim requestGate = new(1, 1);
    private readonly string cacheDirectory;
    private DateTimeOffset nextRequestAtUtc = DateTimeOffset.MinValue;

    public LrcLibLyricsProvider(string? cacheDirectory = null)
    {
        this.cacheDirectory = cacheDirectory ??
            Path.Combine(
                Environment.GetFolderPath(
                    Environment.SpecialFolder.LocalApplicationData),
                "RP2040MediaPanel",
                "Lyrics");
    }

    public async Task<LyricsLookupResult> GetAsync(
        WindowsMediaSnapshot snapshot,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        Task<LyricsLookupResult> lookup;
        lock (cacheGate)
        {
            if (resolved.TryGetValue(snapshot.MediaId, out var cached))
            {
                return LyricsLookupResult.Found(cached);
            }
            if (!inFlight.TryGetValue(snapshot.MediaId, out var current) ||
                current.Token.IsCancellationRequested)
            {
                lookup = LoadAsync(snapshot, cancellationToken);
                inFlight[snapshot.MediaId] =
                    (lookup, cancellationToken);
            }
            else
            {
                lookup = current.Task;
            }
        }

        try
        {
            var result = await lookup;
            if (result.Track is { } track)
            {
                lock (cacheGate)
                {
                    if (!resolved.ContainsKey(snapshot.MediaId))
                    {
                        resolved[snapshot.MediaId] = track;
                        resolvedOrder.Enqueue(snapshot.MediaId);
                        while (resolvedOrder.Count > MaximumResolvedTracks)
                        {
                            resolved.Remove(resolvedOrder.Dequeue());
                        }
                    }
                }
            }
            return result;
        }
        finally
        {
            lock (cacheGate)
            {
                if (inFlight.TryGetValue(snapshot.MediaId, out var current) &&
                    ReferenceEquals(current.Task, lookup))
                {
                    inFlight.Remove(snapshot.MediaId);
                }
            }
        }
    }

    internal string GetCachePath(uint mediaId)
    {
        return Path.Combine(
            cacheDirectory,
            $"{mediaId:X8}.lrc");
    }

    private async Task<LyricsLookupResult> LoadAsync(
        WindowsMediaSnapshot snapshot,
        CancellationToken cancellationToken)
    {
        var cachePath = GetCachePath(snapshot.MediaId);
        try
        {
            if (File.Exists(cachePath))
            {
                var cachedLrc = await File.ReadAllTextAsync(
                    cachePath,
                    cancellationToken);
                var cachedTrack = LrcParser.Parse(cachedLrc);
                if (cachedTrack is not null)
                {
                    Console.WriteLine(
                        $"[LYRICS] Загружен локальный кэш: " +
                        $"{cachedTrack.Lines.Count} строк.");
                    return LyricsLookupResult.Found(cachedTrack);
                }

                Console.Error.WriteLine(
                    $"[WARN] Некорректный LRC-кэш: {cachePath}");
            }

            var requestUri = BuildSearchUri(snapshot);
            var candidates = await SearchAsync(
                requestUri,
                cancellationToken);
            var selected = SelectBest(candidates, snapshot);
            if (selected?.SyncedLyrics is null)
            {
                LogNotFound();
                return LyricsLookupResult.NotFound();
            }

            var track = LrcParser.Parse(selected.SyncedLyrics);
            if (track is null)
            {
                LogNotFound();
                return LyricsLookupResult.NotFound();
            }

            Console.WriteLine(
                $"[LYRICS] Найден синхронизированный текст: " +
                $"{track.Lines.Count} строк.");
            await SaveCacheAsync(
                cachePath,
                selected.SyncedLyrics,
                cancellationToken);
            return LyricsLookupResult.Found(track);
        }
        catch (OperationCanceledException)
            when (!cancellationToken.IsCancellationRequested)
        {
            Console.Error.WriteLine(
                "[WARN] Тайм-аут запроса текста; " +
                "повтор будет выполнен в фоне.");
            return LyricsLookupResult.Retry(TimeSpan.Zero);
        }
        catch (LyricsServiceUnavailableException exception)
        {
            Console.Error.WriteLine(
                $"[WARN] Текст временно недоступен: " +
                exception.Message);
            return LyricsLookupResult.Retry(exception.RetryAfter);
        }
        catch (HttpRequestException exception)
        {
            Console.Error.WriteLine(
                $"[WARN] Текст недоступен: {exception.Message}");
            return exception.StatusCode is null ||
                   IsTransientStatusCode(exception.StatusCode.Value)
                ? LyricsLookupResult.Retry(TimeSpan.Zero)
                : LyricsLookupResult.NotFound();
        }
        catch (Exception exception)
            when (exception is JsonException or IOException)
        {
            Console.Error.WriteLine(
                $"[WARN] Текст недоступен: {exception.Message}");
            return LyricsLookupResult.Retry(TimeSpan.Zero);
        }
        catch (UnauthorizedAccessException exception)
        {
            Console.Error.WriteLine(
                $"[WARN] Текст недоступен: {exception.Message}");
            return LyricsLookupResult.NotFound();
        }
    }

    private async Task<LrcLibTrackDto[]> SearchAsync(
        Uri requestUri,
        CancellationToken cancellationToken)
    {
        for (var attempt = 1;
             attempt <= MaximumRequestAttempts;
             attempt++)
        {
            await requestGate.WaitAsync(cancellationToken);
            try
            {
                var throttleDelay =
                    nextRequestAtUtc - DateTimeOffset.UtcNow;
                if (throttleDelay > TimeSpan.Zero)
                {
                    await Task.Delay(
                        throttleDelay,
                        cancellationToken);
                }

                using var response = await SharedClient.GetAsync(
                    requestUri,
                    HttpCompletionOption.ResponseHeadersRead,
                    cancellationToken);
                nextRequestAtUtc =
                    DateTimeOffset.UtcNow + MinimumRequestInterval;

                if (response.StatusCode == HttpStatusCode.NotFound)
                {
                    return [];
                }

                if (IsTransientStatusCode(response.StatusCode))
                {
                    var retryDelay = GetRetryDelay(response);
                    if (attempt < MaximumRequestAttempts)
                    {
                        Console.Error.WriteLine(
                            $"[WARN] LRCLIB временно ответил " +
                            $"{(int)response.StatusCode}; повтор через " +
                            $"{retryDelay.TotalSeconds:0.#} с.");
                        await Task.Delay(
                            retryDelay,
                            cancellationToken);
                        continue;
                    }

                    throw new LyricsServiceUnavailableException(
                        response.StatusCode,
                        retryDelay);
                }

                response.EnsureSuccessStatusCode();
                await using var stream =
                    await response.Content.ReadAsStreamAsync(
                        cancellationToken);
                return await JsonSerializer.DeserializeAsync<
                        LrcLibTrackDto[]>(
                        stream,
                        cancellationToken: cancellationToken) ??
                    [];
            }
            finally
            {
                requestGate.Release();
            }
        }

        return [];
    }

    internal static bool IsTransientStatusCode(
        HttpStatusCode statusCode)
    {
        var numericStatus = (int)statusCode;
        return statusCode is
                   HttpStatusCode.RequestTimeout or
                   HttpStatusCode.TooManyRequests ||
               numericStatus is >= 500 and <= 599;
    }

    internal static TimeSpan GetBackgroundRetryDelay(int attempt)
    {
        return attempt switch
        {
            <= 1 => TimeSpan.FromSeconds(15),
            2 => TimeSpan.FromSeconds(30),
            3 => TimeSpan.FromSeconds(60),
            _ => TimeSpan.FromSeconds(120),
        };
    }

    internal static TimeSpan GetRetryDelay(
        HttpResponseMessage response)
    {
        var retryAfter = response.Headers.RetryAfter;
        if (retryAfter?.Delta is { } delta &&
            delta > TimeSpan.Zero)
        {
            return delta;
        }
        if (retryAfter?.Date is { } date)
        {
            var delay = date - DateTimeOffset.UtcNow;
            if (delay > TimeSpan.Zero)
            {
                return delay;
            }
        }

        return response.StatusCode == HttpStatusCode.TooManyRequests
            ? TimeSpan.FromSeconds(5)
            : TimeSpan.FromSeconds(1);
    }

    private static Uri BuildSearchUri(
        WindowsMediaSnapshot snapshot)
    {
        var query =
            $"track_name={Uri.EscapeDataString(snapshot.Title)}" +
            $"&artist_name={Uri.EscapeDataString(snapshot.Artist)}";
        return new Uri(
            $"/api/search?{query}",
            UriKind.Relative);
    }

    private static LrcLibTrackDto? SelectBest(
        IEnumerable<LrcLibTrackDto> candidates,
        WindowsMediaSnapshot snapshot)
    {
        var expectedTitle = Normalize(snapshot.Title);
        var expectedArtist = Normalize(snapshot.Artist);
        var expectedAlbum = Normalize(snapshot.Album);
        var expectedDuration =
            snapshot.DurationMs / 1_000.0;

        return candidates
            .Where(candidate =>
                !candidate.Instrumental &&
                !string.IsNullOrWhiteSpace(
                    candidate.SyncedLyrics) &&
                IsCompatible(
                    Normalize(candidate.TrackName),
                    expectedTitle) &&
                IsCompatible(
                    Normalize(candidate.ArtistName),
                    expectedArtist) &&
                IsDurationCompatible(
                    candidate.Duration,
                    expectedDuration))
            .OrderBy(candidate =>
                candidate.Duration > 0 &&
                expectedDuration > 0
                    ? Math.Abs(
                        candidate.Duration -
                        expectedDuration)
                    : 0)
            .ThenByDescending(candidate =>
                Normalize(candidate.AlbumName) ==
                expectedAlbum)
            .FirstOrDefault();
    }

    private static bool IsDurationCompatible(
        double candidate,
        double expected)
    {
        return candidate <= 0 ||
               expected <= 0 ||
               Math.Abs(candidate - expected) <= 15;
    }

    private static bool IsCompatible(
        string candidate,
        string expected)
    {
        if (candidate.Length == 0 || expected.Length == 0)
        {
            return false;
        }
        if (candidate == expected)
        {
            return true;
        }

        var shorter = Math.Min(
            candidate.Length,
            expected.Length);
        var longer = Math.Max(
            candidate.Length,
            expected.Length);
        return shorter * 10 >= longer * 7 &&
               (candidate.Contains(
                    expected,
                    StringComparison.Ordinal) ||
                expected.Contains(
                    candidate,
                    StringComparison.Ordinal));
    }

    private static string Normalize(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return string.Empty;
        }

        var builder = new StringBuilder(value.Length);
        foreach (var character in value)
        {
            if (char.IsLetterOrDigit(character))
            {
                builder.Append(
                    char.ToLowerInvariant(character));
            }
        }
        return builder.ToString();
    }

    private static async Task SaveCacheAsync(
        string path,
        string lrc,
        CancellationToken cancellationToken)
    {
        try
        {
            Directory.CreateDirectory(
                Path.GetDirectoryName(path)!);
            await File.WriteAllTextAsync(
                path,
                lrc,
                new UTF8Encoding(false),
                cancellationToken);
        }
        catch (Exception exception)
            when (exception is IOException or
                  UnauthorizedAccessException)
        {
            Console.Error.WriteLine(
                $"[WARN] Не удалось сохранить LRC-кэш: " +
                exception.Message);
        }
    }

    private static void LogNotFound()
    {
        Console.WriteLine(
            "[LYRICS] Синхронизированный текст не найден; " +
            "остаётся обложка.");
    }

    private static HttpClient CreateHttpClient()
    {
        var handler = new SocketsHttpHandler
        {
            PooledConnectionLifetime =
                TimeSpan.FromMinutes(15),
        };
        var client = new HttpClient(handler)
        {
            BaseAddress = new Uri("https://lrclib.net"),
            Timeout = TimeSpan.FromSeconds(15),
        };
        client.DefaultRequestHeaders.UserAgent.ParseAdd(
            "RP2040MediaPanel/2.0");
        return client;
    }

}

internal readonly record struct LyricsLookupResult(
    SyncedLyricsTrack? Track,
    TimeSpan? RetryAfter)
{
    public bool ShouldRetry =>
        Track is null && RetryAfter is not null;

    public static LyricsLookupResult Found(
        SyncedLyricsTrack track) =>
        new(track, null);

    public static LyricsLookupResult NotFound() =>
        new(null, null);

    public static LyricsLookupResult Retry(
        TimeSpan retryAfter) =>
        new(null, retryAfter);
}

internal sealed class LyricsServiceUnavailableException : Exception
{
    public LyricsServiceUnavailableException(
        HttpStatusCode statusCode,
        TimeSpan retryAfter)
        : base(
            $"LRCLIB ответил {(int)statusCode} " +
            $"({statusCode}).")
    {
        StatusCode = statusCode;
        RetryAfter = retryAfter;
    }

    public HttpStatusCode StatusCode { get; }

    public TimeSpan RetryAfter { get; }
}

internal sealed class LrcLibTrackDto
{
    public LrcLibTrackDto()
    {
    }

    [JsonPropertyName("trackName")]
    public string? TrackName { get; init; }

    [JsonPropertyName("artistName")]
    public string? ArtistName { get; init; }

    [JsonPropertyName("albumName")]
    public string? AlbumName { get; init; }

    [JsonPropertyName("duration")]
    public double Duration { get; init; }

    [JsonPropertyName("instrumental")]
    public bool Instrumental { get; init; }

    [JsonPropertyName("syncedLyrics")]
    public string? SyncedLyrics { get; init; }
}
