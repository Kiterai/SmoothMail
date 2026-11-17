$ErrorActionPreference = 'Stop'
$share = 'C:\McpShares\share1'
$work = 'C:\SyncPerf'
if (!(Test-Path "$share\output\done.txt")) { throw 'Run baseline first.' }
Copy-Item "$share\sync_perf_deep.exe" "$work\sync_perf_deep.exe"
$env:SMOOTHMAIL_PERF_GUEST = '1'
$env:PATH = "$work;C:\Windows\System32;C:\Windows"
$process = Start-Process "$work\sync_perf_deep.exe" -ArgumentList 'C:\SyncPerf\deep --deep-only' -PassThru -Wait
New-Item -ItemType Directory -Force "$share\output\deep" | Out-Null
Copy-Item "$work\deep\results.jsonl","$work\deep\complete.txt","$work\deep\*.png" "$share\output\deep"
Copy-Item "$work\deep\diagnostics" "$share\output\deep" -Recurse
$process.ExitCode | Set-Content "$share\output\exit-code-deep.txt"
