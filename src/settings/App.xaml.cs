using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;

namespace WorldClockSettings;

public partial class App : Application
{
    // Two Settings windows would each save what they loaded and revert one
    // another (#24), so only one runs; a second launch brings it forward.
    // Not the gadget's "WorldClockGadget_SingleInstance": the installer's
    // AppMutex watches that name.
    private const string MutexName = "WorldClockSettings_SingleInstance";
    private Mutex? _instanceMutex;

    protected override void OnStartup(StartupEventArgs e)
    {
        _instanceMutex = new Mutex(true, MutexName, out bool createdNew);
        if (!createdNew)
        {
            _instanceMutex.Dispose();
            _instanceMutex = null;
            ActivateRunningInstance();
            Shutdown();
            return;
        }
        base.OnStartup(e);
        // Created here rather than through StartupUri, so a second instance
        // never builds (and loads config into) a window it would only discard.
        new MainWindow().Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        if (_instanceMutex != null)
        {
            _instanceMutex.ReleaseMutex();
            _instanceMutex.Dispose();
        }
        base.OnExit(e);
    }

    private static void ActivateRunningInstance()
    {
        using var self = Process.GetCurrentProcess();
        foreach (var p in Process.GetProcessesByName(self.ProcessName))
        {
            using (p)
            {
                if (p.Id == self.Id) continue;
                var hwnd = p.MainWindowHandle;
                if (hwnd == IntPtr.Zero) continue;
                if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                return;
            }
        }
    }

    private const int SW_RESTORE = 9;

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    private static extern bool IsIconic(IntPtr hWnd);
}
