namespace Rp2040MediaPanel.Companion;

internal sealed record CompanionOptions(
    string? PortName,
    bool ListPorts,
    bool Watch,
    bool Simulate,
    bool MediaSession,
    bool Background,
    bool NoLyrics,
    bool InstallAutostart,
    bool RemoveAutostart,
    bool StopBackground,
    bool SelfTest,
    bool ShowHelp)
{
    public static bool TryParse(
        string[] args,
        out CompanionOptions options,
        out string? error)
    {
        string? portName = null;
        var listPorts = false;
        var watch = false;
        var simulate = false;
        var mediaSession = false;
        var background = false;
        var noLyrics = false;
        var installAutostart = false;
        var removeAutostart = false;
        var stopBackground = false;
        var selfTest = false;
        var showHelp = false;

        for (var index = 0; index < args.Length; index++)
        {
            var argument = args[index];
            switch (argument)
            {
                case "--port":
                    if (index + 1 >= args.Length)
                    {
                        options = Empty;
                        error = "После --port требуется имя COM-порта.";
                        return false;
                    }
                    portName = args[++index];
                    break;

                case "--list":
                    listPorts = true;
                    break;

                case "--once":
                    watch = false;
                    break;

                case "--watch":
                    watch = true;
                    break;

                case "--simulate":
                    simulate = true;
                    break;

                case "--media-session":
                    mediaSession = true;
                    break;

                case "--background":
                    background = true;
                    mediaSession = true;
                    break;

                case "--no-lyrics":
                    noLyrics = true;
                    break;

                case "--install-autostart":
                    installAutostart = true;
                    break;

                case "--remove-autostart":
                    removeAutostart = true;
                    break;

                case "--stop-background":
                    stopBackground = true;
                    break;

                case "--self-test":
                    selfTest = true;
                    break;

                case "--help":
                case "-h":
                    showHelp = true;
                    break;

                default:
                    if (argument.StartsWith("--port=", StringComparison.Ordinal))
                    {
                        portName = argument["--port=".Length..];
                        break;
                    }

                    options = Empty;
                    error = $"Неизвестный аргумент: {argument}";
                    return false;
            }
        }

        if (args.Length == 0)
        {
            background = true;
            mediaSession = true;
        }

        if (string.IsNullOrWhiteSpace(portName) && portName is not null)
        {
            options = Empty;
            error = "Имя COM-порта не может быть пустым.";
            return false;
        }
        var administrativeCommandCount =
            (installAutostart ? 1 : 0) +
            (removeAutostart ? 1 : 0) +
            (stopBackground ? 1 : 0);
        if (administrativeCommandCount > 1)
        {
            options = Empty;
            error =
                "Команды установки, удаления автозапуска и остановки " +
                "нельзя использовать одновременно.";
            return false;
        }

        options = new CompanionOptions(
            portName,
            listPorts,
            watch,
            simulate,
            mediaSession,
            background,
            noLyrics,
            installAutostart,
            removeAutostart,
            stopBackground,
            selfTest,
            showHelp);
        error = null;
        return true;
    }

    private static CompanionOptions Empty { get; } =
        new(
            null,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false);
}
