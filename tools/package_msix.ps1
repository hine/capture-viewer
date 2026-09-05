param(
    [Parameter(Mandatory = $true)]
    [string]$IdentityName,

    [Parameter(Mandatory = $true)]
    [string]$Publisher,

    [Parameter(Mandatory = $true)]
    [string]$PublisherDisplayName,

    [string]$Version = "1.1.0.0",
    [string]$BuildDirectory = "build",
    [string]$OutputDirectory = "dist-msix",
    [switch]$RequireSymbols
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

if ($Version -notmatch '^[1-9][0-9]{0,4}(\.[0-9]{1,5}){3}$') {
    throw "MSIX Version must use four numeric parts and a nonzero major version."
}
foreach ($part in $Version.Split('.')) {
    if ([int]$part -gt 65535) {
        throw "Each MSIX version part must be between 0 and 65535."
    }
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

$makeAppx = Get-ChildItem `
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\makeappx.exe" `
    -ErrorAction SilentlyContinue |
    Sort-Object { [version]$_.Directory.Parent.Name } -Descending |
    Select-Object -First 1
if (-not $makeAppx) {
    throw "MakeAppx.exe was not found. Install a Windows 10 or Windows 11 SDK."
}

$packageName = "GdW-CaptureView-$Version-x64"
$stagingRoot = Join-Path $outputRoot "staging"
$assetsRoot = Join-Path $stagingRoot "Assets"
$msixPath = Join-Path $outputRoot "$packageName.msix"
$symbolsPath = Join-Path $outputRoot "$packageName.appxsym"
$uploadPath = Join-Path $outputRoot "$packageName.msixupload"

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if (Test-Path $stagingRoot) {
    Remove-Item $stagingRoot -Recurse -Force
}
Remove-Item $msixPath, $symbolsPath, $uploadPath -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $assetsRoot -Force | Out-Null

Copy-Item $executable (Join-Path $stagingRoot "CaptureView.exe")
Copy-Item (Join-Path $projectRoot "LICENSE") (Join-Path $stagingRoot "LICENSE.txt")
Copy-Item (Join-Path $projectRoot "packaging\msix\Assets\*") $assetsRoot

function Escape-Xml([string]$Value) {
    return [Security.SecurityElement]::Escape($Value)
}

$manifest = Get-Content `
    (Join-Path $projectRoot "packaging\msix\AppxManifest.xml.in") -Raw
$manifest = $manifest.Replace("@IDENTITY_NAME@", (Escape-Xml $IdentityName))
$manifest = $manifest.Replace("@PUBLISHER@", (Escape-Xml $Publisher))
$manifest = $manifest.Replace(
    "@PUBLISHER_DISPLAY_NAME@", (Escape-Xml $PublisherDisplayName))
$manifest = $manifest.Replace("@VERSION@", $Version)
$manifestPath = Join-Path $stagingRoot "AppxManifest.xml"
[IO.File]::WriteAllText(
    $manifestPath, $manifest, [Text.UTF8Encoding]::new($false))

& $makeAppx.FullName pack /d $stagingRoot /p $msixPath /o
if ($LASTEXITCODE -ne 0) {
    throw "MakeAppx failed with exit code $LASTEXITCODE."
}

$pdb = @(
    [IO.Path]::ChangeExtension($executable, ".pdb"),
    (Join-Path $buildRoot "CaptureView.pdb"),
    (Join-Path $buildRoot "Release\CaptureView.pdb")
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($RequireSymbols -and -not $pdb) {
    throw "CaptureView.pdb was not found under '$buildRoot'. Store upload symbols are required."
}
if (Test-Path $pdb) {
    $symbolStaging = Join-Path $outputRoot "symbols"
    if (Test-Path $symbolStaging) {
        Remove-Item $symbolStaging -Recurse -Force
    }
    New-Item -ItemType Directory -Path $symbolStaging | Out-Null
    Copy-Item $pdb (Join-Path $symbolStaging "CaptureView.pdb")
    Compress-Archive -Path (Join-Path $symbolStaging "*") `
        -DestinationPath ($symbolsPath + ".zip")
    Move-Item ($symbolsPath + ".zip") $symbolsPath
}

$uploadStaging = Join-Path $outputRoot "upload"
if (Test-Path $uploadStaging) {
    Remove-Item $uploadStaging -Recurse -Force
}
New-Item -ItemType Directory -Path $uploadStaging | Out-Null
Copy-Item $msixPath $uploadStaging
if (Test-Path $symbolsPath) {
    Copy-Item $symbolsPath $uploadStaging
}
Compress-Archive -Path (Join-Path $uploadStaging "*") `
    -DestinationPath ($uploadPath + ".zip")
Move-Item ($uploadPath + ".zip") $uploadPath

Write-Host "Created $msixPath"
Write-Host "Created $uploadPath"

if ($env:GITHUB_OUTPUT) {
    "msix_path=$msixPath" | Add-Content $env:GITHUB_OUTPUT
    "upload_path=$uploadPath" | Add-Content $env:GITHUB_OUTPUT
}
