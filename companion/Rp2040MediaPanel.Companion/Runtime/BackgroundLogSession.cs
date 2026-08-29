using System.Runtime.InteropServices;
using System.Text;

namespace Rp2040MediaPanel.Companion.Runtime;

internal sealed class BackgroundLogSession : IDisposable
{
    private const long MaximumLogBytes = 2L * 1024 * 1024;
    private const int ArchiveCount = 5;

    private readonly StreamWriter writer;

    private BackgroundLogSession(StreamWriter writer)
    {
        this.writer = writer;
    }

    public static string DefaultPath =>
        Path.Combine(
            Environment.GetFolderPath(
                Environment.SpecialFolder.LocalApplicationData),
            "RP2040MediaPanel",
            "companion.log");

    public static BackgroundLogSession Start()
    {
        var path = DefaultPath;
        Directory.CreateDirectory(
            Path.GetDirectoryName(path)!);
        RotateIfNeeded(path);
        var stream = new FileStream(
            path,
            FileMode.Append,
            FileAccess.Write,
            FileShare.ReadWrite);
        var writer = new StreamWriter(
            stream,
            new UTF8Encoding(false))
        {
            AutoFlush = true,
        };
        var synchronized = TextWriter.Synchronized(writer);
        Console.SetOut(synchronized);
        Console.SetError(synchronized);
        Console.WriteLine();
        Console.WriteLine(
            $"[INFO] Запуск фонового компаньона: " +
            $"{DateTimeOffset.Now:O}");
        Console.WriteLine(
            $"[INFO] Companion version=" +
            $"{typeof(BackgroundLogSession).Assembly.GetName().Version} " +
            $"path={Environment.ProcessPath ?? "unknown"}");
        FreeConsole();
        return new BackgroundLogSession(writer);
    }

    public void Dispose()
    {
        Console.SetOut(TextWriter.Null);
        Console.SetError(TextWriter.Null);
        writer.Dispose();
    }

    private static void RotateIfNeeded(string path)
    {
        var current = new FileInfo(path);
        if (!current.Exists || current.Length < MaximumLogBytes)
        {
            return;
        }

        var oldest = $"{path}.{ArchiveCount}";
        if (File.Exists(oldest))
        {
            File.Delete(oldest);
        }
        for (var index = ArchiveCount - 1; index >= 1; index--)
        {
            var source = $"{path}.{index}";
            if (File.Exists(source))
            {
                File.Move(source, $"{path}.{index + 1}");
            }
        }
        File.Move(path, $"{path}.1");
    }

    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool FreeConsole();
}