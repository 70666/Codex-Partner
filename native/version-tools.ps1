Set-StrictMode -Version Latest

function Get-RepositoryVersion {
    param([string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path)

    $versionFile = Join-Path $RepositoryRoot 'version.env'
    if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
        throw "Missing version file: $versionFile"
    }
    $line = Get-Content -LiteralPath $versionFile |
        Where-Object { $_ -match '^MARKETING_VERSION=(\d+\.\d+\.\d+)$' } |
        Select-Object -First 1
    if (-not $line -or $line -notmatch '^MARKETING_VERSION=(\d+\.\d+\.\d+)$') {
        throw 'version.env must contain MARKETING_VERSION=x.y.z.'
    }
    return $Matches[1]
}

function Assert-NativeProductVersion {
    param([string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path)

    $expected = Get-RepositoryVersion -RepositoryRoot $RepositoryRoot
    $headerPath = Join-Path $RepositoryRoot 'native\src\version.h'
    if (-not (Test-Path -LiteralPath $headerPath -PathType Leaf)) {
        throw "Missing native version header: $headerPath"
    }
    $header = Get-Content -LiteralPath $headerPath -Raw
    $parts = @($expected.Split('.') | ForEach-Object { [int]$_ })
    $expectedMacros = [ordered]@{
        CODEX_PARTNER_VERSION_MAJOR = [string]$parts[0]
        CODEX_PARTNER_VERSION_MINOR = [string]$parts[1]
        CODEX_PARTNER_VERSION_PATCH = [string]$parts[2]
        CODEX_PARTNER_VERSION_BUILD = '0'
        CODEX_PARTNER_VERSION_TUPLE = "$($parts[0]),$($parts[1]),$($parts[2]),0"
        CODEX_PARTNER_VERSION_STRING = "`"$expected`""
        CODEX_PARTNER_VERSION_WIDE = "L`"$expected`""
    }
    $failures = [Collections.Generic.List[string]]::new()
    foreach ($entry in $expectedMacros.GetEnumerator()) {
        $pattern = '(?m)^#define\s+' + [regex]::Escape($entry.Key) + '\s+(.+?)\s*$'
        if ($header -notmatch $pattern -or $Matches[1].Replace(' ', '') -ne $entry.Value.Replace(' ', '')) {
            $failures.Add("$($entry.Key) does not match $expected")
        }
    }
    if ($failures.Count -gt 0) {
        throw "Product version consistency failed:`n - $($failures -join "`n - ")"
    }
    return $expected
}
