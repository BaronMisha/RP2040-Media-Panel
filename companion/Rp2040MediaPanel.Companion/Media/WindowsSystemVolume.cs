using System.Runtime.InteropServices;

namespace Rp2040MediaPanel.Companion.Media;

internal static class WindowsSystemVolume
{
    private const uint ClsContextAll = 23;
    private static readonly Guid AudioEndpointVolumeId =
        new("5CDF2C82-841E-4546-9722-0CF74078229A");

    public static byte GetCurrentOrUnknown()
    {
        IMMDeviceEnumerator? enumerator = null;
        IMMDevice? device = null;
        IAudioEndpointVolume? endpointVolume = null;

        try
        {
            enumerator = (IMMDeviceEnumerator)(object)
                new MMDeviceEnumeratorComObject();
            ThrowIfFailed(
                enumerator.GetDefaultAudioEndpoint(
                    EDataFlow.Render,
                    ERole.Multimedia,
                    out device));

            var interfaceId = AudioEndpointVolumeId;
            ThrowIfFailed(
                device.Activate(
                    ref interfaceId,
                    ClsContextAll,
                    IntPtr.Zero,
                    out var activatedInterface));
            endpointVolume =
                (IAudioEndpointVolume)activatedInterface;

            ThrowIfFailed(
                endpointVolume.GetMasterVolumeLevelScalar(
                    out var scalar));
            ThrowIfFailed(endpointVolume.GetMute(out var muted));
            return ToPercentage(scalar, muted);
        }
        catch (COMException)
        {
            return byte.MaxValue;
        }
        catch (InvalidCastException)
        {
            return byte.MaxValue;
        }
        finally
        {
            ReleaseComObject(endpointVolume);
            ReleaseComObject(device);
            ReleaseComObject(enumerator);
        }
    }

    internal static byte ToPercentage(float scalar, bool muted)
    {
        if (muted)
        {
            return 0;
        }
        if (!float.IsFinite(scalar))
        {
            return byte.MaxValue;
        }

        return (byte)Math.Clamp(
            (int)Math.Round(
                scalar * 100.0F,
                MidpointRounding.AwayFromZero),
            0,
            100);
    }

    private static void ThrowIfFailed(int result)
    {
        Marshal.ThrowExceptionForHR(result);
    }

    private static void ReleaseComObject(object? instance)
    {
        if (instance is not null && Marshal.IsComObject(instance))
        {
            Marshal.FinalReleaseComObject(instance);
        }
    }

    private enum EDataFlow
    {
        Render = 0,
    }

    private enum ERole
    {
        Multimedia = 1,
    }

    [ComImport]
    [Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    private sealed class MMDeviceEnumeratorComObject;

    [ComImport]
    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDeviceEnumerator
    {
        [PreserveSig]
        int EnumAudioEndpoints(
            EDataFlow dataFlow,
            uint stateMask,
            out IntPtr devices);

        [PreserveSig]
        int GetDefaultAudioEndpoint(
            EDataFlow dataFlow,
            ERole role,
            out IMMDevice device);

        [PreserveSig]
        int GetDevice(
            [MarshalAs(UnmanagedType.LPWStr)] string id,
            out IMMDevice device);

        [PreserveSig]
        int RegisterEndpointNotificationCallback(
            IntPtr notificationClient);

        [PreserveSig]
        int UnregisterEndpointNotificationCallback(
            IntPtr notificationClient);
    }

    [ComImport]
    [Guid("D666063F-1587-4E43-81F1-B948E807363F")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDevice
    {
        [PreserveSig]
        int Activate(
            ref Guid interfaceId,
            uint classContext,
            IntPtr activationParameters,
            [MarshalAs(UnmanagedType.IUnknown)]
            out object activatedInterface);

        [PreserveSig]
        int OpenPropertyStore(uint access, out IntPtr properties);

        [PreserveSig]
        int GetId(
            [MarshalAs(UnmanagedType.LPWStr)] out string id);

        [PreserveSig]
        int GetState(out uint state);
    }

    [ComImport]
    [Guid("5CDF2C82-841E-4546-9722-0CF74078229A")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IAudioEndpointVolume
    {
        [PreserveSig]
        int RegisterControlChangeNotify(IntPtr callback);

        [PreserveSig]
        int UnregisterControlChangeNotify(IntPtr callback);

        [PreserveSig]
        int GetChannelCount(out uint channelCount);

        [PreserveSig]
        int SetMasterVolumeLevel(float levelDb, ref Guid eventContext);

        [PreserveSig]
        int SetMasterVolumeLevelScalar(
            float level,
            ref Guid eventContext);

        [PreserveSig]
        int GetMasterVolumeLevel(out float levelDb);

        [PreserveSig]
        int GetMasterVolumeLevelScalar(out float level);

        [PreserveSig]
        int SetChannelVolumeLevel(
            uint channel,
            float levelDb,
            ref Guid eventContext);

        [PreserveSig]
        int SetChannelVolumeLevelScalar(
            uint channel,
            float level,
            ref Guid eventContext);

        [PreserveSig]
        int GetChannelVolumeLevel(
            uint channel,
            out float levelDb);

        [PreserveSig]
        int GetChannelVolumeLevelScalar(
            uint channel,
            out float level);

        [PreserveSig]
        int SetMute(
            [MarshalAs(UnmanagedType.Bool)] bool muted,
            ref Guid eventContext);

        [PreserveSig]
        int GetMute(
            [MarshalAs(UnmanagedType.Bool)] out bool muted);

        [PreserveSig]
        int GetVolumeStepInfo(out uint step, out uint stepCount);

        [PreserveSig]
        int VolumeStepUp(ref Guid eventContext);

        [PreserveSig]
        int VolumeStepDown(ref Guid eventContext);

        [PreserveSig]
        int QueryHardwareSupport(out uint hardwareSupportMask);

        [PreserveSig]
        int GetVolumeRange(
            out float minimumDb,
            out float maximumDb,
            out float incrementDb);
    }
}
