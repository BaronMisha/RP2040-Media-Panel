namespace Rp2040MediaPanel.Companion.Runtime;

internal sealed class SingleInstanceLock : IDisposable
{
    private const string InstanceName =
        @"Local\RP2040MediaPanel.Companion.MediaSession";

    private readonly EventWaitHandle handle;

    private SingleInstanceLock(EventWaitHandle handle)
    {
        this.handle = handle;
    }

    public static bool TryAcquire(
        out SingleInstanceLock? instance)
    {
        var handle = new EventWaitHandle(
            false,
            EventResetMode.ManualReset,
            InstanceName,
            out var createdNew);
        if (!createdNew)
        {
            handle.Dispose();
            instance = null;
            return false;
        }

        instance = new SingleInstanceLock(handle);
        return true;
    }

    public static bool RequestStop()
    {
        try
        {
            using var existing =
                EventWaitHandle.OpenExisting(InstanceName);
            return existing.Set();
        }
        catch (WaitHandleCannotBeOpenedException)
        {
            return false;
        }
    }

    public IDisposable RegisterStop(Action callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        var registration =
            ThreadPool.RegisterWaitForSingleObject(
                handle,
                (_, _) => callback(),
                state: null,
                Timeout.Infinite,
                executeOnlyOnce: true);
        return new StopRegistration(registration);
    }

    public void Dispose()
    {
        handle.Dispose();
    }

    private sealed class StopRegistration : IDisposable
    {
        private readonly RegisteredWaitHandle registration;

        public StopRegistration(
            RegisteredWaitHandle registration)
        {
            this.registration = registration;
        }

        public void Dispose()
        {
            registration.Unregister(waitObject: null);
        }
    }
}
