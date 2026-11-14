param(
    [Parameter(Mandatory = $true)][string]$RebuildRoot,
    [Parameter(Mandatory = $true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path $RebuildRoot).Path
if (Test-Path $Output) { throw 'Choose a new modification review output file' }
$records = @()
foreach ($id in @('qtsvg', 'kcodecs')) {
    $base = Join-Path "$root/src" $id
    $target = 'Svg'
    $subdir = 'src/svg'
    $symbol = 'smoothmail_source_replacement_svg'
    if ($id -eq 'kcodecs') {
        $base = Join-Path "$root/patched" $id
        $target = 'KF6Codecs'
        $subdir = 'src'
        $symbol = 'smoothmail_source_replacement_codecs'
    }
    $modified = Join-Path "$root/modified/src" $id
    $cmake = "$subdir/CMakeLists.txt"
    $marker = "$subdir/replacement-marker.cpp"
    $files = @(Get-ChildItem $base -File -Recurse -Force)
    $changedFiles = @(Get-ChildItem $modified -File -Recurse -Force)
    if ($changedFiles.Count -ne $files.Count + 1) { throw 'Unexpected files in modified source' }
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($base.Length + 1).Replace('\', '/')
        $copy = Join-Path $modified $relative
        if ($relative -eq $cmake) {
            $expected = (Get-Content $file.FullName -Raw).Replace("`r`n", "`n") + "`ntarget_sources($target PRIVATE replacement-marker.cpp)`n"
            if ((Get-Content $copy -Raw).Replace("`r`n", "`n") -cne $expected) { throw 'Unexpected modified CMake input' }
        } elseif ((Get-FileHash $file.FullName).Hash -ne (Get-FileHash $copy).Hash) {
            throw "Unexpected library source change: $relative"
        }
    }
    $expected = "extern `"C`" __declspec(dllexport) int $symbol() { return 45654; }`n"
    if ((Get-Content (Join-Path $modified $marker) -Raw).Replace("`r`n", "`n") -cne $expected) {
        throw 'Unexpected exported marker source'
    }
    $records += @{
        component = $id; original_files_verified = $files.Count
        cmake = $cmake; cmake_sha256 = (Get-FileHash (Join-Path $modified $cmake)).Hash.ToLowerInvariant()
        marker = $marker; marker_sha256 = (Get-FileHash (Join-Path $modified $marker)).Hash.ToLowerInvariant()
    }
}
@{ review_only = $true; modifications = $records } | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $Output -Encoding UTF8
'Verified both library modifications contain only the added marker and its CMake target source entry.'
