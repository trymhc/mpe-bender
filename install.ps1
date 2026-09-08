# Installs the freshly built MPE Bender.vst3 into the system VST3 folder that
# FL Studio scans. Run this in an ELEVATED PowerShell (Win+X -> Terminal (Admin)):
#
#     powershell -ExecutionPolicy Bypass -File "C:\Users\thcol\Documents\CONTENT\MPEPianoRoll\install.ps1"
#
# Then in FL: Options -> Manage plugins -> Find installed plugins, search "bender".

$ErrorActionPreference = 'Stop'

$src  = Join-Path $PSScriptRoot 'build\MpePianoRoll_artefacts\Release\VST3\MPE Bender.vst3'
$dest = 'C:\Program Files\Common Files\VST3'

if (-not (Test-Path $src)) {
    Write-Error "Not built yet. Run:  cmake --build build --config Release --target MpePianoRoll_VST3"
}

# Remove the obsolete MIDI-effect build if it is still around.
$old = Join-Path $dest 'MPE Piano Roll.vst3'
if (Test-Path $old) { Remove-Item -Recurse -Force $old; Write-Host "Removed old MPE Piano Roll.vst3" }

Copy-Item -Recurse -Force $src $dest
Write-Host "Installed: $dest\MPE Bender.vst3"
