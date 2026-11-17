$ErrorActionPreference = 'Stop'
$share = 'C:\McpShares\share1'
$work = 'C:\SyncPerf'
New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item "$share\runtime\*" $work -Recurse
$env:SMOOTHMAIL_PERF_GUEST = '1'
$env:PATH = "$work;C:\Windows\System32;C:\Windows"
Set-Location $work
Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, TotalVisibleMemorySize, NumberOfProcesses |
    ConvertTo-Json | Set-Content "$share\output\environment.json"
Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors |
    ConvertTo-Json | Set-Content "$share\output\cpu.json"
Start-Process "$work\smoothmail.exe"
$process = Start-Process "$work\sync_perf.exe" -ArgumentList 'C:\SyncPerf\output' -PassThru -Wait
Copy-Item "$work\output" "$share\output\with-diagnostics" -Recurse
$process.ExitCode | Set-Content "$share\output\exit-code.txt"
$process = Start-Process "$work\sync_perf.exe" -ArgumentList 'C:\SyncPerf\output-nolog --no-diagnostics' -PassThru -Wait
Copy-Item "$work\output-nolog" "$share\output\without-diagnostics" -Recurse
$process.ExitCode | Set-Content "$share\output\exit-code-nolog.txt"
'done' | Set-Content "$share\output\done.txt"
