param(
    [string]$Version,
    [string]$BuildDirectory = "build",
    [string]$OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

if (-not $Version) {
    $cmakeText = Get-Content (Join-Path $projectRoot "CMakeLists.txt") -Raw
    if ($cmakeText -notmatch 'project\(CaptureView VERSION ([0-9]+\.[0-9]+\.[0-9]+)') {
        throw "Could not read the CaptureView version from CMakeLists.txt."
    }
    $Version = $Matches[1]
}

if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw "Version must use the MAJOR.MINOR.PATCH form."
}

$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $projectRoot $BuildDirectory
}
$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
} else {
    Join-Path $projectRoot $OutputDirectory
}

$executable = @(
    (Join-Path $buildRoot "Release\CaptureView.exe"),
    (Join-Path $buildRoot "CaptureView.exe")
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $executable) {
    throw "CaptureView.exe was not found under '$buildRoot'. Build Release first."
}

$packageName = "CaptureView-$Version-x64"
$packageDirectory = Join-Path $outputRoot $packageName
$zipPath = Join-Path $outputRoot "$packageName.zip"
$checksumPath = Join-Path $outputRoot "$packageName.sha256"

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if (Test-Path $packageDirectory) {
    Remove-Item $packageDirectory -Recurse -Force
}
Remove-Item $zipPath, $checksumPath -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $packageDirectory | Out-Null

Copy-Item $executable (Join-Path $packageDirectory "CaptureView.exe")
Copy-Item (Join-Path $projectRoot "LICENSE") `
    (Join-Path $packageDirectory "LICENSE.txt")
Copy-Item (Join-Path $projectRoot "README.md") $packageDirectory

Compress-Archive -Path (Join-Path $packageDirectory "*") -DestinationPath $zipPath
$hash = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($zipPath))" |
    Set-Content -Path $checksumPath -Encoding ascii

Write-Host "Created $zipPath"
Write-Host "Created $checksumPath"

if ($env:GITHUB_OUTPUT) {
    "version=$Version" | Add-Content $env:GITHUB_OUTPUT
    "zip_path=$zipPath" | Add-Content $env:GITHUB_OUTPUT
    "checksum_path=$checksumPath" | Add-Content $env:GITHUB_OUTPUT
}
