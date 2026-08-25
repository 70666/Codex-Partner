param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$TestsOnly
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\version-tools.ps1"
$null = Assert-NativeProductVersion
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer (vswhere.exe) was not found."
}

$installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installPath) {
    throw "Visual Studio with the Desktop development with C++ workload was not found."
}

$msbuild = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild was not found at $msbuild."
}

$buildTarget = if ($TestsOnly) {
    Join-Path $PSScriptRoot "CodexPartner.Tests.vcxproj"
} else {
    Join-Path $PSScriptRoot "CodexPartner.sln"
}
$builtExecutable = if ($TestsOnly) { "CodexPartner.Tests.exe" } else { "CodexPartner.exe" }

# Codex Desktop can expose both `PATH` and `Path` in the raw Windows
# environment block. .NET Framework's MSBuild tool launcher treats those as a
# duplicate dictionary key and fails before CL.exe starts. Remove only the
# injected uppercase duplicate when both spellings are present; this changes
# the current build process only.
$rawEnvironment = & "$env:SystemRoot\System32\cmd.exe" /d /c set
$pathEntries = @($rawEnvironment | Where-Object { $_.StartsWith("PATH=", [System.StringComparison]::Ordinal) -or $_.StartsWith("Path=", [System.StringComparison]::Ordinal) })
if ($pathEntries.Count -gt 1) {
    if ($PSVersionTable.PSEdition -ne "Core") {
        throw "The current process contains duplicate PATH variables. Run build.ps1 from PowerShell 7 or a fresh terminal."
    }
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $msbuild
    $startInfo.UseShellExecute = $false
    $startInfo.ArgumentList.Add($buildTarget)
    $startInfo.ArgumentList.Add("/m")
    $startInfo.ArgumentList.Add("/restore")
    $startInfo.ArgumentList.Add("/p:Configuration=$Configuration")
    $startInfo.ArgumentList.Add("/p:Platform=x64")
    $startInfo.Environment.Clear()
    Get-ChildItem Env: | ForEach-Object { $startInfo.Environment[$_.Name] = $_.Value }
    $buildProcess = [System.Diagnostics.Process]::Start($startInfo)
    $buildProcess.WaitForExit()
    if ($buildProcess.ExitCode -ne 0) {
        exit $buildProcess.ExitCode
    }
    Write-Host "Built $PSScriptRoot\build\x64\$Configuration\$builtExecutable"
    exit 0
}

& $msbuild $buildTarget /m /restore /p:Configuration=$Configuration /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Built $PSScriptRoot\build\x64\$Configuration\$builtExecutable"
