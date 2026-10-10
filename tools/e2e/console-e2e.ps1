# 经典控制台（conhost）里的 PowerShell：后台模式连续输入逐字正确，并且对最小化的窗口做 restore / maximize / minimize 时不抢用户的前台。
# 全程隔离：CLI 复制到 .scratch 下并放 portable.flag，用 --inproc 在自己的进程里跑引擎，不碰正在运行的 Deixion、真实设置与数据，
# 只结束自己拉起的进程（按 PID）。-Cli 可换成别的 deixion-cli.exe，用来对照旧版本。
param([string]$Cli = '', [int]$Runs = 3)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Cli) { $Cli = Join-Path $root 'build\deixion-cli.exe' }
$testapp = Join-Path $root 'build\dx-testapp.exe'
foreach ($f in $Cli, $testapp) { if (-not (Test-Path -LiteralPath $f)) { throw "missing $f; build first" } }

Add-Type -TypeDefinition @'
using System; using System.Text; using System.Diagnostics; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Threading;
public static class Win {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  delegate bool EnumProc(IntPtr h, IntPtr l);
  public static IntPtr[] Find(uint pid, string cls) {
    var r = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid) { var sb = new StringBuilder(128); GetClassName(h, sb, 128); if (sb.ToString() == cls) r.Add(h); } return true; }, IntPtr.Zero);
    return r.ToArray();
  }
  public static IntPtr[] FindClass(string cls) {
    var r = new List<IntPtr>();
    EnumWindows((h, l) => { var sb = new StringBuilder(128); GetClassName(h, sb, 128); if (sb.ToString() == cls) r.Add(h); return true; }, IntPtr.Zero);
    return r.ToArray();
  }
  public static string Desc(IntPtr h) {
    uint pid; GetWindowThreadProcessId(h, out pid);
    var c = new StringBuilder(128); GetClassName(h, c, 128); var t = new StringBuilder(128); GetWindowText(h, t, 128);
    string proc = "?"; try { proc = Process.GetProcessById((int)pid).ProcessName; } catch { }
    return "0x" + h.ToInt64().ToString("X") + " [" + t + "] <" + proc + "/" + c + ">";
  }
  public static void Raise(IntPtr h) { keybd_event(0x12, 0, 0, UIntPtr.Zero); SetForegroundWindow(h); keybd_event(0x12, 0, 2, UIntPtr.Zero); }
  public static List<string> Log = new List<string>(); static volatile bool stop; static Thread th;
  public static void Start() {
    lock (Log) { Log.Clear(); } stop = false;
    th = new Thread(() => {
      var sw = Stopwatch.StartNew(); IntPtr last = IntPtr.Zero;
      while (!stop) {
        IntPtr h = GetForegroundWindow();
        if (h != last) { lock (Log) { Log.Add(sw.Elapsed.TotalMilliseconds.ToString("F1") + "ms " + Desc(h)); } last = h; }
        Thread.Sleep(0);
      }
    });
    th.IsBackground = true; th.Priority = ThreadPriority.AboveNormal; th.Start();
  }
  public static string[] Stop() { stop = true; th.Join(1000); lock (Log) { return Log.ToArray(); } }
}
'@

$fails = 0
function Check($name, $ok, $detail = '') { if (-not $ok) { $script:fails++ }; "{0}  {1} {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail }
$work = Join-Path $root ('.scratch\console-e2e-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $work | Out-Null
$cliCopy = Join-Path $work 'deixion-cli.exe'
Copy-Item -LiteralPath $Cli -Destination $cliCopy
New-Item -ItemType File (Join-Path $work 'portable.flag') | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding $false

function Dx($method, $obj) {
  $ErrorActionPreference = 'Continue'
  $f = Join-Path $work 'call.json'
  [IO.File]::WriteAllText($f, ($obj | ConvertTo-Json -Compress -Depth 6), $utf8)
  $out = & $cliCopy --inproc call $method "@$f" 2>&1
  [pscustomobject]@{ Ok = ($LASTEXITCODE -eq 0); Text = (($out | ForEach-Object { "$_" }) -join ' ') }
}
function Wait-File($p, $ms = 8000) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($sw.ElapsedMilliseconds -lt $ms) { if (Test-Path -LiteralPath $p) { Start-Sleep -Milliseconds 150; return Get-Content -LiteralPath $p -Raw } Start-Sleep -Milliseconds 100 }
  return $null
}

$procs = @()
try {
  "cli under test: $Cli  ($(& $cliCopy version))"
  $user = Start-Process -FilePath $testapp -ArgumentList ('"' + (Join-Path $work 'user-window.json') + '"') -PassThru
  $procs += $user
  $before = @([Win]::FindClass('ConsoleWindowClass') | ForEach-Object { $_.ToInt64() })
  $con = Start-Process -FilePath 'conhost.exe' -ArgumentList 'powershell.exe', '-NoProfile', '-NoLogo' -WorkingDirectory $work -PassThru
  $procs += $con
  $uh = [IntPtr]::Zero; $ch = [IntPtr]::Zero
  foreach ($i in 1..100) {
    if ($uh -eq [IntPtr]::Zero) { $a = [Win]::Find([uint32]$user.Id, 'DxTestTarget'); if ($a.Count) { $uh = $a[0] } }
    if ($ch -eq [IntPtr]::Zero) { $a = @([Win]::FindClass('ConsoleWindowClass') | Where-Object { $before -notcontains $_.ToInt64() }); if ($a.Count) { $ch = $a[0] } }
    if ($uh -ne [IntPtr]::Zero -and $ch -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 100
  }
  if ($ch -eq [IntPtr]::Zero) { throw 'the classic console window did not appear' }
  if ($uh -eq [IntPtr]::Zero) { throw 'the stand-in user window (dx-testapp) did not appear' }
  $chs = '0x{0:X}' -f $ch.ToInt64()
  "console target $([Win]::Desc($ch))"
  "user window    $([Win]::Desc($uh))"
  Start-Sleep -Milliseconds 2500

  [Win]::Raise($uh); Start-Sleep -Milliseconds 400
  Check 'setup: the stand-in user window is in front' ([Win]::GetForegroundWindow() -eq $uh) ([Win]::Desc([Win]::GetForegroundWindow()))

  # 对照：不经 Deixion，直接对最小化的控制台 ShowWindow(SW_RESTORE)，它会不会把自己顶成前台。
  # 会的话说明下面“前台不变”的断言抓得到抢占；不会的话这次对照没有说服力，只提示不判失败。
  [void][Win]::ShowWindow($ch, 6); Start-Sleep -Milliseconds 500
  [Win]::Raise($uh); Start-Sleep -Milliseconds 400
  [void][Win]::ShowWindow($ch, 9); Start-Sleep -Milliseconds 600
  $ctl = [Win]::GetForegroundWindow()
  "INFO  control: a plain ShowWindow(SW_RESTORE) on the minimized console leaves the foreground at $([Win]::Desc($ctl)) " + $(if ($ctl -eq $ch) { '(it stole the foreground: this test can detect that)' } else { '(no steal this time: the control is inconclusive)' })
  [Win]::Raise($uh); Start-Sleep -Milliseconds 400
  [Win]::Start()

  # 一、后台连续输入：命令行里把 "-- ee --ee aa 11 ;;" 写进文件，文件内容必须与输入逐字一致
  $payload = '-- ee --ee aa 11 ;;'
  foreach ($run in 1..$Runs) {
    $name = "typed$run.txt"
    $r = Dx 'type' @{ window = "hwnd:$chs"; text = "Set-Content $name -Value '$payload' -NoNewline" }
    if ($r.Ok) { $r = Dx 'key' @{ window = "hwnd:$chs"; keys = 'enter' } }
    $got = Wait-File (Join-Path $work $name)
    Check "type run ${run}: the console received '$payload' verbatim" ($got -ceq $payload) ("got " + ($(if ($null -eq $got) { '(no file; ' + $r.Text.Substring(0, [Math]::Min(120, $r.Text.Length)) + ')' } else { "'$got'" })))
  }

  # 一·补：文字里自带换行（`n）就应当提交命令，不必再单独发 enter；文件内容不能带多余字符
  $nl = 'NL' + [guid]::NewGuid().ToString('N').Substring(0, 6)
  $r = Dx 'type' @{ window = "hwnd:$chs"; text = "Set-Content newline.txt -Value '$nl' -NoNewline`n" }
  $got = Wait-File (Join-Path $work 'newline.txt')
  Check 'type with a trailing newline commits the command (no separate enter)' ($got -ceq $nl) ("got " + $(if ($null -eq $got) { '(no file; ' + $r.Text.Substring(0, [Math]::Min(120, $r.Text.Length)) + ')' } else { "'$got'" }))

  # 二、最小化的窗口：恢复 / 最大化 / 最小化 / 再恢复，用户的窗口始终在前台
  [void][Win]::ShowWindow($ch, 6); Start-Sleep -Milliseconds 500
  [Win]::Raise($uh); Start-Sleep -Milliseconds 400
  Check 'setup: console minimized, stand-in user window in front' ([Win]::IsIconic($ch) -and [Win]::GetForegroundWindow() -eq $uh) ([Win]::Desc([Win]::GetForegroundWindow()))
  $steps = @(
    @{ op = 'restore';  ok = { -not [Win]::IsIconic($ch) -and -not [Win]::IsZoomed($ch) } },
    @{ op = 'maximize'; ok = { [Win]::IsZoomed($ch) } },
    @{ op = 'minimize'; ok = { [Win]::IsIconic($ch) } },
    @{ op = 'restore';  ok = { -not [Win]::IsIconic($ch) } }
  )
  foreach ($s in $steps) {
    $r = Dx 'window' @{ window = "hwnd:$chs"; op = $s.op }
    Start-Sleep -Milliseconds 400
    $fg = [Win]::GetForegroundWindow()
    Check "window op=$($s.op) on the console (background mode)" ($r.Ok -and (& $s.ok)) ("iconic=$([Win]::IsIconic($ch)) zoomed=$([Win]::IsZoomed($ch)) " + $(if ($r.Ok) { '' } else { $r.Text.Substring(0, [Math]::Min(140, $r.Text.Length)) }))
    Check "  foreground is still the user's window after op=$($s.op)" ($fg -eq $uh) ([Win]::Desc($fg))
  }

  # 三、恢复之后目标照样能被操控
  $r = Dx 'type' @{ window = "hwnd:$chs"; text = "Set-Content after.txt -Value '$payload' -NoNewline" }
  if ($r.Ok) { $r = Dx 'key' @{ window = "hwnd:$chs"; keys = 'enter' } }
  $got = Wait-File (Join-Path $work 'after.txt')
  Check 'type after the restore: the console received the text verbatim' ($got -ceq $payload) ("got " + $(if ($null -eq $got) { '(no file)' } else { "'$got'" }))

  $log = [Win]::Stop()
  $stolen = @($log | Where-Object { $_ -like "*$chs *" })
  Check 'the console never became the foreground window during the whole run' ($stolen.Count -eq 0) ("($($log.Count) foreground changes polled at sub-millisecond resolution)")
  if ($stolen.Count) { $stolen | ForEach-Object { "      $_" } }
}
finally {
  # 只结束自己拉起的进程：控制台宿主连同它里面的 PowerShell（按父进程找），以及充当用户窗口的靶子。
  foreach ($p in $procs) {
    if (-not $p) { continue }
    Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)" -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
  }
  foreach ($i in 1..20) {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path -LiteralPath $work)) { break }
    Start-Sleep -Milliseconds 250
  }
}
"`n=== RESULT: $fails failure(s) ==="
exit $fails
