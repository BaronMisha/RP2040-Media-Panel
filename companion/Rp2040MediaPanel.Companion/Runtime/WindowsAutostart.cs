using Microsoft.Win32;

namespace Rp2040MediaPanel.Companion.Runtime;

internal static class WindowsAutostart
{
    private const string RegistryPath =
        @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName =
        "RP2040 Media Panel Companion";

    public static void Install()
    {
        var executablePath =
            Environment.ProcessPath ??
            throw new InvalidOperationException(
                "Путь исполняемого файла недоступен.");
        if (string.Equals(
                Path.GetFileNameWithoutExtension(executablePath),
                "dotnet",
                StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException(
                "Автозапуск устанавливается из собранного EXE, " +
                "а не через dotnet run.");
        }

        using var key = Registry.CurrentUser.CreateSubKey(
            RegistryPath,
            writable: true);
        key.SetValue(
            ValueName,
            BuildCommand(executablePath),
            RegistryValueKind.String);
    }

    public static void Remove()
    {
        using var key = Registry.CurrentUser.OpenSubKey(
            RegistryPath,
            writable: true);
        key?.DeleteValue(ValueName, throwOnMissingValue: false);
    }

    public static bool IsInstalled()
    {
        using var key = Registry.CurrentUser.OpenSubKey(
            RegistryPath,
            writable: false);
        return key?.GetValue(ValueName) is string;
    }

    internal static string BuildCommand(string executablePath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(executablePath);
        return $"\"{executablePath}\" --background";
    }
}
