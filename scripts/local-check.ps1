#Requires -Version 5.1
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot

function Invoke-Step([string]$Name, [scriptblock]$Action) {
    Write-Host "`n==> $Name" -ForegroundColor Cyan
    & $Action
    if (-not $?) { throw "$Name failed." }
    if (Test-Path variable:global:LASTEXITCODE) {
        if ($global:LASTEXITCODE -ne 0) { throw "$Name failed with exit code $global:LASTEXITCODE" }
        $global:LASTEXITCODE = 0
    }
}

Push-Location $repositoryRoot
try {
    Invoke-Step 'Repository boundary' { powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\readme-sanity.tests.ps1 }
    Invoke-Step 'Version contract' { powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\native-version.tests.ps1 }
    Invoke-Step 'C++ build' { powershell.exe -NoProfile -ExecutionPolicy Bypass -File native\build.ps1 -Configuration $Configuration }
    Invoke-Step 'C++ tests' { & "native\build\x64\$Configuration\CodexPartner.Tests.exe" }
    if ($Configuration -eq 'Release') {
        Invoke-Step 'Release package' { powershell.exe -NoProfile -ExecutionPolicy Bypass -File native\package.ps1 -Configuration Release -SkipBuild }
        Invoke-Step 'Package contract' { powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\native-package.tests.ps1 }
    }
} finally {
    Pop-Location
}
Write-Host "`nCodex Partner checks passed." -ForegroundColor Green
