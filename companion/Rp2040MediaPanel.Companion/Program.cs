using System.Text;
using Rp2040MediaPanel.Companion;
using Rp2040MediaPanel.Companion.Configuration;
using Rp2040MediaPanel.Companion.Protocol;
using Rp2040MediaPanel.Companion.Runtime;

Console.OutputEncoding = Encoding.UTF8;
var showWindow = args.Length == 0;

if (!CompanionOptions.TryParse(
        args,
        out var options,
        out var parseError))
{
    Console.Error.WriteLine($"[ERROR] {parseError}");
    PrintHelp();
    return 2;
}

if (options.ShowHelp)
{
    PrintHelp();
    return 0;
}

if (options.SelfTest)
{
    ProtocolSelfTest.Run();
    return 0;
}

if (options.InstallAutostart || options.RemoveAutostart)
{
    try
    {
        if (options.InstallAutostart)
        {
            WindowsAutostart.Install();
            Console.WriteLine(
                "[INFO] Автозапуск для текущего пользователя установлен.");
        }
        else
        {
            WindowsAutostart.Remove();
            Console.WriteLine(
                "[INFO] Автозапуск для текущего пользователя удалён.");
        }
        return 0;
    }
    catch (Exception exception)
        when (exception is InvalidOperationException or
              UnauthorizedAccessException)
    {
        Console.Error.WriteLine($"[ERROR] {exception.Message}");
        return 1;
    }
}

if (options.StopBackground)
{
    Console.WriteLine(
        SingleInstanceLock.RequestStop()
            ? "[INFO] Фоновому компаньону отправлена команда остановки."
            : "[INFO] Фоновый компаньон не запущен.");
    return 0;
}

if (options.ListPorts)
{
    PrintPorts(CompanionRunner.GetPorts());
    return 0;
}

using var cancellation = new CancellationTokenSource();
Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    cancellation.Cancel();
};

BackgroundLogSession? backgroundLog = null;
try
{
    if (options.Background)
    {
        backgroundLog = BackgroundLogSession.Start();
    }

    if (options.MediaSession)
    {
        var config = CompanionConfig.LoadOrCreate();
        if (!SingleInstanceLock.TryAcquire(
                out var singleInstance))
        {
            Console.Error.WriteLine(
                "[WARN] Компаньон уже запущен.");
            return 0;
        }
        var activeInstance = singleInstance!;
        using (activeInstance)
        using (activeInstance.RegisterStop(
                   cancellation.Cancel))
        using (var trayIcon = options.Background
                   ? TrayIconSession.Start(
                       cancellation.Cancel,
                       cancellation.Token,
                       showWindow)
                   : null)
        {
            await CompanionRunner.FollowWindowsMediaSession(
                options.PortName ?? config.PreferredPort,
                autoDiscover: options.PortName is null,
                lyricsEnabled:
                    config.LyricsEnabled && !options.NoLyrics,
                cancellation.Token);
        }
    }
    else
    {
        var ports = CompanionRunner.GetPorts();
        var portName = options.PortName;
        if (portName is null)
        {
            if (ports.Length != 1)
            {
                Console.Error.WriteLine(
                    "[ERROR] Укажите порт через --port COMxx.");
                PrintPorts(ports);
                return 2;
            }

            portName = ports[0];
        }

        if (options.Simulate)
        {
            await CompanionRunner.Simulate(
                portName,
                cancellation.Token);
        }
        else if (options.Watch)
        {
            await CompanionRunner.Watch(
                portName,
                cancellation.Token);
        }
        else
        {
            await CompanionRunner.RunOnce(
                portName,
                cancellation.Token);
        }
    }
    return 0;
}
catch (OperationCanceledException)
{
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine($"[ERROR] {exception.Message}");
    return 1;
}
finally
{
    backgroundLog?.Dispose();
}

static void PrintPorts(IEnumerable<string> ports)
{
    var portList = ports.ToArray();
    if (portList.Length == 0)
    {
        Console.WriteLine("COM-порты не найдены.");
        return;
    }

    Console.WriteLine("Доступные COM-порты:");
    foreach (var port in portList)
    {
        Console.WriteLine($"  {port}");
    }
}

static void PrintHelp()
{
    Console.WriteLine(
        """
        RP2040 Media Panel Companion

        Использование:
          без аргументов      запустить фоновый режим со значком в трее
          --list              показать доступные COM-порты
          --port COM20        выбрать порт устройства
          --once              HELLO, PING и STATUS, затем выход (по умолчанию)
          --watch             STATUS каждые 2 секунды с переподключением
          --simulate          передавать тестовый PC-трек до нажатия Ctrl+C
          --media-session     передавать медиасессию; без --port искать панель
          --background        media-session скрыто, с логом, треем и автопоиском
          --no-lyrics         не обращаться к сервису синхронизированных текстов
          --install-autostart установить фоновый запуск при входе в Windows
          --remove-autostart  удалить фоновый запуск при входе в Windows
          --stop-background   корректно остановить фоновый компаньон
          --self-test         проверить parser и runtime без устройства
          --help              показать справку

        Перед запуском закройте Serial Monitor: COM-порт открывается эксклюзивно.
        """);
}
