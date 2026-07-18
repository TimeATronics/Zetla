# setup.ps1 - Zetla build pipeline
# Run from project root: .\scripts\setup.ps1
#
# Downloads Vosk speech model and builds the release APK.
# All other dependencies (Python, native libs) are committed in the repo.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

$VCPKG_ZIP = "$Root\vcpkg_deps.zip"
$VCPKG_DIR = "$Root\build\vcpkg_installed"

$VOSK_MODEL_URL = "https://alphacephei.com/vosk/models/vosk-model-small-en-in-0.4.zip"
$VOSK_MODEL_DIR = "$Root\Zetla\app\src\main\assets\model-en-us"

function Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Ok($msg)   { Write-Host "  [OK] $msg" -ForegroundColor Green }
function Fail($msg) { Write-Host "  [FAIL] $msg" -ForegroundColor Red; exit 1 }

Step "Extracting vcpkg native deps"
if (-not (Test-Path "$VCPKG_DIR\x64-mingw-dynamic\include\curl")) {
    if (Test-Path $VCPKG_ZIP) {
        Expand-Archive -Path $VCPKG_ZIP -DestinationPath "$Root\build" -Force
        Ok "vcpkg deps extracted"
    } else {
        Fail "vcpkg_deps.zip not found at $VCPKG_ZIP - needed for native DLL build"
    }
} else {
    Ok "vcpkg deps already present"
}

Step "Downloading Vosk model"
if (-not (Test-Path "$VOSK_MODEL_DIR\am\final.mdl")) {
    $zipFile = "$env:TEMP\vosk-model.zip"
    Write-Host "  Downloading Vosk model from $VOSK_MODEL_URL ..."
    try { Invoke-WebRequest -Uri $VOSK_MODEL_URL -OutFile $zipFile }
    catch { Fail "Vosk download failed. Download manually from: $VOSK_MODEL_URL" }
    $extractDir = "$env:TEMP\vosk-extract"
    Expand-Archive -Path $zipFile -DestinationPath $extractDir -Force
    $extracted = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1
    if ($extracted) {
        Remove-Item -Recurse -Force $VOSK_MODEL_DIR -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force -Path $VOSK_MODEL_DIR | Out-Null
        Copy-Item -Recurse -Force "$($extracted.FullName)\*" $VOSK_MODEL_DIR
        Ok "Vosk model installed"
    } else { Fail "Could not find extracted Vosk model" }
    Remove-Item -Recurse -Force $zipFile, $extractDir -ErrorAction SilentlyContinue
} else {
    Ok "Vosk model already present"
}

Step "Building release APK"
Push-Location "$Root\Zetla"
try {
    & ".\gradlew.bat" assembleRelease --no-daemon
    if ($LASTEXITCODE -ne 0) { Fail "Gradle build failed" }
} finally { Pop-Location }

$apkDir = "$Root\Zetla\app\build\outputs\apk\release"
Get-ChildItem "$apkDir\*.apk" | ForEach-Object {
    $size = [math]::Round($_.Length / 1MB, 1)
    $name = $_.Name
    Ok "APK: $name ($size MB)"
}

Write-Host "`n=== Build Complete! ===" -ForegroundColor Green
