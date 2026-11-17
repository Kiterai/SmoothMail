param(
    [string]$InputDirectory = 'C:\McpShares\share1',
    [string]$OutputDirectory = 'C:\McpShares\share2'
)
# Only for a NEW offline Windows Sandbox. Never run against host user data.
$ErrorActionPreference = 'Stop'
$result = @{ testOnly = $true; success = $false; variants = @() }
function Invoke-Probe {
    param([string]$File, [string]$Arguments, [string]$Log, [string]$Directory)
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo.FileName = $File
    $process.StartInfo.WorkingDirectory = $Directory
    $process.StartInfo.Arguments = $Arguments
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(120000)) {
        $process.Kill()
        throw 'Test process timeout'
    }
    $process.WaitForExit()
    [IO.File]::WriteAllText($Log + '-stdout.txt', $stdout.Result)
    [IO.File]::WriteAllText($Log + '-stderr.txt', $stderr.Result)
    $code = $process.ExitCode
    $process.Dispose()
    return $code
}
try {
    $result.sdkAbsent = -not (Test-Path 'C:\Qt') -and
        @(Get-Command cl.exe -ErrorAction SilentlyContinue).Count -eq 0
    if (-not $result.sdkAbsent) { throw 'Expected a fresh SDK-free guest' }
    $env:SMOOTHMAIL_GUI_SANDBOX_TEST = '1'
    foreach ($variant in @('baseline', 'replacement')) {
        $directory = 'C:\DLL replacement ' + [Guid]::NewGuid().ToString('N')
        Expand-Archive -LiteralPath (Join-Path $InputDirectory "$variant.zip") -DestinationPath $directory
        $tree = Join-Path $directory 'SmoothMail'
        $runtime = Join-Path $tree 'runtime'
        $logs = Join-Path $OutputDirectory $variant
        New-Item -ItemType Directory $logs | Out-Null
        $inventory = @(Get-Content (Join-Path $InputDirectory "$variant-sha256.txt"))
        foreach ($row in $inventory) {
            if ((Get-FileHash -LiteralPath (Join-Path $directory $row.Substring(66))).Hash -ne $row.Substring(0, 64)) {
                throw 'Test runtime hash mismatch'
            }
        }
        $env:PATH = "$env:WINDIR\system32;$env:WINDIR"
        $env:QT_PLUGIN_PATH = $runtime
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $runtime 'platforms'
        $env:SMOOTHMAIL_TEST_SASL_PATH = Join-Path $runtime 'sasl2'
        $env:QT_QPA_PLATFORM = 'windows'
        $record = @{ name = $variant; hashesValid = $true; files = $inventory.Count; tests = @(); success = $false }
        $result.variants += $record
        $app = Start-Process (Join-Path $runtime 'Smoothmail.exe') -WorkingDirectory 'C:\Windows' -PassThru
        try {
            $deadline = [DateTime]::UtcNow.AddSeconds(30)
            do {
                $app.Refresh()
                if ($app.HasExited) { throw 'Product exited before creating a window' }
                if ($app.MainWindowHandle -ne 0) { break }
                Start-Sleep -Milliseconds 100
            } while ([DateTime]::UtcNow -lt $deadline)
            if ($app.MainWindowHandle -eq 0) { throw 'Product window timeout' }
            $record.appReady = $true
            $record.applicationSha256 = (Get-FileHash (Join-Path $runtime 'Smoothmail.exe')).Hash
            $record.localModules = @($app.Modules | Where-Object {
                $_.ModuleName -match '^(Qt6|KF6|KPim6|qt6keychain|libsasl|iconv-2|intl-8)'
            } | ForEach-Object {
                if (-not $_.FileName.StartsWith($runtime + '\', [StringComparison]::OrdinalIgnoreCase)) {
                    throw 'Product loaded a library from outside its runtime'
                }
                @{ name = $_.ModuleName; sha256 = (Get-FileHash -LiteralPath $_.FileName).Hash }
            })
            if (@($record.localModules | Where-Object { $_.name -eq 'KF6Codecs.dll' }).Count -ne 1) {
                throw 'Product did not load the tested KCodecs library'
            }
        } finally {
            if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force }
        }
        $env:QT_QPA_PLATFORM = 'offscreen'
        $arguments = ''
        if ($variant -eq 'replacement') { $arguments = '--require-markers' }
        $code = Invoke-Probe (Join-Path $runtime 'source_replacement_probe.exe') $arguments `
            (Join-Path $logs 'marker-probe') $runtime
        if ($code -ne 0) { throw 'API/modified-library marker probe failed' }
        $record.probePassed = $true
        if ($variant -eq 'baseline') {
            $code = Invoke-Probe (Join-Path $runtime 'source_replacement_probe.exe') '--require-markers' `
                (Join-Path $logs 'marker-negative') $runtime
            if ($code -eq 0) { throw 'Unmodified DLLs unexpectedly passed the marker requirement' }
            $record.negativeControlPassed = $true
        }
        foreach ($test in Get-Content (Join-Path $InputDirectory 'tests.txt')) {
            if ($test -notmatch '^tst_[a-z]+$') { throw 'Invalid test name' }
            $testLog = Join-Path $logs "$test.txt"
            $code = Invoke-Probe (Join-Path $runtime "$test.exe") ('-o "' + $testLog + ',txt"') `
                (Join-Path $logs $test) $runtime
            $text = Get-Content $testLog -Raw
            if ($code -ne 0 -or $text -notmatch 'Totals:\s+(\d+) passed,\s+0 failed,\s+0 skipped') {
                throw "Functional test failed or skipped: $test"
            }
            $record.tests += @{ name = "$test"; exitCode = $code; passed = [int]$Matches[1] }
        }
        $record.success = $true
    }
    $result.success = $result.variants.Count -eq 2 -and
        @($result.variants | Where-Object { -not $_.success }).Count -eq 0
} catch {
    $result.error = $_.Exception.Message
} finally {
    $result | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
}
if (-not $result.success) { exit 1 }
