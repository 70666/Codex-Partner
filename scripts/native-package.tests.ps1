#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $repositoryRoot 'native\version-tools.ps1')
$version = Assert-NativeProductVersion -RepositoryRoot $repositoryRoot
$nativeRoot = Join-Path $repositoryRoot 'native'
$packageRoot = Join-Path $nativeRoot 'build\package'

& (Join-Path $nativeRoot 'package.ps1') -Configuration Release -SkipBuild

$archive = Join-Path $packageRoot "Codex-Partner-$version-windows-x64.zip"
$archiveSidecar = Join-Path $packageRoot "Codex-Partner-$version-windows-x64.sha256"
$standalone = Join-Path $packageRoot "Codex-Partner-$version-native-windows-x64.exe"
$standaloneSidecar = "$standalone.sha256"
foreach ($path in @($archive, $archiveSidecar, $standalone, $standaloneSidecar)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing package asset: $path" }
}

$firstArchiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
$firstExecutableHash = (Get-FileHash -LiteralPath $standalone -Algorithm SHA256).Hash
& (Join-Path $nativeRoot 'package.ps1') -Configuration Release -SkipBuild
$secondArchiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
$secondExecutableHash = (Get-FileHash -LiteralPath $standalone -Algorithm SHA256).Hash
if ($firstArchiveHash -ne $secondArchiveHash -or $firstExecutableHash -ne $secondExecutableHash) {
    throw 'Release assets are not reproducible.'
}

function Assert-Sidecar([string]$Asset, [string]$Sidecar) {
    $expected = (Get-FileHash -LiteralPath $Asset -Algorithm SHA256).Hash.ToLowerInvariant()
    $line = Get-Content -LiteralPath $Sidecar | Select-Object -First 1
    $parts = $line -split '\s+', 2
    if ($parts[0].ToLowerInvariant() -ne $expected -or $parts[1].Trim() -ne (Split-Path $Asset -Leaf)) {
        throw "Invalid SHA-256 sidecar: $Sidecar"
    }
}
Assert-Sidecar $archive $archiveSidecar
Assert-Sidecar $standalone $standaloneSidecar

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    $actual = @($zip.Entries | ForEach-Object FullName)
    $expected = @('BUILD-INFO.txt','CodexPartner.exe','LICENSE','NOTICE','README.md')
    if (($actual -join '|') -ne ($expected -join '|')) {
        throw "Unexpected archive contents: $($actual -join ', ')"
    }
} finally { $zip.Dispose() }

Write-Host 'Package contract passed.'
