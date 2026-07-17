# setup.ps1 - Zetla build pipeline
# Run from project root: .\scripts\setup.ps1
#
# Downloads Vosk speech model and builds the release APK.
# All other dependencies (Python, native libs) are committed in the repo.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

$VOSK_MODEL_URL = "https://alphacephei.com/vosk/models/vosk-model-en-us-0.22-lgraph.zip"
$VOSK_MODEL_DIR = "$Root\Zetla\app\src\main\assets\model-en-us"

function Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Ok($msg)   { Write-Host "  [OK] $msg" -ForegroundColor Green }
function Fail($msg) { Write-Host "  [FAIL] $msg" -ForegroundColor Red; exit 1 }

Step "Downloading Vosk model"
if (-not (Test-Path "$VOSK_MODEL_DIR\am\final.mdl")) {
    $zipFile = "$env:TEMP\vosk-model.zip"
    try { Invoke-WebRequest -Uri $VOSK_MODEL_URL -OutFile $zipFile }
    catch { Fail "Vosk download failed. Download manually from: $VOSK_MODEL_URL" }
    $extractDir = "$env:TEMP\vosk-extract"
    Expand-Archive -Path $zipFile -DestinationPath $extractDir -Force
    $extracted = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1
    if ($extracted) { Copy-Item -Recurse -Force "$($extracted.FullName)\*" $VOSK_MODEL_DIR; Ok "Vosk model installed" }
    else { Fail "Could not find extracted Vosk model" }
    Remove-Item -Recurse -Force $zipFile, $extractDir -ErrorAction SilentlyContinue
} else { Ok "Vosk model already present" }

Step "Building release APK"
Push-Location "$Root\Zetla"
try {
    & ".\gradlew.bat" assembleRelease --no-daemon
    if ($LASTEXITCODE -ne 0) { Fail "Gradle build failed" }
} finally { Pop-Location }

$apkDir = "$Root\Zetla\app\build\outputs\apk\release"
Get-ChildItem "$apkDir\*.apk" | ForEach-Object {
    $size = [math]::Round($_.Length / 1MB, 1)
    Ok "APK: $($_.Name) ($size MB)"
}

Write-Host "`n╔══════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║          Build Complete!             ║" -ForegroundColor Green
Write-Host "╚══════════════════════════════════════╝" -ForegroundColor Green
