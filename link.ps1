# One-time setup. Run ONCE in an ELEVATED PowerShell (Win+X -> Terminal (Admin)):
#
#     powershell -ExecutionPolicy Bypass -File "C:\Users\thcol\Documents\CONTENT\MPEPianoRoll\link.ps1"
#
# It replaces  C:\Program Files\Common Files\VST3\MPE Bender.vst3  with a junction
# that points at the build output. After this, EVERY build is instantly the plugin
# FL Studio loads - no copying, no admin, no reinstalling. Just rebuild (or use
# watch.ps1) and re-add / reload the plugin in FL.
#
# Re-run this only if you delete the build folder or move the project.

$ErrorActionPreference = 'Stop'

$buildBundle = Join-Path $PSScriptRoot 'build\MpePianoRoll_artefacts\Release\VST3\MPE Bender.vst3'
$systemDir   = 'C:\Program Files\Common Files\VST3'
$link        = Join-Path $systemDir 'MPE Bender.vst3'

# Make sure we are elevated - creating anything under Program Files needs it.
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
          ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Error "Run this in an elevated PowerShell (Win+X -> Terminal (Admin))."
}

if (-not (Test-Path $buildBundle)) {
    Write-Error "Build it first:  cmake --build build --config Release --target MpePianoRoll_VST3"
}

# Remove whatever is there now (old real folder, old junction, or the obsolete build).
foreach ($p in @($link, (Join-Path $systemDir 'MPE Piano Roll.vst3'))) {
    if (Test-Path $p) {
        $item = Get-Item $p -Force
        if ($item.LinkType) { $item.Delete() }          # junction / symlink
        else { Remove-Item $p -Recurse -Force }          # real directory
        Write-Host "Removed $p"
    }
}

New-Item -ItemType Junction -Path $link -Target $buildBundle | Out-Null

Write-Host ""
Write-Host "Linked:  $link"
Write-Host "     ->  $buildBundle"
Write-Host ""
Write-Host "Now: FL -> Manage plugins -> Find installed plugins, search 'bender'."
Write-Host "From now on, rebuilding is all you need."
