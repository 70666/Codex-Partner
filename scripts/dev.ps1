#Requires -Version 5.1
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$SkipBuild,
    [switch]$Minimized
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$nativeRoot = Join-Path $repositoryRoot 'native'

if (-not $SkipBuild) {
    & (Join-Path $nativeRoot 'build.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$executable = Join-Path $nativeRoot "build\x64\$Configuration\CodexPartner.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Codex Partner executable not found: $executable"
}

$arguments = if ($Minimized) { @('--minimized') } else { @() }
Start-Process -FilePath $executable -ArgumentList $arguments
