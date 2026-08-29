using System.Drawing;
using System.Windows.Forms;

namespace Rp2040MediaPanel.Companion.Runtime;

internal sealed class TrayIconSession : IDisposable
{
    private readonly Action requestStop;
    private readonly CancellationToken cancellationToken;
    private readonly ManualResetEventSlim ready = new(false);
    private readonly Thread thread;
    private TrayApplicationContext? context;
    private Exception? startupException;
    private bool disposed;

    private TrayIconSession(
        Action requestStop,
        CancellationToken cancellationToken,
        bool showWindow)
    {
        this.requestStop = requestStop;
        this.cancellationToken = cancellationToken;
        thread = new Thread(
            () => RunMessageLoop(showWindow))
        {
            IsBackground = true,
            Name = "RP2040 Media Panel Tray",
        };
        thread.SetApartmentState(ApartmentState.STA);
    }

    public static TrayIconSession Start(
        Action requestStop,
        CancellationToken cancellationToken,
        bool showWindow)
    {
        ArgumentNullException.ThrowIfNull(requestStop);
        var session = new TrayIconSession(
            requestStop,
            cancellationToken,
            showWindow);
        session.thread.Start();
        session.ready.Wait();

        if (session.startupException is not null)
        {
            session.thread.Join();
            session.ready.Dispose();
            throw new InvalidOperationException(
                "Не удалось создать значок в системном трее.",
                session.startupException);
        }

        return session;
    }

    public void Dispose()
    {
        if (disposed)
        {
            return;
        }

        disposed = true;
        Volatile.Read(ref context)?.RequestExit();
        if (Thread.CurrentThread != thread &&
            !thread.Join(TimeSpan.FromSeconds(5)))
        {
            Console.Error.WriteLine(
                "[WARN] Поток значка в трее не завершился вовремя.");
        }
        ready.Dispose();
    }

    private void RunMessageLoop(bool showWindow)
    {
        var published = false;
        try
        {
            Application.SetHighDpiMode(
                HighDpiMode.SystemAware);
            using var localContext =
                new TrayApplicationContext(
                    requestStop,
                    cancellationToken,
                    showWindow);
            Volatile.Write(ref context, localContext);
            published = true;
            ready.Set();
            Application.Run(localContext);
        }
        catch (Exception exception)
        {
            startupException = exception;
            if (published)
            {
                Console.Error.WriteLine(
                    $"[ERROR] Ошибка значка в трее: " +
                    exception.Message);
                requestStop();
            }
        }
        finally
        {
            Volatile.Write(ref context, null);
            ready.Set();
        }
    }

    private sealed class TrayApplicationContext :
        ApplicationContext
    {
        private readonly Action requestStop;
        private readonly Control dispatcher;
        private readonly ContextMenuStrip menu;
        private readonly ToolStripMenuItem openItem;
        private readonly ToolStripMenuItem exitItem;
        private readonly NotifyIcon notifyIcon;
        private readonly CancellationTokenRegistration
            cancellationRegistration;
        private CompanionWindow? window;
        private bool exitRequested;

        public TrayApplicationContext(
            Action requestStop,
            CancellationToken cancellationToken,
            bool showWindow)
        {
            this.requestStop = requestStop;
            dispatcher = new Control();
            _ = dispatcher.Handle;

            openItem = new ToolStripMenuItem("Открыть");
            openItem.Click += HandleOpenClick;

            exitItem = new ToolStripMenuItem(
                "Отключить и выйти");
            exitItem.Click += HandleExitClick;

            menu = new ContextMenuStrip();
            menu.Items.Add(openItem);
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add(exitItem);

            notifyIcon = new NotifyIcon
            {
                ContextMenuStrip = menu,
                Icon = SystemIcons.Application,
                Text = "RP2040 Media Panel",
                Visible = true,
            };
            notifyIcon.DoubleClick += HandleOpenClick;

            cancellationRegistration =
                cancellationToken.Register(RequestExit);
            Console.WriteLine(
                "[INFO] Значок компаньона добавлен в системный трей.");
            if (showWindow)
            {
                ShowWindow();
            }
        }

        public void RequestExit()
        {
            if (dispatcher.IsDisposed)
            {
                return;
            }

            try
            {
                if (dispatcher.InvokeRequired)
                {
                    dispatcher.BeginInvoke(
                        (Action)ExitThread);
                }
                else
                {
                    ExitThread();
                }
            }
            catch (InvalidOperationException)
            {
                // The UI message loop is already stopping.
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                cancellationRegistration.Dispose();
                window?.Dispose();
                notifyIcon.Visible = false;
                notifyIcon.Dispose();
                menu.Dispose();
                dispatcher.Dispose();
            }

            base.Dispose(disposing);
        }

        private void HandleExitClick(
            object? sender,
            EventArgs eventArgs)
        {
            if (exitRequested)
            {
                return;
            }

            exitRequested = true;
            exitItem.Enabled = false;
            notifyIcon.Text = "RP2040 Media Panel — остановка";
            requestStop();
        }

        private void HandleOpenClick(
            object? sender,
            EventArgs eventArgs)
        {
            ShowWindow();
        }

        private void ShowWindow()
        {
            if (window is null || window.IsDisposed)
            {
                window = new CompanionWindow();
            }

            if (window.WindowState == FormWindowState.Minimized)
            {
                window.WindowState = FormWindowState.Normal;
            }
            window.Show();
            window.Activate();
        }
    }
}
