using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace Rp2040MediaPanel.Companion.Runtime;

internal sealed class CompanionWindow : Form
{
    private const int MaximumDisplayedLogBytes = 256 * 1024;

    private readonly Label statusLabel;
    private readonly RichTextBox logBox;
    private readonly Button autostartButton;
    private readonly System.Windows.Forms.Timer refreshTimer;

    public CompanionWindow()
    {
        Text = "RP2040 Media Panel";
        Icon = SystemIcons.Application;
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new Size(620, 400);
        ClientSize = new Size(820, 520);

        statusLabel = new Label
        {
            AutoSize = true,
            Text = "Компаньон работает в фоновом режиме.",
        };

        var pathLabel = new Label
        {
            AutoEllipsis = true,
            Dock = DockStyle.Fill,
            Text = $"Журнал: {BackgroundLogSession.DefaultPath}",
        };

        logBox = new RichTextBox
        {
            BackColor = SystemColors.Window,
            DetectUrls = false,
            Dock = DockStyle.Fill,
            Font = new Font(
                FontFamily.GenericMonospace,
                9.0F),
            ReadOnly = true,
            WordWrap = false,
        };

        autostartButton = new Button
        {
            AutoSize = true,
        };
        autostartButton.Click += HandleAutostartClick;

        var refreshButton = new Button
        {
            AutoSize = true,
            Text = "Обновить",
        };
        refreshButton.Click += (_, _) => RefreshLog();

        var hideButton = new Button
        {
            AutoSize = true,
            Text = "Скрыть",
        };
        hideButton.Click += (_, _) => Hide();

        var buttons = new FlowLayoutPanel
        {
            AutoSize = true,
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.RightToLeft,
            WrapContents = false,
        };
        buttons.Controls.Add(hideButton);
        buttons.Controls.Add(autostartButton);
        buttons.Controls.Add(refreshButton);

        var layout = new TableLayoutPanel
        {
            ColumnCount = 1,
            Dock = DockStyle.Fill,
            Padding = new Padding(12),
            RowCount = 4,
        };
        layout.ColumnStyles.Add(
            new ColumnStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(
            new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.Controls.Add(statusLabel, 0, 0);
        layout.Controls.Add(pathLabel, 0, 1);
        layout.Controls.Add(logBox, 0, 2);
        layout.Controls.Add(buttons, 0, 3);
        Controls.Add(layout);

        refreshTimer = new System.Windows.Forms.Timer
        {
            Interval = 1000,
        };
        refreshTimer.Tick += (_, _) => RefreshLog();

        UpdateAutostartButton();
        RefreshLog();
    }

    protected override void OnFormClosing(
        FormClosingEventArgs eventArgs)
    {
        if (eventArgs.CloseReason == CloseReason.UserClosing)
        {
            eventArgs.Cancel = true;
            Hide();
            return;
        }

        base.OnFormClosing(eventArgs);
    }

    protected override void OnVisibleChanged(EventArgs eventArgs)
    {
        base.OnVisibleChanged(eventArgs);
        refreshTimer.Enabled = Visible;
        if (Visible)
        {
            UpdateAutostartButton();
            RefreshLog();
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            refreshTimer.Dispose();
        }

        base.Dispose(disposing);
    }

    private void HandleAutostartClick(
        object? sender,
        EventArgs eventArgs)
    {
        try
        {
            if (WindowsAutostart.IsInstalled())
            {
                WindowsAutostart.Remove();
                statusLabel.Text = "Автозагрузка отключена.";
            }
            else
            {
                WindowsAutostart.Install();
                statusLabel.Text = "Автозагрузка включена.";
            }

            UpdateAutostartButton();
        }
        catch (Exception exception)
            when (exception is InvalidOperationException or
                  UnauthorizedAccessException)
        {
            MessageBox.Show(
                this,
                exception.Message,
                "Автозагрузка",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
        }
    }

    private void UpdateAutostartButton()
    {
        try
        {
            autostartButton.Text = WindowsAutostart.IsInstalled()
                ? "Убрать из автозагрузки"
                : "Добавить в автозагрузку";
            autostartButton.Enabled = true;
        }
        catch (UnauthorizedAccessException)
        {
            autostartButton.Text = "Автозагрузка недоступна";
            autostartButton.Enabled = false;
        }
    }

    private void RefreshLog()
    {
        try
        {
            var path = BackgroundLogSession.DefaultPath;
            if (!File.Exists(path))
            {
                SetLogText("Журнал пока не создан.");
                return;
            }

            using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            var truncated =
                stream.Length > MaximumDisplayedLogBytes;
            if (truncated)
            {
                stream.Seek(
                    -MaximumDisplayedLogBytes,
                    SeekOrigin.End);
            }

            using var reader = new StreamReader(
                stream,
                Encoding.UTF8,
                detectEncodingFromByteOrderMarks: true);
            if (truncated)
            {
                _ = reader.ReadLine();
            }

            var text = reader.ReadToEnd();
            if (truncated)
            {
                text =
                    "… показан конец журнала …" +
                    Environment.NewLine +
                    text;
            }

            SetLogText(text);
        }
        catch (IOException exception)
        {
            statusLabel.Text =
                $"Не удалось прочитать журнал: {exception.Message}";
        }
    }

    private void SetLogText(string text)
    {
        if (string.Equals(
                logBox.Text,
                text,
                StringComparison.Ordinal))
        {
            return;
        }

        var wasAtEnd =
            logBox.TextLength == 0 ||
            logBox.SelectionStart >= logBox.TextLength - 2;
        logBox.Text = text;
        if (wasAtEnd)
        {
            logBox.SelectionStart = logBox.TextLength;
            logBox.ScrollToCaret();
        }
    }
}
