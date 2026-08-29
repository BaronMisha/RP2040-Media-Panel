namespace Rp2040MediaPanel.Companion.Media;

internal readonly record struct CoverIdentity(
    uint MediaId,
    uint Crc32);

internal sealed class MediaDeliveryState
{
    private static readonly TimeSpan FirstCoverRetryDelay =
        TimeSpan.FromMilliseconds(500);
    private static readonly TimeSpan SecondCoverRetryDelay =
        TimeSpan.FromSeconds(2);
    private static readonly TimeSpan ThirdCoverRetryDelay =
        TimeSpan.FromSeconds(5);
    private static readonly TimeSpan BackgroundCoverRetryDelay =
        TimeSpan.FromSeconds(30);

    private CoverIdentity? sentCover;
    private CoverIdentity? attemptedCover;
    private DateTimeOffset nextCoverAttempt;
    private int coverFailures;

    public uint? SentMediaId { get; private set; }

    public bool NeedsMedia(uint mediaId) =>
        SentMediaId != mediaId;

    public void MarkMediaSent(uint mediaId)
    {
        SentMediaId = mediaId;
    }

    public bool ShouldSendCover(
        WindowsMediaSnapshot snapshot,
        DateTimeOffset now,
        out CoverIdentity identity)
    {
        if (snapshot.CoverRgb565 is null ||
            snapshot.CoverCrc32 is not uint crc32)
        {
            identity = default;
            return false;
        }

        identity = new CoverIdentity(snapshot.MediaId, crc32);
        if (sentCover == identity)
        {
            return false;
        }

        if (attemptedCover != identity)
        {
            attemptedCover = identity;
            coverFailures = 0;
            nextCoverAttempt = DateTimeOffset.MinValue;
        }

        return now >= nextCoverAttempt;
    }

    public void MarkCoverSent(CoverIdentity identity)
    {
        sentCover = identity;
        attemptedCover = identity;
        coverFailures = 0;
        nextCoverAttempt = DateTimeOffset.MaxValue;
    }

    public int MarkCoverFailed(
        CoverIdentity identity,
        DateTimeOffset now)
    {
        if (attemptedCover != identity)
        {
            attemptedCover = identity;
            coverFailures = 0;
        }

        coverFailures++;
        nextCoverAttempt = coverFailures switch
        {
            1 => now + FirstCoverRetryDelay,
            2 => now + SecondCoverRetryDelay,
            3 => now + ThirdCoverRetryDelay,
            _ => now + BackgroundCoverRetryDelay,
        };
        return coverFailures;
    }

    public void Reset()
    {
        SentMediaId = null;
        sentCover = null;
        attemptedCover = null;
        coverFailures = 0;
        nextCoverAttempt = DateTimeOffset.MinValue;
    }
}

internal sealed class MediaSessionContinuityState
{
    internal static readonly TimeSpan AbsenceGracePeriod =
        TimeSpan.FromSeconds(2);
    internal static readonly TimeSpan TransientFailureGracePeriod =
        TimeSpan.FromSeconds(5);

    private DateTimeOffset? absentSince;
    private DateTimeOffset? transientFailureSince;
    private bool absenceReleaseIssued;

    public void MarkAvailable()
    {
        absentSince = null;
        transientFailureSince = null;
        absenceReleaseIssued = false;
    }

    public bool ShouldReleaseAbsent(DateTimeOffset now)
    {
        transientFailureSince = null;
        absentSince ??= now;
        if (absenceReleaseIssued ||
            now - absentSince < AbsenceGracePeriod)
        {
            return false;
        }

        absenceReleaseIssued = true;
        return true;
    }

    public bool ShouldRecoverTransientFailure(DateTimeOffset now)
    {
        absentSince = null;
        absenceReleaseIssued = false;
        transientFailureSince ??= now;
        if (now - transientFailureSince <
            TransientFailureGracePeriod)
        {
            return false;
        }

        transientFailureSince = now;
        return true;
    }
}