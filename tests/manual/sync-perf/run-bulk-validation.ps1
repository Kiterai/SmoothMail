param([string]$Phase = 'final')
$ErrorActionPreference = 'Stop'
$share = 'C:\McpShares\share1'
& "$share\run-optimization-guest.ps1" -Phase "$Phase-regression" -AllTests
& "$share\run-bulk-guest.ps1" -Phase "$Phase-after"
& "$share\run-bulk-guest.ps1" -Phase "$Phase-list-status" -ListStatus
