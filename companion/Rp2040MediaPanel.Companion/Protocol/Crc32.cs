namespace Rp2040MediaPanel.Companion.Protocol;

internal static class Crc32
{
    public static uint Compute(ReadOnlySpan<byte> data)
    {
        var crc = uint.MaxValue;
        foreach (var octet in data)
        {
            crc ^= octet;
            for (var bit = 0; bit < 8; bit++)
            {
                crc = (crc & 1U) != 0
                    ? crc >> 1 ^ 0xEDB88320U
                    : crc >> 1;
            }
        }

        return crc ^ uint.MaxValue;
    }
}
