# Package a new MPE Bender release for the built-in auto-updater.
#
#   powershell -ExecutionPolicy Bypass -File publish-update.ps1 `
#       -BaseUrl "https://github.com/you/mpe-bender/releases/download/v0.3.0" `
#       -Notes "Scale viewer, per-note mute, undo/redo"
#
# Produces  dist\MPE Bender-<version>.vst3.zip  and  dist\latest.json
# Then:
#   1. Upload BOTH files to <BaseUrl> (e.g. attach them to a GitHub Release).
#   2. Host latest.json at a stable URL and build the plugin once with
#        -DMPE_BENDER_UPDATE_URL="<that stable latest.json URL>"
#      Friends' copies check it on startup and self-install on the next DAW restart.
#
# The version comes from the "project(MpePianoRoll VERSION x.y.z)" line in CMakeLists.txt.
# Bump that before publishing.

param(
    [Parameter(Mandatory = $true)] [string] $BaseUrl,
    [string] $Notes = "",
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$build = Join-Path $root 'build'
$dist  = Join-Path $root 'dist'

# --- version from CMakeLists.txt ---
$cml = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cml -notmatch 'project\(MpePianoRoll\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    Write-Error "Couldn't read version from CMakeLists.txt"
}
$version = $Matches[1]
Write-Host "Version $version" -ForegroundColor Cyan

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = 'C:\Program Files\CMake\bin\cmake.exe' }

if (-not $SkipBuild) {
    Write-Host "Building Release..." -ForegroundColor Yellow
    & $cmake --build $build --config Release --target MpePianoRoll_VST3 | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Error "Build failed" }
}

$vst3 = Join-Path $build 'MpePianoRoll_artefacts\Release\VST3\MPE Bender.vst3'
if (-not (Test-Path $vst3)) { Write-Error "Not found: $vst3" }

New-Item -ItemType Directory -Force -Path $dist | Out-Null
$zipName = "MPE Bender-$version.vst3.zip"
$zipPath = Join-Path $dist $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

Write-Host "Zipping bundle -> dist\$zipName" -ForegroundColor Yellow
# Compress the bundle folder itself so the archive root is "MPE Bender.vst3\..."
Compress-Archive -Path $vst3 -DestinationPath $zipPath -CompressionLevel Optimal

$download = ($BaseUrl.TrimEnd('/')) + '/' + [uri]::EscapeDataString($zipName)
$manifest = [ordered]@{
    version  = $version
    download = $download
    notes    = $Notes
}
$manifestPath = Join-Path $dist 'latest.json'
$manifest | ConvertTo-Json | Set-Content -Path $manifestPath -Encoding utf8

Write-Host ""
Write-Host "Wrote:" -ForegroundColor Green
Write-Host "  $zipPath"
Write-Host "  $manifestPath"
Write-Host ""
Write-Host "latest.json:" -ForegroundColor Green
Get-Content $manifestPath
Write-Host ""
Write-Host "Next: upload both files so they're reachable at:" -ForegroundColor Green
Write-Host "  $download"
Write-Host "  <stable URL for latest.json>  <- build friends' copies with -DMPE_BENDER_UPDATE_URL=this"
