# Publish a new MPE Bender release + point the auto-updater at it.
#
#   1. bump the version in CMakeLists.txt:  project(MpePianoRoll VERSION x.y.z)
#   2. commit your code changes
#   3. powershell -ExecutionPolicy Bypass -File publish-update.ps1 -Notes "what changed"
#
# It will:
#   - build the VST3 with the update URL baked in
#   - zip  MPE Bender.vst3 + INSTALL.txt  ->  dist\MPE-Bender-x.y.z.vst3.zip
#   - create GitHub release  vx.y.z  with that zip attached
#   - rewrite latest.json and push it, so every friend's copy sees the update
#
# Friends who already run a self-updating build get it automatically on their next
# DAW restart. New friends download the zip from the Releases page.

param(
    [string] $Repo    = "trymhc/mpe-bender",
    [string] $Notes   = "",
    [switch] $SkipBuild,
    [switch] $DraftRelease
)

$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$build = Join-Path $root 'build'
$dist  = Join-Path $root 'dist'

function Need($name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $fallbacks = @{
        cmake = 'C:\Program Files\CMake\bin\cmake.exe'
        gh    = 'C:\Program Files\GitHub CLI\gh.exe'
        git   = 'C:\Program Files\Git\cmd\git.exe'
    }
    if ($fallbacks.ContainsKey($name) -and (Test-Path $fallbacks[$name])) { return $fallbacks[$name] }
    throw "$name not found on PATH"
}
$cmake = Need cmake
$gh    = Need gh
$git   = Need git

# run a native exe, return its stdout lines; throw with context on non-zero exit
function Run($exe, [string[]] $arguments) {
    $out = & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$([IO.Path]::GetFileName($exe)) $($arguments -join ' ') -> exit $LASTEXITCODE`n$out" }
    return $out
}

# --- version ---
$cml = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cml -notmatch 'project\(MpePianoRoll\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    throw "Couldn't read version from CMakeLists.txt"
}
$version     = $Matches[1]
$tag         = "v$version"
$manifestUrl = "https://raw.githubusercontent.com/$Repo/main/latest.json"
$zipName     = "MPE-Bender-$version.vst3.zip"
$download    = "https://github.com/$Repo/releases/download/$tag/$zipName"
$notesText   = if ($Notes) { $Notes } else { "MPE Bender $version" }
Write-Host "Publishing $tag" -ForegroundColor Cyan

# make sure gh is logged in and this tag isn't already released
Run $gh @('auth','status') | Out-Null
$existingTags = @(Run $gh @('release','list','--repo',$Repo,'--json','tagName','--jq','.[].tagName'))
if ($existingTags -contains $tag) {
    throw "Release $tag already exists. Bump project(MpePianoRoll VERSION ...) in CMakeLists.txt first."
}

# --- build ---
if (-not $SkipBuild) {
    Write-Host "Configuring + building Release..." -ForegroundColor Yellow
    Run $cmake @('-S',$root,'-B',$build,"-DMPE_BENDER_UPDATE_URL=$manifestUrl") | Out-Null
    Run $cmake @('--build',$build,'--config','Release','--target','MpePianoRoll_VST3') | Out-Null
}

$vst3 = Join-Path $build 'MpePianoRoll_artefacts\Release\VST3\MPE Bender.vst3'
if (-not (Test-Path $vst3)) { throw "Not found: $vst3" }

# --- zip (bundle + INSTALL.txt) ---
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$stage = Join-Path $dist 'MPE Bender'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item $vst3 (Join-Path $stage 'MPE Bender.vst3') -Recurse
Copy-Item (Join-Path $dist 'INSTALL.txt') $stage
$zipPath = Join-Path $dist $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force
Write-Host "  $zipPath" -ForegroundColor Green

# --- GitHub release ---
$ghArgs = @('release','create',$tag,'--repo',$Repo,'--title',"MPE Bender $version",'--notes',$notesText,$zipPath)
if ($DraftRelease) { $ghArgs += '--draft' }
Run $gh $ghArgs

# --- manifest: rewrite, commit, push ---
$manifest = [ordered]@{ version = $version; download = $download; notes = $notesText }
$manifestPath = Join-Path $root 'latest.json'
Set-Content -Path $manifestPath -Value (($manifest | ConvertTo-Json)) -Encoding utf8

Run $git @('-C',$root,'add','latest.json') | Out-Null
$pending = & $git -C $root status --porcelain latest.json
if ($pending) {
    Run $git @('-C',$root,'commit','-m',"Release $version") | Out-Null
}
Run $git @('-C',$root,'push','origin','main') | Out-Null

Write-Host ""
Write-Host "Done. $tag is live:" -ForegroundColor Green
Write-Host "  https://github.com/$Repo/releases/tag/$tag"
Write-Host "  friends on a self-updating build get it on their next DAW restart."
