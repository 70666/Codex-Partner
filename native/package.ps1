param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\version-tools.ps1"
$expectedVersion = Assert-NativeProductVersion

function New-DeterministicZip {
    param(
        [Parameter(Mandatory)][string]$SourceDirectory,
        [Parameter(Mandatory)][string]$DestinationPath
    )

    Add-Type -AssemblyName System.IO.Compression
    $fixedTimestamp = [DateTimeOffset]::Parse('2000-01-01T00:00:00Z')
    $sourcePrefix = $SourceDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $files = @(Get-ChildItem -LiteralPath $SourceDirectory -File -Recurse | Sort-Object {
        $_.FullName.Substring($sourcePrefix.Length).Replace('\', '/')
    })

    $archiveStream = [IO.File]::Open($DestinationPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $zip = [IO.Compression.ZipArchive]::new($archiveStream, [IO.Compression.ZipArchiveMode]::Create, $false)
        try {
            foreach ($file in $files) {
                $entryName = $file.FullName.Substring($sourcePrefix.Length).Replace('\', '/')
                $entry = $zip.CreateEntry($entryName, [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = $fixedTimestamp
                $input = [IO.File]::OpenRead($file.FullName)
                try {
                    $output = $entry.Open()
                    try {
                        $input.CopyTo($output)
                    } finally {
                        $output.Dispose()
                    }
                } finally {
                    $input.Dispose()
                }
            }
        } finally {
            $zip.Dispose()
        }
    } finally {
        $archiveStream.Dispose()
    }
}

if (-not $SkipBuild) {
    & "$PSScriptRoot\build.ps1" -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "build"))
$outputRoot = Join-Path $buildRoot "x64\$Configuration"
$executable = Join-Path $outputRoot "CodexPartner.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Native executable was not found at $executable. Run build.ps1 first."
}

$versionInfo = (Get-Item -LiteralPath $executable).VersionInfo
$version = $versionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "The executable contains an invalid product version: $version"
}
if ($version -ne $expectedVersion) {
    throw "The executable product version is $version, expected repository version $expectedVersion. Rebuild before packaging."
}

$suffix = if ($Configuration -eq "Release") { "" } else { "-$($Configuration.ToLowerInvariant())" }
$archiveName = "Codex-Partner-$version-windows-x64$suffix"
$standaloneName = "Codex-Partner-$version-native-windows-x64$suffix.exe"
$packageRoot = Join-Path $buildRoot "package"
$staging = [System.IO.Path]::GetFullPath((Join-Path $packageRoot $archiveName))
$archive = [System.IO.Path]::GetFullPath((Join-Path $packageRoot "$archiveName.zip"))
$checksum = [System.IO.Path]::GetFullPath((Join-Path $packageRoot "$archiveName.sha256"))
$standalone = [System.IO.Path]::GetFullPath((Join-Path $packageRoot $standaloneName))
$standaloneChecksum = [System.IO.Path]::GetFullPath((Join-Path $packageRoot "$standaloneName.sha256"))

$expectedPrefix = $buildRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
foreach ($target in @($staging, $archive, $checksum, $standalone, $standaloneChecksum)) {
    if (-not $target.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to package outside the native build directory: $target"
    }
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive -Force
}
if (Test-Path -LiteralPath $checksum) {
    Remove-Item -LiteralPath $checksum -Force
}
if (Test-Path -LiteralPath $standalone) {
    Remove-Item -LiteralPath $standalone -Force
}
if (Test-Path -LiteralPath $standaloneChecksum) {
    Remove-Item -LiteralPath $standaloneChecksum -Force
}
New-Item -ItemType Directory -Path $staging | Out-Null

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
Copy-Item -LiteralPath $executable -Destination $staging
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "README.md") -Destination $staging
Copy-Item -LiteralPath (Join-Path $repositoryRoot "LICENSE") -Destination $staging
Copy-Item -LiteralPath (Join-Path $repositoryRoot "NOTICE") -Destination $staging

$buildInfo = @(
    "Product: Codex Partner"
    "Version: $version"
    "Architecture: x64"
    "Configuration: $Configuration"
    "Runtime: statically linked Microsoft C/C++ runtime"
)
[IO.File]::WriteAllText(
    (Join-Path $staging "BUILD-INFO.txt"),
    (($buildInfo -join "`n") + "`n"),
    [Text.UTF8Encoding]::new($false)
)

New-DeterministicZip -SourceDirectory $staging -DestinationPath $archive
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($checksum, "$archiveHash  $([IO.Path]::GetFileName($archive))`n", [Text.ASCIIEncoding]::new())

# Publish the same statically linked binary directly for the shortest trial
# path: download, verify if desired, and run without extracting an archive.
Copy-Item -LiteralPath $executable -Destination $standalone
$standaloneHash = (Get-FileHash -LiteralPath $standalone -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($standaloneChecksum, "$standaloneHash  $([IO.Path]::GetFileName($standalone))`n", [Text.ASCIIEncoding]::new())

Write-Host "Created $archive"
Write-Host "SHA-256 $archiveHash"
Write-Host "Created $standalone"
Write-Host "SHA-256 $standaloneHash"
