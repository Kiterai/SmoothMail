param([string]$Phase = 'stage1', [switch]$Baseline)
$ErrorActionPreference = 'Stop'
$share = 'C:\McpShares\share1'
$work = "C:\SyncOptimization-$Phase"
New-Item -ItemType Directory -Force $work,"$share\output\$Phase" | Out-Null
Copy-Item "$share\runtime\*" $work -Recurse
$env:PATH = "$work;C:\Windows\System32;C:\Windows"
$env:QT_PLUGIN_PATH = $work
$env:SMOOTHMAIL_TEST_SASL_PATH = "$work\sasl2"
$env:SMOOTHMAIL_TEST_KEYCHAIN = '1'
$env:SMOOTHMAIL_GUI_SANDBOX_TEST = '1'
$env:SMOOTHMAIL_PERF_GUEST = '1'
$env:SMOOTHMAIL_GUI_OUTPUT = "$work\tests"
New-Item -ItemType Directory -Force "$work\tests" | Out-Null
Set-Location $work
$failures = @()
foreach ($name in @('tst_accountrepository','tst_connectiontester','tst_mailtransport','tst_guismoke')) {
    $p = Start-Process "$work\$name.exe" -ArgumentList "-o $work\tests\$name.txt,txt" -PassThru -Wait
    if ($p.ExitCode -ne 0) { $failures += "$name=$($p.ExitCode)" }
}
Copy-Item "$work\tests" "$share\output\$Phase" -Recurse
$failures | Set-Content "$share\output\$Phase\failures.txt"
if ($failures.Count) { throw 'Regression test failed; benchmarks not started.' }
if ($Baseline) {
    $p = Start-Process "$work\sync_perf_before.exe" -ArgumentList "$work\before" -PassThru -Wait
    Copy-Item "$work\before" "$share\output\$Phase" -Recurse
    $p.ExitCode | Set-Content "$share\output\$Phase\before-exit.txt"
}
$p = Start-Process "$work\sync_perf.exe" -ArgumentList "$work\after" -PassThru -Wait
Copy-Item "$work\after" "$share\output\$Phase" -Recurse
$p.ExitCode | Set-Content "$share\output\$Phase\after-exit.txt"
'done' | Set-Content "$share\output\$Phase\done.txt"
