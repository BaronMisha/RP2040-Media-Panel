using System.Text;
using System.Text.Json;

namespace Rp2040MediaPanel.Companion.Configuration;

internal sealed record CompanionConfig(
    string? PreferredPort,
    bool LyricsEnabled)
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
    };

    public static CompanionConfig Default { get; } =
        new(null, true);

    public static string DefaultPath =>
        Path.Combine(
            Environment.GetFolderPath(
                Environment.SpecialFolder.LocalApplicationData),
            "RP2040MediaPanel",
            "config.json");

    public static CompanionConfig LoadOrCreate()
    {
        var path = DefaultPath;
        try
        {
            if (File.Exists(path))
            {
                return Parse(File.ReadAllText(path));
            }

            Directory.CreateDirectory(
                Path.GetDirectoryName(path)!);
            File.WriteAllText(
                path,
                JsonSerializer.Serialize(Default, JsonOptions),
                new UTF8Encoding(false));
        }
        catch (Exception exception)
            when (exception is IOException or
                  UnauthorizedAccessException or
                  JsonException or
                  NotSupportedException)
        {
            Console.Error.WriteLine(
                $"[WARN] Не удалось загрузить конфигурацию: " +
                exception.Message);
        }

        return Default;
    }

    internal static CompanionConfig Parse(string json)
    {
        var parsed =
            JsonSerializer.Deserialize<CompanionConfig>(
                json,
                JsonOptions) ??
            Default;
        return parsed with
        {
            PreferredPort =
                string.IsNullOrWhiteSpace(parsed.PreferredPort)
                    ? null
                    : parsed.PreferredPort.Trim(),
        };
    }
}
