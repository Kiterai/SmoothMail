param([string]$Phase = 'gmail-fix')
$ErrorActionPreference = 'Stop'
$share = 'C:\McpShares\share1'
$work = "C:\GmailRoot-$Phase"
New-Item -ItemType Directory -Force $work,"$share\output\$Phase-baseline" | Out-Null
Copy-Item "$share\runtime\*" $work -Recurse
$env:PATH = "$work;C:\Windows\System32;C:\Windows"
$env:QT_PLUGIN_PATH = $work
$env:SMOOTHMAIL_TEST_SASL_PATH = "$work\sasl2"
$p = Start-Process "$work\gmail_before.exe" -ArgumentList "gmailHierarchyRootIsNeverFetched -o $work\before.txt,txt" -PassThru -Wait
Copy-Item "$work\before.txt" "$share\output\$Phase-baseline"
$p.ExitCode | Set-Content "$share\output\$Phase-baseline\exit.txt"
& "$share\run-optimization-guest.ps1" -Phase $Phase -AllTests
