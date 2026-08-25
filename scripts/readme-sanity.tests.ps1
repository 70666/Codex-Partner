#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$readme = Get-Content -LiteralPath (Join-Path $root 'README.md') -Raw
foreach ($required in @('C++20','native\build.ps1','scripts\local-check.ps1','70666/Codex-Partner')) {
    if (-not $readme.Contains($required)) { throw "README is missing: $required" }
}
Write-Host 'README boundary contract passed.'
