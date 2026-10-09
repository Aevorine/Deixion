$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$setup = Join-Path $root 'build\Deixion-Setup-x64.exe'
$base = Join-Path $env:TEMP 'dxe2e'
$dir = Join-Path $base 'Pick Me\Deixion'          # 路径含空格，且父目录名不是 Deixion
$fails = 0
function Check($name, $ok, $detail = '') { if ($ok) { "PASS  $name $detail" } else { $script:fails++; "FAIL  $name $detail" } }
function Run-Setup([string[]]$a) {
  $psi = New-Object Diagnostics.ProcessStartInfo $setup
  $psi.Arguments = ($a -join ' '); $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
  $sw = [Diagnostics.Stopwatch]::StartNew(); $p = [Diagnostics.Process]::Start($psi); $p.WaitForExit(); $sw.Stop()
  return [pscustomobject]@{ Code = $p.ExitCode; Ms = $sw.ElapsedMilliseconds }
}
function Kill-Dx { Get-Process | Where-Object { $_.ProcessName -match '^(Deixion|deixion-cli)$' } | ForEach-Object { try { $_.Kill() } catch {} }; Start-Sleep -Milliseconds 400 }

Kill-Dx
if (Test-Path $base) { Remove-Item -Recurse -Force $base }
New-Item -ItemType Directory $base | Out-Null
$uk = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Deixion'
# 这个脚本不碰真实环境：不动 Claude Code 的 MCP 登记与 skill（程序见到该变量就跳过），真实安装的卸载注册表项先备份、结束时恢复。
$env:DEIXION_NO_CLAUDE = '1'
$ukBackup = Join-Path $env:TEMP 'dx-e2e-uninstall-key.reg'
Remove-Item $ukBackup -ErrorAction SilentlyContinue
$hadKey = Test-Path $uk
if ($hadKey) { reg export 'HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Deixion' $ukBackup /y | Out-Null }

"=== 1. fresh install: path with spaces, parent not named Deixion, /D= quoted ==="
$r = Run-Setup @("/S", "`"/D=$base\Pick Me`"", "/CLAUDE=0", "/AUTOSTART=0", "/STARTMENU=0", "`"/LOG=$base\i.log`"")
Check 'exit code 0' ($r.Code -eq 0) "($($r.Code), $($r.Ms) ms)"
Check 'lands under ...\Deixion' (Test-Path "$dir\Deixion.exe")
Check 'uninstall key written' (Test-Path $uk)
Check 'InstallLocation correct' ((Get-ItemProperty $uk -ErrorAction SilentlyContinue).InstallLocation -eq $dir)
Check 'no Run key (autostart=0)' (-not (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -ErrorAction SilentlyContinue).Deixion)

"=== 2. update while app runs and cli.exe is locked ==="
$env:DEIXION_NO_APP = '1'
$app = Start-Process "$dir\Deixion.exe" -ArgumentList '--tray' -PassThru; Start-Sleep -Milliseconds 2500
$psi = New-Object Diagnostics.ProcessStartInfo "$dir\deixion-cli.exe", 'mcp'
$psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
$mcp = [Diagnostics.Process]::Start($psi); Start-Sleep -Milliseconds 800
Remove-Item Env:DEIXION_NO_APP
Check 'precondition: app + locked mcp alive' ((-not $app.HasExited) -and (-not $mcp.HasExited))
$r = Run-Setup @("/S", "/UPDATE", "/RELAUNCH", "/CLAUDE=0", "`"/LOG=$base\u.log`"")
Check 'update exit 0' ($r.Code -eq 0) "($($r.Code), $($r.Ms) ms)"
Start-Sleep -Milliseconds 2500
$now = @(Get-Process Deixion -ErrorAction SilentlyContinue)
Check 'old app exited, new one running' ($app.HasExited -and $now.Count -eq 1 -and $now[0].Id -ne $app.Id) "(old $($app.Id) -> new $($now.Id))"
Check 'locked cli.exe replaced (hash == build)' ((Get-FileHash "$dir\deixion-cli.exe").Hash -eq (Get-FileHash "$root\build\deixion-cli.exe").Hash)
Check 'locked mcp process undisturbed' (-not $mcp.HasExited)
$mcp.StandardInput.Close(); $mcp.WaitForExit(3000) | Out-Null; Kill-Dx

"=== 3. corrupted package must not touch the install (rollback safety) ==="
$bad = Join-Path $base 'bad-setup.exe'
node (Join-Path $PSScriptRoot 'corrupt.mjs') $setup $bad 'WebView2Loader.dll'
$before = (Get-FileHash "$dir\Deixion.exe").Hash
$psi = New-Object Diagnostics.ProcessStartInfo $bad; $psi.Arguments = "/S /UPDATE /CLAUDE=0"; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
$p = [Diagnostics.Process]::Start($psi); $p.WaitForExit()
Check 'corrupted payload rejected (exit != 0)' ($p.ExitCode -ne 0) "(exit $($p.ExitCode))"
Check 'existing install untouched' ((Get-FileHash "$dir\Deixion.exe").Hash -eq $before)
Check 'no .dxnew debris left' (@(Get-ChildItem $dir -Recurse -Filter *.dxnew -ErrorAction SilentlyContinue).Count -eq 0)

"=== 4. uninstall from the installed uninstall.exe (self-delete) ==="
$app = Start-Process "$dir\Deixion.exe" -ArgumentList '--tray' -PassThru; Start-Sleep -Milliseconds 2000
New-Item -ItemType Directory "$env:LOCALAPPDATA\Deixion" -Force | Out-Null
$psi = New-Object Diagnostics.ProcessStartInfo "$dir\uninstall.exe"; $psi.Arguments = "/UNINSTALL /S"; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
$p = [Diagnostics.Process]::Start($psi); $p.WaitForExit()
$t0 = Get-Date; while ((Test-Path $dir) -and ((Get-Date) - $t0).TotalSeconds -lt 15) { Start-Sleep -Milliseconds 300 }
Check 'install dir removed' (-not (Test-Path $dir))
Check 'app was stopped' ($app.HasExited)
Check 'uninstall key removed' (-not (Test-Path $uk))
Check 'user data kept (no /PURGE)' (Test-Path "$env:LOCALAPPDATA\Deixion")
Start-Sleep -Seconds 4
Check 'temp uninstaller copy cleaned' (@(Get-ChildItem $env:TEMP -Filter 'Deixion-uninstall-*.exe' -ErrorAction SilentlyContinue).Count -eq 0)

Remove-Item Env:DEIXION_NO_CLAUDE -ErrorAction SilentlyContinue
if ($hadKey -and (Test-Path $ukBackup)) { reg import $ukBackup 2>$null | Out-Null; Remove-Item $ukBackup -ErrorAction SilentlyContinue; Check 'real uninstall entry restored' (Test-Path $uk) }
"`n=== RESULT: $fails failure(s) ==="
