# Rebuild-on-save. Run in a normal PowerShell (no admin needed):
#
#     powershell -ExecutionPolicy Bypass -File "C:\Users\thcol\Documents\CONTENT\MPEPianoRoll\watch.ps1"
#
# Watches Source\ and CMakeLists.txt; every time you save, it rebuilds the VST3.
# With link.ps1 done once, the rebuilt binary IS what FL loads - just reload the
# plugin instance in FL (remove/re-add it, or reopen the project) to hear changes.
# Ctrl+C to stop.

$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$src   = Join-Path $root 'Source'
$cml   = Join-Path $root 'CMakeLists.txt'
$build = Join-Path $root 'build'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = 'C:\Program Files\CMake\bin\cmake.exe' }
if (-not (Test-Path $cmake)) { Write-Error "cmake not found on PATH." }

function Get-Sig {
    $files = @(Get-ChildItem $src -File -Recurse -ErrorAction SilentlyContinue)
    if (Test-Path $cml) { $files += Get-Item $cml }
    ($files | Sort-Object FullName |
        ForEach-Object { "$($_.FullName)|$($_.Length)|$($_.LastWriteTimeUtc.Ticks)" }) -join "`n"
}

function Invoke-Build {
    Write-Host ("`n[{0}] building..." -f (Get-Date -Format HH:mm:ss)) -ForegroundColor Cyan

    for ($attempt = 1; $attempt -le 40; $attempt++) {
        $out = & $cmake --build $build --config Release --target MpePianoRoll_VST3 2>&1
        $ok  = ($LASTEXITCODE -eq 0)

        if ($ok) {
            Write-Host ("[{0}] OK - reload MPE Bender in FL" -f (Get-Date -Format HH:mm:ss)) -ForegroundColor Green
            try { [console]::Beep(880,120) } catch {}
            return
        }

        # LNK1104 = the .vst3 is loaded in FL. Wait for it to be freed and retry.
        if ($out -match 'LNK1104') {
            if ($attempt -eq 1) {
                Write-Host "  .vst3 is locked - remove MPE Bender from FL's rack; retrying..." -ForegroundColor Yellow
            }
            Start-Sleep -Seconds 3
            continue
        }

        $out | Select-String -Pattern 'error C\d|error LNK|error MSB|: error|FAILED' |
            ForEach-Object { Write-Host "  $($_.Line.Trim())" }
        Write-Host ("[{0}] BUILD FAILED" -f (Get-Date -Format HH:mm:ss)) -ForegroundColor Red
        try { [console]::Beep(220,300) } catch {}
        return
    }

    Write-Host ("[{0}] gave up waiting for the .vst3 to unlock" -f (Get-Date -Format HH:mm:ss)) -ForegroundColor Red
}

if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
    Write-Host "Configuring (first run)..." -ForegroundColor Yellow
    & $cmake -S $root -B $build | Out-Null
}

Write-Host "Watching Source\ + CMakeLists.txt. Ctrl+C to stop." -ForegroundColor Yellow
Invoke-Build
$last = Get-Sig

while ($true) {
    Start-Sleep -Milliseconds 800
    $now = Get-Sig
    if ($now -ne $last) {
        Start-Sleep -Milliseconds 400    # let the editor finish writing the file
        $last = Get-Sig
        Invoke-Build
    }
}
