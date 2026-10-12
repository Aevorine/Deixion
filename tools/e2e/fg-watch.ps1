# Foreground watcher for frameworks-e2e.mjs. A high-priority thread reads the real foreground window without sleeping and
# records every change (time, hwnd, pid, process, title). EVENT_SYSTEM_FOREGROUND is not used: an activation that the
# foreground lock stopped still fires that event although the foreground window did not change.
#   -Raise <hwnd>  first bring this window to the foreground (the "anchor" the user is assumed to be working in)
#   -Out <file>    JSON result, written when -Stop <file> appears (or after -Seconds)
param([long]$Raise = 0, [string]$Out, [string]$Stop, [int]$Seconds = 120)
Add-Type -TypeDefinition @'
using System; using System.Text; using System.Diagnostics; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Threading;
public static class DxFg {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  public class Ev { public double t; public long hwnd; public int pid; public string proc; public string title; }
  public static List<Ev> Log = new List<Ev>(); static volatile bool stop; static Thread th; static Stopwatch sw;
  public static void Raise(IntPtr h) { keybd_event(0x12, 0, 0, UIntPtr.Zero); SetForegroundWindow(h); keybd_event(0x12, 0, 2, UIntPtr.Zero); }
  public static void Start() {
    stop = false; sw = Stopwatch.StartNew();
    th = new Thread(() => {
      IntPtr last = new IntPtr(-1);
      while (!stop) {
        IntPtr h = GetForegroundWindow();
        if (h != last) {
          uint pid; GetWindowThreadProcessId(h, out pid);
          var sb = new StringBuilder(160); GetWindowText(h, sb, 160);
          string proc = "?"; try { proc = Process.GetProcessById((int)pid).ProcessName; } catch { }
          lock (Log) { Log.Add(new Ev { t = sw.Elapsed.TotalMilliseconds, hwnd = h.ToInt64(), pid = (int)pid, proc = proc, title = sb.ToString() }); }
          last = h;
        }
        Thread.Sleep(0);
      }
    });
    th.IsBackground = true; th.Priority = ThreadPriority.AboveNormal; th.Start();
  }
  public static double Stop() { stop = true; th.Join(1000); return sw.Elapsed.TotalMilliseconds; }
}
'@
$anchorOk = $true
if ($Raise -ne 0) {
  $anchorOk = $false
  foreach ($i in 1..20) {
    [DxFg]::Raise([IntPtr]$Raise)
    Start-Sleep -Milliseconds 150
    if ([DxFg]::GetForegroundWindow().ToInt64() -eq $Raise) { $anchorOk = $true; break }
  }
}
[DxFg]::Start()
Set-Content -LiteralPath "$Out.ready" -Value 'ready'
$end = (Get-Date).AddSeconds($Seconds)
while (-not (Test-Path -LiteralPath $Stop) -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 50 }
$total = [DxFg]::Stop()
@{ anchor_ok = $anchorOk; end_ms = $total; events = @([DxFg]::Log) } | ConvertTo-Json -Depth 4 -Compress | Set-Content -LiteralPath $Out -Encoding UTF8
