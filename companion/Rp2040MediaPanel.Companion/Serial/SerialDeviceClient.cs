using System.Diagnostics;
using System.IO.Ports;
using Rp2040MediaPanel.Companion.Protocol;

namespace Rp2040MediaPanel.Companion.Serial;

internal sealed class SerialDeviceClient : IDisposable
{
    private const int BaudRate = 115200;
    private const int CoverWidth = 160;
    private const int CoverHeight = 160;
    private const int CoverByteCount = CoverWidth * CoverHeight * 2;
    private static readonly TimeSpan DefaultResponseTimeout =
        TimeSpan.FromSeconds(2);

    private readonly SerialPort port;
    private readonly TimeSpan responseTimeout;
    private ushort nextSequence = 1;
    private int protocolVersion = 1;
    private bool supportsRawCover;

    public SerialDeviceClient(
        string portName,
        TimeSpan? responseTimeout = null)
    {
        port = new SerialPort(
            portName,
            BaudRate,
            Parity.None,
            8,
            StopBits.One)
        {
            NewLine = "\n",
            ReadTimeout = 250,
            WriteTimeout = 2500,
            DtrEnable = true,
            RtsEnable = false,
            Encoding = System.Text.Encoding.UTF8,
        };
        this.responseTimeout = responseTimeout ?? DefaultResponseTimeout;
    }

    public string PortName => port.PortName;
    public bool IsOpen => port.IsOpen;
    public int ProtocolVersion => protocolVersion;
    public bool SupportsRawCover => supportsRawCover;

    public void Open()
    {
        port.Open();
        port.DiscardInBuffer();
        port.DiscardOutBuffer();
    }

    public DeviceHello Hello(
        CancellationToken cancellationToken,
        bool logTraffic = true)
    {
        try
        {
            var response = Exchange(
                "HELLO",
                "HELLO",
                cancellationToken,
                sequence => RpmpProtocol.CreateVersionedRequest(
                    2,
                    "HELLO",
                    sequence),
                logTraffic);
            var hello = RpmpProtocol.ParseHello(response);
            protocolVersion = hello.ProtocolVersion;
            supportsRawCover = hello.SupportsRawCover;
            return hello;
        }
        catch (Exception exception)
            when (exception is TimeoutException or ProtocolException)
        {
            if (logTraffic)
            {
                Console.WriteLine(
                    $"[INFO] RPMP2 unavailable ({exception.Message}); " +
                    "fallback to RPMP1.");
            }
            port.DiscardInBuffer();
        }

        protocolVersion = 1;
        supportsRawCover = false;
        var fallback = Exchange(
            "HELLO",
            "HELLO",
            cancellationToken,
            sequence => RpmpProtocol.CreateVersionedRequest(
                1,
                "HELLO",
                sequence),
            logTraffic);
        return RpmpProtocol.ParseHello(fallback);
    }

    public TimeSpan Ping(CancellationToken cancellationToken)
    {
        var timer = Stopwatch.StartNew();
        var response = Exchange(
            "PING",
            "ACK",
            cancellationToken);
        timer.Stop();
        RpmpProtocol.ValidatePong(response);
        return timer.Elapsed;
    }

    public DeviceStatus GetStatus(CancellationToken cancellationToken)
    {
        var response = Exchange(
            "STATUS",
            "STATUS",
            cancellationToken);
        return RpmpProtocol.ParseStatus(response);
    }

    public void SendMedia(
        uint mediaId,
        uint durationMs,
        string title,
        string artist,
        string album,
        CancellationToken cancellationToken)
    {
        var response = Exchange(
            "MEDIA",
            "ACK",
            cancellationToken,
            sequence => RpmpProtocol.CreateMediaRequest(
                sequence,
                mediaId,
                durationMs,
                title,
                artist,
                album,
                protocolVersion));
        RpmpProtocol.ValidateAck(response, "MEDIA");
    }

    public void SendState(
        uint positionMs,
        uint durationMs,
        byte volume,
        bool playing,
        CancellationToken cancellationToken)
    {
        var response = Exchange(
            "STATE",
            "ACK",
            cancellationToken,
            sequence => RpmpProtocol.CreateStateRequest(
                sequence,
                positionMs,
                durationMs,
                volume,
                playing,
                protocolVersion),
            logTraffic: false);
        RpmpProtocol.ValidateAck(response, "STATE");
    }

    public void Release(CancellationToken cancellationToken)
    {
        var response = Exchange(
            "RELEASE",
            "ACK",
            cancellationToken,
            sequence => RpmpProtocol.CreateReleaseRequest(
                sequence,
                protocolVersion));
        RpmpProtocol.ValidateAck(response, "RELEASE");
    }

    public void SendKaraoke(
        uint mediaId,
        uint lineStartMs,
        uint lineEndMs,
        string previousLine,
        string currentLine,
        string nextLine,
        CancellationToken cancellationToken)
    {
        var response = Exchange(
            "KARAOKE",
            "ACK",
            cancellationToken,
            sequence => RpmpProtocol.CreateKaraokeRequest(
                sequence,
                mediaId,
                lineStartMs,
                lineEndMs,
                previousLine,
                currentLine,
                nextLine,
                protocolVersion),
            logTraffic: false);
        RpmpProtocol.ValidateAck(response, "KARAOKE");
    }

    public void ClearKaraoke(
        uint mediaId,
        CancellationToken cancellationToken)
    {
        var response = Exchange(
            "KARAOKE_CLEAR",
            "ACK",
            cancellationToken,
            sequence => RpmpProtocol.CreateKaraokeClearRequest(
                sequence,
                mediaId,
                protocolVersion),
            logTraffic: false);
        RpmpProtocol.ValidateAck(response, "KARAOKE_CLEAR");
    }

    public void SendCover(
        uint mediaId,
        byte[] rgb565,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(rgb565);
        if (rgb565.Length != CoverByteCount)
        {
            throw new ArgumentException(
                $"Cover must contain {CoverByteCount} RGB565 bytes.",
                nameof(rgb565));
        }

        if (protocolVersion == 2 && supportsRawCover)
        {
            SendRawCover(mediaId, rgb565, cancellationToken);
            return;
        }

        SendRpmp1Cover(mediaId, rgb565, cancellationToken);
    }

    public void Dispose()
    {
        port.Dispose();
    }

    private void SendRawCover(
        uint mediaId,
        byte[] rgb565,
        CancellationToken cancellationToken)
    {
        var timer = Stopwatch.StartNew();
        var sequence = TakeSequence();
        var crc32 = Crc32.Compute(rgb565);
        var request = RpmpProtocol.CreateCoverStreamRequest(
            sequence,
            mediaId,
            CoverWidth,
            CoverHeight,
            CoverByteCount,
            crc32);
        WriteRequest(request, logTraffic: true);
        var ready = ReadResponse(
            sequence,
            "ACK",
            "COVER_STREAM",
            cancellationToken,
            logTraffic: true);
        RpmpProtocol.ValidateAck(ready, "COVER_READY");

        cancellationToken.ThrowIfCancellationRequested();
        port.Write(rgb565, 0, rgb565.Length);
        var completed = ReadResponse(
            sequence,
            "ACK",
            "COVER_STREAM",
            cancellationToken,
            logTraffic: true);
        RpmpProtocol.ValidateAck(completed, "COVER_END");
        LogCoverCompleted(timer, "RPMP2/raw");
    }

    private void SendRpmp1Cover(
        uint mediaId,
        byte[] rgb565,
        CancellationToken cancellationToken)
    {
        var timer = Stopwatch.StartNew();
        var crc32 = Crc32.Compute(rgb565);
        var beginResponse = Exchange(
            "COVER_BEGIN",
            "ACK",
            cancellationToken,
            sequence => RpmpProtocol.CreateCoverBeginRequest(
                sequence,
                mediaId,
                CoverWidth,
                CoverHeight,
                CoverByteCount,
                crc32,
                protocolVersion));
        RpmpProtocol.ValidateAck(beginResponse, "COVER_BEGIN");

        var lastReportedPercent = 0;
        for (var offset = 0; offset < rgb565.Length;
             offset += RpmpProtocol.CoverChunkBytes)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var length = Math.Min(
                RpmpProtocol.CoverChunkBytes,
                rgb565.Length - offset);
            var dataResponse = Exchange(
                "COVER_DATA",
                "ACK",
                cancellationToken,
                sequence => RpmpProtocol.CreateCoverDataRequest(
                    sequence,
                    mediaId,
                    (uint)offset,
                    rgb565.AsSpan(offset, length),
                    protocolVersion),
                logTraffic: false);
            RpmpProtocol.ValidateAck(dataResponse, "COVER_DATA");

            var percent = (offset + length) * 100 / rgb565.Length;
            if (percent >= lastReportedPercent + 10 || percent == 100)
            {
                lastReportedPercent = percent;
                Console.WriteLine($"[COVER] Transferred {percent}%");
            }
        }

        var endResponse = Exchange(
            "COVER_END",
            "ACK",
            cancellationToken,
            sequence => RpmpProtocol.CreateCoverEndRequest(
                sequence,
                mediaId,
                protocolVersion));
        RpmpProtocol.ValidateAck(endResponse, "COVER_END");
        LogCoverCompleted(timer, "RPMP1/base64");
    }

    private void LogCoverCompleted(Stopwatch timer, string transport)
    {
        timer.Stop();
        var kibPerSecond =
            CoverByteCount / 1024.0 /
            Math.Max(timer.Elapsed.TotalSeconds, 0.001);
        Console.WriteLine(
            $"[COVER] Completed via {transport} in " +
            $"{timer.Elapsed.TotalSeconds:0.00}s, " +
            $"{kibPerSecond:0.0} KiB/s");
    }

    private RpmpMessage Exchange(
        string command,
        string expectedResponse,
        CancellationToken cancellationToken,
        Func<ushort, string>? requestFactory = null,
        bool logTraffic = true)
    {
        var sequence = TakeSequence();
        var request = requestFactory is null
            ? RpmpProtocol.CreateVersionedRequest(
                protocolVersion,
                command,
                sequence)
            : requestFactory(sequence);
        WriteRequest(request, logTraffic);
        return ReadResponse(
            sequence,
            expectedResponse,
            command,
            cancellationToken,
            logTraffic);
    }

    private void WriteRequest(string request, bool logTraffic)
    {
        port.WriteLine(request);
        if (logTraffic)
        {
            Console.WriteLine($"> {request}");
        }
    }

    private RpmpMessage ReadResponse(
        ushort sequence,
        string expectedResponse,
        string command,
        CancellationToken cancellationToken,
        bool logTraffic)
    {
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < responseTimeout)
        {
            cancellationToken.ThrowIfCancellationRequested();
            string line;
            try
            {
                line = port.ReadLine().TrimEnd('\r');
            }
            catch (TimeoutException)
            {
                continue;
            }

            if (!RpmpProtocol.TryParse(line, out var message))
            {
                if (logTraffic)
                {
                    Console.WriteLine($"  {line}");
                }
                continue;
            }
            if (logTraffic)
            {
                Console.WriteLine($"< {line}");
            }
            if (message!.Sequence != sequence)
            {
                continue;
            }

            RpmpProtocol.ThrowIfError(message);
            if (message.Type == expectedResponse)
            {
                return message;
            }
        }

        throw new TimeoutException(
            $"No {expectedResponse} response to {command} from " +
            $"{PortName} within {responseTimeout.TotalSeconds:0.##}s.");
    }

    private ushort TakeSequence()
    {
        var sequence = nextSequence;
        nextSequence = nextSequence == ushort.MaxValue
            ? (ushort)1
            : (ushort)(nextSequence + 1);
        return sequence;
    }
}