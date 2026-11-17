param([string]$Phase, [switch]$ListStatus)
$ErrorActionPreference = 'Stop'
$share = 'C:\McpShares\share1'
$work = "C:\Bulk-$Phase"
New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item "$share\runtime\*" $work -Recurse
$env:PATH = "$work;C:\Windows\System32;C:\Windows"
$env:QT_PLUGIN_PATH = $work
$env:SMOOTHMAIL_PERF_GUEST = '1'
$options = "$work\results --bulk-only"
if ($ListStatus) { $options += ' --list-status' }
$p = Start-Process "$work\sync_perf.exe" -ArgumentList $options -Wait -PassThru
New-Item -ItemType Directory -Force "$share\output\$Phase" | Out-Null
Copy-Item "$work\results\*" "$share\output\$Phase" -Recurse
$p.ExitCode | Set-Content "$share\output\$Phase\exit.txt"
