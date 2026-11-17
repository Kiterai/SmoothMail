param([string]$Phase = 'final')
$ErrorActionPreference = 'Stop'
$share = 'C:\McpShares\share1'
& "$share\run-optimization-guest.ps1" -Phase "$Phase-regression" -AllTests
foreach ($parallel in @(1, 2, 4, 8)) {
    $work = "C:\FolderParallel-$Phase-$parallel"
    New-Item -ItemType Directory -Force $work | Out-Null
    Copy-Item "$share\runtime\*" $work -Recurse
    $env:PATH = "$work;C:\Windows\System32;C:\Windows"
    $env:QT_PLUGIN_PATH = $work
    $env:SMOOTHMAIL_PERF_GUEST = '1'
    $p = Start-Process "$work\sync_perf.exe" -ArgumentList "$work\results --bulk-only --folders-only --list-status --parallel $parallel" -PassThru -Wait
    New-Item -ItemType Directory -Force "$share\output\$Phase-$parallel" | Out-Null
    Copy-Item "$work\results\*" "$share\output\$Phase-$parallel" -Recurse
    $p.ExitCode | Set-Content "$share\output\$Phase-$parallel\exit.txt"
    if ($p.ExitCode) { throw "Parallel $parallel failed" }
}
'done' | Set-Content "$share\output\$Phase-done.txt"
