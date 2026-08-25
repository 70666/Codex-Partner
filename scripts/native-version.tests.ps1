#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $repositoryRoot 'native\version-tools.ps1')

$actual = Assert-NativeProductVersion -RepositoryRoot $repositoryRoot
if ($actual -ne '0.0.0') {
    throw "Expected initial version 0.0.0, found $actual."
}
Write-Host "Version contract passed: $actual"
