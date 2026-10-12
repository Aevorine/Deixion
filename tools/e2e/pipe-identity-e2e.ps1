# Named-pipe identity check, end to end: a pipe with Deixion's name that is served by ANOTHER account must get nothing from the client.
#   powershell -ExecutionPolicy Bypass -File tools\e2e\pipe-identity-e2e.ps1 [-Cli path] [-SkipSystem]
# What it does:
#   1. control  - a fake server run by YOUR account takes the pipe name; the real deixion-cli sends its request to it
#                 (bytes > 0). This proves the harness can see a send.
#   2. the test - the same fake server run by NT AUTHORITY\SYSTEM (a scheduled task; one UAC prompt); the client must
#                 connect, send 0 bytes and fall back to its in-process engine.
# Side effects, so run it on a development machine: a running Deixion is asked to quit for the duration (the pipe name is
# one per user) and is started again afterwards; the client is a portable copy in a temp folder, so your settings and data
# are not touched. The script that SYSTEM executes is first copied to a folder only administrators can write.
# -SkipSystem runs the control only (no elevation).
param(
  [string]$Cli = '', [switch]$SkipSystem,
  [ValidateSet('main', 'server', 'elevate')][string]$Role = 'main',
  [string]$PipeName = '', [string]$OutFile = '', [string]$StopFile = '', [string]$Work = '', [int]$Seconds = 40
)
$ErrorActionPreference = 'Stop'

# ---- fake server: accept connections on the pipe name, record who connected and how many bytes arrived ----
if ($Role -eq 'server') {
  Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class DxPipe { [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetNamedPipeClientProcessId(IntPtr pipe, out uint pid); }'
  $sec = New-Object System.IO.Pipes.PipeSecurity
  $everyone = New-Object System.Security.Principal.SecurityIdentifier('S-1-1-0')
  $rights = [System.IO.Pipes.PipeAccessRights]::ReadWrite -bor [System.IO.Pipes.PipeAccessRights]::CreateNewInstance
  $sec.AddAccessRule((New-Object System.IO.Pipes.PipeAccessRule($everyone, $rights, 'Allow')))
  $conns = @()
  $err = ''
  $deadline = (Get-Date).AddSeconds($Seconds)
  try {
    while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath $StopFile)) {
      $p = New-Object System.IO.Pipes.NamedPipeServerStream($PipeName, 'InOut', 4, 'Byte', 'Asynchronous', 4096, 4096, $sec)
      try {
        if (-not (Test-Path -LiteralPath "$OutFile.ready")) { Set-Content -LiteralPath "$OutFile.ready" -Value 'ready' }
        $w = $p.BeginWaitForConnection($null, $null)
        while (-not $w.AsyncWaitHandle.WaitOne(150)) { if ((Get-Date) -ge $deadline -or (Test-Path -LiteralPath $StopFile)) { break } }
        if (-not $w.IsCompleted) { break }
        $p.EndWaitForConnection($w)
        [uint32]$cpid = 0
        [void][DxPipe]::GetNamedPipeClientProcessId($p.SafePipeHandle.DangerousGetHandle(), [ref]$cpid)
        $buf = New-Object byte[] 4096
        $n = 0
        try { $t = $p.ReadAsync($buf, 0, 4096); if ($t.Wait(1500)) { $n = $t.Result } } catch { $n = 0 }
        $conns += @{ pid = [int]$cpid; bytes = [int]$n }
      } finally { $p.Dispose() }
    }
  } catch { $err = "$_" }
  @{ sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value; conns = @($conns); error = $err } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $OutFile
  exit 0
}

# ---- elevated helper: put the script where only administrators can write, run it as SYSTEM, clean up ----
if ($Role -eq 'elevate') {
  $task = 'dx-pipe-identity-e2e'
  $sysDir = Join-Path $env:ProgramData ('dx-pipe-e2e-' + [guid]::NewGuid().ToString('N'))
  try {
    New-Item -ItemType Directory -Path $sysDir | Out-Null
    & icacls.exe $sysDir /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' | Out-Null
    $script = Join-Path $sysDir 'pipe-identity-e2e.ps1'
    Copy-Item -LiteralPath $PSCommandPath -Destination $script
    $out = Join-Path $sysDir 'result.json'
    $arg = "-NoProfile -ExecutionPolicy Bypass -File `"$script`" -Role server -PipeName `"$PipeName`" -OutFile `"$out`" -StopFile `"$StopFile`" -Seconds $Seconds"
    $action = New-ScheduledTaskAction -Execute "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Argument $arg
    $set = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    Register-ScheduledTask -TaskName $task -Action $action -Settings $set -User 'NT AUTHORITY\SYSTEM' -RunLevel Highest -Force | Out-Null
    Start-ScheduledTask -TaskName $task
    Set-Content -LiteralPath (Join-Path $Work 'sysdir.txt') -Value $sysDir
    $end = (Get-Date).AddSeconds($Seconds + 20)
    while (-not (Test-Path -LiteralPath $out) -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 200 }
    if (Test-Path -LiteralPath $out) { Copy-Item -LiteralPath $out -Destination (Join-Path $Work 'system.json') }
  } catch {
    Set-Content -LiteralPath (Join-Path $Work 'elevate-error.txt') -Value "$_"
  } finally {
    Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $sysDir) { Remove-Item -LiteralPath $sysDir -Recurse -Force -ErrorAction SilentlyContinue }
    Set-Content -LiteralPath (Join-Path $Work 'elevate-done.txt') -Value 'done'
  }
  exit 0
}

# ---- main ----
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Cli) { $Cli = Join-Path $root 'build\deixion-cli.exe' }
if (-not (Test-Path -LiteralPath $Cli)) { throw "missing $Cli; build first" }
$ps = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$fails = 0
function Check($name, $ok, $detail = '') { if (-not $ok) { $script:fails++ }; "{0}  {1} {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $name, $detail }
function WaitFor($path, $ms) { $end = (Get-Date).AddMilliseconds($ms); while (-not (Test-Path -LiteralPath $path) -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 100 }; Test-Path -LiteralPath $path }
# Listing the pipe directory does not open the pipe (Test-Path would connect to it and use up an instance).
function PipeExists($name) {
  try { $all = [System.IO.Directory]::GetFiles('\\.\pipe\') } catch { $all = @(Get-ChildItem '\\.\pipe\' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }) }
  [bool]($all | Where-Object { $_ -ieq "\\.\pipe\$name" })
}
# The portable client: its doctor command connects through the pipe when it may, and says which engine answered.
function RunClient($tag) {
  $o = Join-Path $work "$tag.out.txt"
  $p = Start-Process -FilePath $client -ArgumentList 'doctor' -WorkingDirectory $cliDir -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput $o -RedirectStandardError (Join-Path $work "$tag.err.txt")
  @{ pid = $p.Id; out = (Get-Content -LiteralPath $o -Raw -ErrorAction SilentlyContinue) }
}
function ReadResult($path) { if (Test-Path -LiteralPath $path) { Get-Content -LiteralPath $path -Raw | ConvertFrom-Json } else { $null } }

$pipe = 'Deixion-v1-' + $env:USERNAME
$work = Join-Path ([System.IO.Path]::GetTempPath()) ('dx-pipe-e2e-' + [guid]::NewGuid().ToString('N'))
$cliDir = Join-Path $work 'cli'
New-Item -ItemType Directory -Path $cliDir | Out-Null
$client = Join-Path $cliDir 'deixion-cli.exe'
Copy-Item -LiteralPath $Cli -Destination $client
Set-Content -LiteralPath (Join-Path $cliDir 'portable.flag') -Value ''

# The pipe name is one per user: a running Deixion owns it, so ask it to quit and remember to start it again.
$running = @(Get-Process -Name Deixion -ErrorAction SilentlyContinue | Where-Object { $_.Path } | Select-Object -ExpandProperty Path -Unique)
foreach ($exe in $running) { & $exe --quit | Out-Null }
$end = (Get-Date).AddSeconds(8)
while ((PipeExists $pipe) -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 200 }

try {
  if (PipeExists $pipe) { throw "the pipe \\.\pipe\$pipe is still served; quit Deixion and run again" }

  # 1. control: same account
  $stop1 = Join-Path $work 'stop1'
  $out1 = Join-Path $work 'control.json'
  $srv = Start-Process -FilePath $ps -WindowStyle Hidden -PassThru -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Role server -PipeName `"$pipe`" -OutFile `"$out1`" -StopFile `"$stop1`" -Seconds 30"
  if (-not (WaitFor "$out1.ready" 10000)) { throw 'the control server did not start' }
  $c1 = RunClient 'control'
  Set-Content -LiteralPath $stop1 -Value ''
  [void](WaitFor $out1 8000)
  $r1 = ReadResult $out1
  $mine1 = @($r1.conns | Where-Object { $_.pid -eq $c1.pid })
  Check 'control: a same-account server is accepted and receives the request' ($mine1.Count -ge 1 -and ($mine1 | Measure-Object bytes -Sum).Sum -gt 0) ("connections {0}, bytes {1}" -f $mine1.Count, ($mine1 | Measure-Object bytes -Sum).Sum)
  if (-not $srv.HasExited) { $srv.WaitForExit(3000) | Out-Null }

  # 2. the test: another account (SYSTEM)
  if ($SkipSystem) { 'SKIP  another-account server (run without -SkipSystem; needs one UAC approval)' }
  else {
    $stop2 = Join-Path $work 'stop2'
    $elevArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Role elevate -PipeName `"$pipe`" -StopFile `"$stop2`" -Work `"$work`" -Seconds 40"
    $elevated = $true
    try { Start-Process -FilePath $ps -Verb RunAs -WindowStyle Hidden -ArgumentList $elevArgs | Out-Null } catch { $elevated = $false }
    Check 'elevation approved (UAC)' $elevated
    if ($elevated) {
      $ready = $false
      if (WaitFor (Join-Path $work 'sysdir.txt') 20000) {
        $sysDir = (Get-Content -LiteralPath (Join-Path $work 'sysdir.txt') -Raw).Trim()
        $ready = WaitFor (Join-Path $sysDir 'result.json.ready') 15000
      }
      Check 'the SYSTEM server took the pipe name' ($ready -and (PipeExists $pipe)) $(if (Test-Path -LiteralPath (Join-Path $work 'elevate-error.txt')) { Get-Content -LiteralPath (Join-Path $work 'elevate-error.txt') -Raw })
      if ($ready) {
        $c2 = RunClient 'system'
        Set-Content -LiteralPath $stop2 -Value ''
        [void](WaitFor (Join-Path $work 'system.json') 20000)
        $r2 = ReadResult (Join-Path $work 'system.json')
        $mine2 = @($r2.conns | Where-Object { $_.pid -eq $c2.pid })
        Check 'the fake server really ran as another account (SYSTEM)' ($r2.sid -eq 'S-1-5-18') "sid $($r2.sid) $($r2.error)"
        Check 'the client reached the foreign pipe' ($mine2.Count -ge 1) "connections $($mine2.Count)"
        Check 'the client sent nothing to it' ($mine2.Count -ge 1 -and ($mine2 | Measure-Object bytes -Sum).Sum -eq 0) "bytes $(($mine2 | Measure-Object bytes -Sum).Sum)"
        Check 'the client fell back to its in-process engine' ($c2.out -match 'engine reachable via in-process') (($c2.out -split "`n" | Where-Object { $_ -match 'engine' }) -join ' ').Trim()
      }
      Set-Content -LiteralPath $stop2 -Value ''
      [void](WaitFor (Join-Path $work 'elevate-done.txt') 70000)
      Check 'the scheduled task and its folder were removed' ((Test-Path -LiteralPath (Join-Path $work 'elevate-done.txt')) -and -not (PipeExists $pipe))
    }
  }
} catch {
  Check 'run' $false "$_"
} finally {
  foreach ($f in 'stop1', 'stop2') { Set-Content -LiteralPath (Join-Path $work $f) -Value '' -ErrorAction SilentlyContinue }
  Start-Sleep -Milliseconds 800
  foreach ($exe in $running) { Start-Process -FilePath $exe -ArgumentList '--tray' }
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
if ($fails) { "`n$fails failed"; exit 1 }
"`nall passed"
exit 0
