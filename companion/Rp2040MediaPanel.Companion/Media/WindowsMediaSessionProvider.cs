using System.Runtime.InteropServices;
using System.Text;
using Rp2040MediaPanel.Companion.Protocol;
using Windows.Graphics.Imaging;
using Windows.Media.Control;

namespace Rp2040MediaPanel.Companion.Media;

internal sealed class WindowsMediaSessionProvider : IDisposable
{
    private static readonly TimeSpan MaximumTimelineAge =
        TimeSpan.FromHours(24);
    private static readonly TimeSpan VolumeRefreshInterval =
        TimeSpan.FromSeconds(1);
    private static readonly TimeSpan MetadataSafetyRefreshInterval =
        TimeSpan.FromSeconds(30);
    private static readonly TimeSpan CoverStabilizationDelay =
        TimeSpan.FromMilliseconds(250);
    private static readonly TimeSpan CoverRevalidationWindow =
        TimeSpan.FromSeconds(10);
    private static readonly TimeSpan CoverRevalidationInterval =
        TimeSpan.FromSeconds(1);
    private static readonly TimeSpan[] CoverRetryDelays =
    [
        TimeSpan.FromMilliseconds(250),
        TimeSpan.FromMilliseconds(500),
        TimeSpan.FromSeconds(1),
        TimeSpan.FromSeconds(2),
        TimeSpan.FromSeconds(5),
    ];

    private GlobalSystemMediaTransportControlsSessionManager manager;
    private GlobalSystemMediaTransportControlsSession? currentSession;
    private int sessionDirty = 1;
    private int metadataDirty = 1;
    private bool metadataAvailable;
    private uint cachedMediaId;
    private string cachedSourceAppId = string.Empty;
    private string cachedTitle = string.Empty;
    private string cachedArtist = string.Empty;
    private string cachedAlbum = string.Empty;
    private byte[]? cachedCover;
    private uint? cachedCoverCrc32;
    private DateTimeOffset lastMetadataRefresh;
    private DateTimeOffset nextCoverRetry;
    private DateTimeOffset coverValidationUntil;
    private int coverRetryAttempt;
    private DateTimeOffset lastVolumeRefresh;
    private byte cachedVolume = byte.MaxValue;
    private bool disposed;

    private WindowsMediaSessionProvider(
        GlobalSystemMediaTransportControlsSessionManager manager)
    {
        this.manager = manager;
        manager.CurrentSessionChanged += OnCurrentSessionChanged;
    }

    public static async Task<WindowsMediaSessionProvider> CreateAsync()
    {
        var manager =
            await GlobalSystemMediaTransportControlsSessionManager
                .RequestAsync();
        return new WindowsMediaSessionProvider(manager);
    }

    public async Task<MediaSessionPollResult> GetCurrentAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        try
        {
            RefreshCurrentSessionIfNeeded();
            var session = currentSession;
            if (session is null)
            {
                return MediaSessionPollResult.Absent();
            }

            var now = DateTimeOffset.UtcNow;
            var refreshRequested =
                Interlocked.Exchange(ref metadataDirty, 0) != 0;
            var safetyRefreshDue =
                now - lastMetadataRefresh >=
                MetadataSafetyRefreshInterval;
            var coverRetryDue =
                metadataAvailable &&
                now >= nextCoverRetry &&
                (cachedCover is null ||
                 now < coverValidationUntil);
            if (!metadataAvailable ||
                refreshRequested ||
                safetyRefreshDue ||
                coverRetryDue)
            {
                try
                {
                    await RefreshMetadataAsync(
                        session,
                        now,
                        refreshRequested ||
                            safetyRefreshDue ||
                            coverRetryDue,
                        cancellationToken);
                }
                catch
                {
                    Interlocked.Exchange(ref metadataDirty, 1);
                    throw;
                }
            }

            var playback = session.GetPlaybackInfo();
            var timeline = session.GetTimelineProperties();
            var playing =
                playback.PlaybackStatus ==
                GlobalSystemMediaTransportControlsSessionPlaybackStatus
                    .Playing;

            var duration = timeline.EndTime > timeline.StartTime
                ? timeline.EndTime - timeline.StartTime
                : timeline.EndTime;
            var position = timeline.Position > timeline.StartTime
                ? timeline.Position - timeline.StartTime
                : timeline.Position;

            if (playing)
            {
                position += GetElapsedPlayback(playback, timeline);
            }

            if (duration > TimeSpan.Zero && position > duration)
            {
                position = duration;
            }

            var durationMs = ToUIntMilliseconds(duration);

            if (now - lastVolumeRefresh >=
                VolumeRefreshInterval)
            {
                cachedVolume =
                    WindowsSystemVolume.GetCurrentOrUnknown();
                lastVolumeRefresh = now;
            }

            return MediaSessionPollResult.Available(
                new WindowsMediaSnapshot(
                    cachedMediaId,
                    cachedSourceAppId,
                    cachedTitle,
                    cachedArtist,
                    cachedAlbum,
                    ToUIntMilliseconds(position),
                    durationMs,
                    playing,
                    cachedVolume,
                    cachedCover,
                    cachedCoverCrc32));
        }
        catch (Exception exception)
            when (IsTransientMediaException(exception))
        {
            return MediaSessionPollResult.TransientFailure(
                exception);
        }
    }

    public async Task ReinitializeAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        DetachCurrentSession();
        manager.CurrentSessionChanged -= OnCurrentSessionChanged;
        manager =
            await GlobalSystemMediaTransportControlsSessionManager
                .RequestAsync();
        manager.CurrentSessionChanged += OnCurrentSessionChanged;
        ResetMetadata();
        Interlocked.Exchange(ref sessionDirty, 1);
        RefreshCurrentSessionIfNeeded();
    }

    public void Dispose()
    {
        if (disposed)
        {
            return;
        }

        disposed = true;
        DetachCurrentSession();
        manager.CurrentSessionChanged -= OnCurrentSessionChanged;
    }

    private async Task RefreshMetadataAsync(
        GlobalSystemMediaTransportControlsSession session,
        DateTimeOffset now,
        bool coverRefreshRequested,
        CancellationToken cancellationToken)
    {
        var properties = await session.TryGetMediaPropertiesAsync();
        cancellationToken.ThrowIfCancellationRequested();

        var sourceAppId = session.SourceAppUserModelId ?? string.Empty;
        var subtitle = ExcludeSourceAppId(
            properties.Subtitle,
            sourceAppId);
        var title = FirstNotEmpty(
            ExcludeSourceAppId(properties.Title, sourceAppId),
            "Название не указано");
        var artist = FirstNotEmpty(
            ExcludeSourceAppId(properties.Artist, sourceAppId),
            ExcludeSourceAppId(
                properties.AlbumArtist,
                sourceAppId),
            subtitle,
            "Исполнитель не указан");
        var album = FirstNotEmpty(
            ExcludeSourceAppId(
                properties.AlbumTitle,
                sourceAppId),
            !string.Equals(
                subtitle,
                artist,
                StringComparison.OrdinalIgnoreCase)
                ? subtitle
                : null,
            "Альбом не указан");
        var mediaId = ComputeMediaId(
            sourceAppId,
            title,
            artist,
            album);
        var mediaChanged =
            !metadataAvailable ||
            cachedMediaId != mediaId;

        if (mediaChanged)
        {
            cachedCover = null;
            cachedCoverCrc32 = null;
            coverRetryAttempt = 0;
            coverValidationUntil = now + CoverRevalidationWindow;
            nextCoverRetry = now + CoverStabilizationDelay;
        }
        else if (coverRefreshRequested &&
                 (cachedCover is not null ||
                  now >= nextCoverRetry))
        {
            var decodedCover = await DecodeCoverAsync(
                properties.Thumbnail,
                cancellationToken);
            if (decodedCover is not null)
            {
                cachedCover = decodedCover;
                cachedCoverCrc32 = Crc32.Compute(decodedCover);
                coverRetryAttempt = 0;
                nextCoverRetry = now < coverValidationUntil
                    ? now + CoverRevalidationInterval
                    : DateTimeOffset.MaxValue;
            }
            else if (cachedCover is null)
            {
                ScheduleCoverRetry(now);
            }
            else if (now < coverValidationUntil)
            {
                nextCoverRetry = now + CoverRevalidationInterval;
            }
        }

        cachedMediaId = mediaId;
        cachedSourceAppId = sourceAppId;
        cachedTitle = title;
        cachedArtist = artist;
        cachedAlbum = album;
        metadataAvailable = true;
        lastMetadataRefresh = now;
    }

    private void ScheduleCoverRetry(DateTimeOffset now)
    {
        var delay = CoverRetryDelays[
            Math.Min(
                coverRetryAttempt,
                CoverRetryDelays.Length - 1)];
        coverRetryAttempt++;
        nextCoverRetry = now + delay;
    }

    private void RefreshCurrentSessionIfNeeded()
    {
        if (Interlocked.Exchange(ref sessionDirty, 0) == 0)
        {
            return;
        }

        DetachCurrentSession();
        try
        {
            currentSession = manager.GetCurrentSession();
        }
        catch
        {
            Interlocked.Exchange(ref sessionDirty, 1);
            throw;
        }
        if (currentSession is not null)
        {
            currentSession.MediaPropertiesChanged +=
                OnMediaPropertiesChanged;
        }
        ResetMetadata();
    }

    private void DetachCurrentSession()
    {
        if (currentSession is not null)
        {
            currentSession.MediaPropertiesChanged -=
                OnMediaPropertiesChanged;
            currentSession = null;
        }
    }

    private void ResetMetadata()
    {
        metadataAvailable = false;
        cachedMediaId = 0;
        cachedSourceAppId = string.Empty;
        cachedTitle = string.Empty;
        cachedArtist = string.Empty;
        cachedAlbum = string.Empty;
        cachedCover = null;
        cachedCoverCrc32 = null;
        lastMetadataRefresh = DateTimeOffset.MinValue;
        nextCoverRetry = DateTimeOffset.MinValue;
        coverValidationUntil = DateTimeOffset.MinValue;
        coverRetryAttempt = 0;
        Interlocked.Exchange(ref metadataDirty, 1);
    }

    private void OnCurrentSessionChanged(
        GlobalSystemMediaTransportControlsSessionManager sender,
        CurrentSessionChangedEventArgs args)
    {
        Interlocked.Exchange(ref sessionDirty, 1);
    }

    private void OnMediaPropertiesChanged(
        GlobalSystemMediaTransportControlsSession sender,
        MediaPropertiesChangedEventArgs args)
    {
        Interlocked.Exchange(ref metadataDirty, 1);
    }

    private static bool IsTransientMediaException(
        Exception exception) =>
        exception is COMException or
            IOException or
            InvalidOperationException or
            UnauthorizedAccessException;

    private static async Task<byte[]?> DecodeCoverAsync(
        Windows.Storage.Streams.IRandomAccessStreamReference? thumbnail,
        CancellationToken cancellationToken)
    {
        if (thumbnail is null)
        {
            return null;
        }

        try
        {
            using var stream = await thumbnail.OpenReadAsync();
            var decoder = await BitmapDecoder.CreateAsync(stream);
            cancellationToken.ThrowIfCancellationRequested();

            const uint targetSize = 160;
            var sourceWidth = decoder.OrientedPixelWidth;
            var sourceHeight = decoder.OrientedPixelHeight;
            if (sourceWidth == 0 || sourceHeight == 0)
            {
                return null;
            }

            var scale = targetSize /
                        (double)Math.Min(sourceWidth, sourceHeight);
            var scaledWidth = Math.Max(
                targetSize,
                (uint)Math.Round(sourceWidth * scale));
            var scaledHeight = Math.Max(
                targetSize,
                (uint)Math.Round(sourceHeight * scale));

            var transform = new BitmapTransform
            {
                ScaledWidth = scaledWidth,
                ScaledHeight = scaledHeight,
                InterpolationMode = BitmapInterpolationMode.Fant,
                Bounds = new BitmapBounds
                {
                    X = (scaledWidth - targetSize) / 2U,
                    Y = (scaledHeight - targetSize) / 2U,
                    Width = targetSize,
                    Height = targetSize,
                },
            };

            var pixelData = await decoder.GetPixelDataAsync(
                BitmapPixelFormat.Bgra8,
                BitmapAlphaMode.Ignore,
                transform,
                ExifOrientationMode.RespectExifOrientation,
                ColorManagementMode.ColorManageToSRgb);
            cancellationToken.ThrowIfCancellationRequested();

            var bgra = pixelData.DetachPixelData();
            if (bgra.Length != targetSize * targetSize * 4U)
            {
                return null;
            }

            return ConvertBgraToRgb565(bgra);
        }
        catch (Exception exception)
            when (IsTransientMediaException(exception) ||
                  exception is ArgumentException)
        {
            Console.Error.WriteLine(
                $"[WARN] Не удалось декодировать обложку: " +
                exception.Message);
            return null;
        }
    }

    private static byte[] ConvertBgraToRgb565(byte[] bgra)
    {
        var rgb565 = new byte[bgra.Length / 2];
        for (var source = 0; source < bgra.Length; source += 4)
        {
            var blue = bgra[source];
            var green = bgra[source + 1];
            var red = bgra[source + 2];
            var pixel = (ushort)(
                (red & 0xF8) << 8 |
                (green & 0xFC) << 3 |
                blue >> 3);
            var destination = source / 2;
            rgb565[destination] = (byte)pixel;
            rgb565[destination + 1] = (byte)(pixel >> 8);
        }

        return rgb565;
    }

    private static TimeSpan GetElapsedPlayback(
        GlobalSystemMediaTransportControlsSessionPlaybackInfo playback,
        GlobalSystemMediaTransportControlsSessionTimelineProperties timeline)
    {
        var elapsed = DateTimeOffset.UtcNow - timeline.LastUpdatedTime;
        if (elapsed <= TimeSpan.Zero || elapsed > MaximumTimelineAge)
        {
            return TimeSpan.Zero;
        }

        var playbackRate = playback.PlaybackRate ?? 1.0;
        if (playbackRate <= 0 || playbackRate > 16)
        {
            playbackRate = 1.0;
        }

        return TimeSpan.FromTicks(
            (long)(elapsed.Ticks * playbackRate));
    }

    private static uint ToUIntMilliseconds(TimeSpan value)
    {
        if (value <= TimeSpan.Zero)
        {
            return 0;
        }

        return value.TotalMilliseconds >= uint.MaxValue
            ? uint.MaxValue
            : (uint)value.TotalMilliseconds;
    }

    private static string? ExcludeSourceAppId(
        string? value,
        string sourceAppId)
    {
        return string.Equals(
            value,
            sourceAppId,
            StringComparison.OrdinalIgnoreCase)
            ? null
            : value;
    }

    private static string FirstNotEmpty(params string?[] values)
    {
        foreach (var value in values)
        {
            if (!string.IsNullOrWhiteSpace(value))
            {
                return value.Trim();
            }
        }

        return string.Empty;
    }

    private static uint ComputeMediaId(
        string sourceAppId,
        string title,
        string artist,
        string album)
    {
        var hash = 2166136261U;
        AddHashText(ref hash, sourceAppId);
        AddHashText(ref hash, title);
        AddHashText(ref hash, artist);
        AddHashText(ref hash, album);
        return hash == 0 ? 1U : hash;
    }

    private static void AddHashText(ref uint hash, string value)
    {
        foreach (var octet in Encoding.UTF8.GetBytes(value))
        {
            hash ^= octet;
            hash *= 16777619U;
        }

        hash ^= 0xFFU;
        hash *= 16777619U;
    }
}
