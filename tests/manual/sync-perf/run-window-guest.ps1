$ErrorActionPreference = 'Stop'
$share = 'C:\McpShares\share1'
$work = 'C:\SyncOptimization-windows'
New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item "$share\runtime\*" $work -Recurse
$env:PATH = "$work;C:\Windows\System32;C:\Windows"
$env:SMOOTHMAIL_PERF_GUEST = '1'
foreach ($window in @(100, 250, 500, 1000)) {
    $env:SMOOTHMAIL_PERF_FLAG_WINDOW = "$window"
    $p = Start-Process "$work\sync_perf.exe" -ArgumentList "$work\window-$window --window-only" -PassThru -Wait
    New-Item -ItemType Directory -Force "$share\output\windows\$window" | Out-Null
    Copy-Item "$work\window-$window\results.jsonl","$work\window-$window\complete.txt" "$share\output\windows\$window"
    $p.ExitCode | Set-Content "$share\output\windows\$window\exit.txt"
}
'done' | Set-Content "$share\output\windows\done.txt"
