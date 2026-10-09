# 后台模式的核心承诺：不抢焦点。一个高优先级线程不停地读系统前台窗口（亚毫秒级），记录每一次变化，
# 同时跑完整的 MCP 套件，断言测试靶子（dx-testapp）从头到尾没有成为前台窗口。
# 不用 EVENT_SYSTEM_FOREGROUND：被前台锁拦下的激活也会发这个事件，但真正的前台窗口并没有变，会误报。
# -Isolated：用 --inproc 在脚本自己的进程里跑 build 里的引擎，不碰正在运行的 Deixion（比如已安装的版本）；不加则连上当前运行的 Deixion。两种模式都只结束项目目录下的进程。
param([int]$Runs = 4, [switch]$Isolated)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Add-Type -TypeDefinition @'
using System; using System.Text; using System.Diagnostics; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Threading;
public static class FgWatch {
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public static List<string> Log = new List<string>(); static volatile bool stop; static Thread th;
  static string Owner(IntPtr h){ uint pid; GetWindowThreadProcessId(h, out pid); try { return Process.GetProcessById((int)pid).ProcessName; } catch { return "?"; } }
  public static void Start(){
    lock(Log){ Log.Clear(); } stop = false;
    th = new Thread(() => {
      var sw = Stopwatch.StartNew(); IntPtr last = IntPtr.Zero;
      while (!stop) {
        IntPtr h = GetForegroundWindow();
        if (h != last) { var sb = new StringBuilder(128); GetWindowText(h, sb, 128); lock(Log){ Log.Add(sw.Elapsed.TotalMilliseconds.ToString("F1") + "ms [" + sb + "] <" + Owner(h) + ">"); } last = h; }
        Thread.Sleep(0);
      }
    });
    th.IsBackground = true; th.Priority = ThreadPriority.AboveNormal; th.Start();
  }
  public static string[] Stop(){ stop = true; th.Join(1000); lock(Log){ return Log.ToArray(); } }
}
'@
# 只结束可执行文件在本项目目录下的进程；真实安装的 Deixion 和当前会话的 MCP 服务不碰。
function Reset-Dx { Get-Process | Where-Object { $_.ProcessName -match '^(Deixion|deixion-cli|dx-testapp)$' -and $_.Path -and $_.Path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) } | Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 800 }
if ($Isolated) { $env:DX_E2E_ISOLATED = '1' }
$fails = 0
foreach ($run in 1..$Runs) {
  Reset-Dx
  [FgWatch]::Start()
  $o = node (Join-Path $PSScriptRoot 'mcp-e2e.mjs') 2>&1
  $log = [FgWatch]::Stop()
  $stolen = @($log | Where-Object { $_ -match '<dx-testapp>' })
  $suite = ($o | Select-String 'MCP RESULT').Line
  if ($stolen.Count) { $fails++; "FAIL  run $run : the test target became the foreground window ($($stolen.Count)x)"; $log | ForEach-Object { "      $_" } }
  else { "PASS  run $run : target never took the foreground ($($log.Count) foreground changes by the user or the harness) | $suite" }
}
Reset-Dx
"`n=== RESULT: $fails failure(s) ==="
exit $fails
