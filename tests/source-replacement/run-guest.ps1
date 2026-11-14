param(
    [string]$InputDirectory = 'C:\McpShares\share1',
    [string]$OutputDirectory = 'C:\McpShares\share2'
)
# Run ONLY in a new offline Windows Sandbox. Inputs must contain synthetic
# fixtures and runtime binaries, never host accounts, databases or credentials.
$ErrorActionPreference = 'Stop'
$result = @{ testOnly = $true; success = $false; variants = @() }
$run = [Guid]::NewGuid().ToString('N').Substring(0, 8)
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
function Invoke-TestProcess {
    param([string]$File, [string[]]$Arguments, [string]$Directory, [string]$Log, [int]$Timeout = 60000)
    # Retain the native process handle. Windows PowerShell Start-Process can
    # lose ExitCode for short-lived processes when combined with WaitForExit.
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo.FileName = $File
    $process.StartInfo.WorkingDirectory = $Directory
    $process.StartInfo.Arguments = $Arguments -join ' '
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($Timeout)) {
        $process.Kill()
        $process.WaitForExit()
        throw 'Isolated functional test timeout'
    }
    $process.WaitForExit()
    [IO.File]::WriteAllText($Log + '-stdout.txt', $stdout.Result)
    [IO.File]::WriteAllText($Log + '-stderr.txt', $stderr.Result)
    $code = $process.ExitCode
    $process.Dispose()
    return $code
}
try {
    $env:PATH = "$env:WINDIR\system32;$env:WINDIR"
    $env:SMOOTHMAIL_TEST_KEYCHAIN = '1'
    $env:SMOOTHMAIL_GUI_SANDBOX_TEST = '1'
    foreach ($variant in @('baseline', 'replacement')) {
        $runtime = "C:\Source replacement $run $variant"
        $logs = Join-Path $OutputDirectory $variant
        New-Item -ItemType Directory $logs | Out-Null
        Expand-Archive -LiteralPath (Join-Path $InputDirectory "$variant.zip") -DestinationPath $runtime
        $inventory = Get-Content (Join-Path $InputDirectory "$variant-sha256.txt")
        foreach ($line in $inventory) {
            $file = Join-Path $runtime $line.Substring(66)
            if ((Get-FileHash -LiteralPath $file).Hash -ne $line.Substring(0, 64)) { throw 'Runtime hash mismatch' }
        }
        $env:PATH = "$runtime;$env:WINDIR\system32;$env:WINDIR"
        $env:QT_PLUGIN_PATH = $runtime
        $env:SMOOTHMAIL_TEST_SASL_PATH = Join-Path $runtime 'sasl2'
        $env:SMOOTHMAIL_GUI_OUTPUT = $logs
        $record = @{ name = $variant; hashesValid = $true; files = $inventory.Count; tests = @(); appReady = $false }
        $result.variants += $record
        $env:QT_QPA_PLATFORM = 'windows'
        $app = Start-Process (Join-Path $runtime 'smoothmail.exe') -WorkingDirectory $runtime -PassThru
        try {
            $deadline = [DateTime]::UtcNow.AddSeconds(30)
            do {
                $app.Refresh()
                if ($app.HasExited) { throw 'Application exited before creating window' }
                if ($app.MainWindowHandle -ne 0) { break }
                Start-Sleep -Milliseconds 100
            } while ([DateTime]::UtcNow -lt $deadline)
            if ($app.MainWindowHandle -eq 0) { throw 'Application window timeout' }
            $record.appReady = $true
            $record.localModules = @($app.Modules | Where-Object {
                $_.ModuleName -match '^(Qt6|KF6|KPim6|qt6keychain|libsasl|iconv-2|intl-8)'
            } | ForEach-Object {
                if (-not $_.FileName.StartsWith($runtime + '\', [StringComparison]::OrdinalIgnoreCase)) {
                    throw 'Library loaded outside test runtime'
                }
                @{ name = $_.ModuleName; sha256 = (Get-FileHash -LiteralPath $_.FileName).Hash }
            })
        } finally { if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force } }
        $env:QT_QPA_PLATFORM = 'offscreen'
        $probeArguments = @()
        if ($variant -eq 'replacement') { $probeArguments = @('--require-markers') }
        $code = Invoke-TestProcess -File (Join-Path $runtime 'source_replacement_probe.exe') `
            -Directory $runtime -Arguments ($probeArguments + @('--test-only')) -Log (Join-Path $logs 'replacement-probe')
        if ($code -ne 0) { throw 'Replacement API/marker probe failed' }
        $record.probePassed = $true
        if ($variant -eq 'baseline') {
            $code = Invoke-TestProcess -File (Join-Path $runtime 'source_replacement_probe.exe') -Directory $runtime `
                -Arguments @('--require-markers', '--test-only') -Log (Join-Path $logs 'marker-negative')
            if ($code -eq 0) { throw 'Marker negative control unexpectedly passed' }
            $record.markerNegativePassed = $true
        }
        $env:LANGUAGE = 'zz'
        $code = Invoke-TestProcess -File (Join-Path $runtime 'native_runtime_probe.exe') -Directory $runtime `
            -Arguments @('"' + $runtime + '"') -Log (Join-Path $logs 'native-probe')
        if ($code -ne 0) { throw 'GNU runtime conversion/catalog probe failed' }
        $record.nativeProbePassed = $true
        foreach ($test in Get-Content (Join-Path $InputDirectory 'tests.txt')) {
            $test = [string]$test
            $env:QT_QPA_PLATFORM = 'offscreen'
            $arguments = @('-o', ('"' + (Join-Path $logs "$test.txt") + ',txt"'))
            if ($test -eq 'tst_guismoke') {
                $env:QT_QPA_PLATFORM = 'windows'
                $arguments = @('nativeControllerWorkflow', 'editsFoldersSendsMailAndDeletesAccountDuringFetch') + $arguments
            }
            $code = Invoke-TestProcess -File (Join-Path $runtime "$test.exe") -Directory $runtime `
                -Arguments $arguments -Log (Join-Path $logs $test) -Timeout 300000
            $record.tests += @{ name = $test; exitCode = $code }
        }
        $record.failedTests = @($record.tests | Where-Object { $_.exitCode -ne 0 } | ForEach-Object { $_.name })
        $record.success = $record.failedTests.Count -eq 0
    }
    $result.success = @($result.variants | Where-Object { -not $_.success }).Count -eq 0
    $baselineFailures = @($result.variants[0].failedTests)
    $replacementFailures = @($result.variants[1].failedTests)
    $result.equivalentFailureSet = ($baselineFailures -join ';') -eq ($replacementFailures -join ';')
} catch {
    $result.error = $_.Exception.Message
} finally {
    $result | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding UTF8
}
