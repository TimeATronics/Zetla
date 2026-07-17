# setup.ps1 - Zetla build pipeline
# Run from project root: .\scripts\setup.ps1
#
# Step 1: Build Python static binary in WSL using zetla_android_build
# Step 2: Copy Python + Vosk model to Android project, build release APK
#
# Prerequisites:
#  - WSL Ubuntu with NDK r28b at ~/ndk/android-ndk-r28b/
#  - Python build system at ~/zetla_android_build/
#  - See docs/PYTHON_ANDROID_BUILD.md for WSL setup

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

# Configuration 
$WSL_BUILD_DIR = "/home/aradhya/zetla_android_build"
$PYTHON_DIST = "$WSL_BUILD_DIR/dist/bin"
$VOSK_MODEL_URL = "https://alphacephei.com/vosk/models/vosk-model-en-us-0.22-lgraph.zip"
$VOSK_MODEL_DIR = "$Root\Zetla\app\src\main\assets\model-en-us"

# Helpers 
function Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Ok($msg)   { Write-Host "  [OK] $msg" -ForegroundColor Green }
function Fail($msg) { Write-Host "  [FAIL] $msg" -ForegroundColor Red; exit 1 }

# Step 1: Build Python in WSL 
function Build-Python {
    Step "Step 1: Building Python in WSL"

    # Check if already built
    $pyBin = wsl wslpath -w "$PYTHON_DIST/python" 2>$null
    $pyStdlib = wsl wslpath -w "$PYTHON_DIST/python314t.zip" 2>$null
    $pyCacert = wsl wslpath -w "$PYTHON_DIST/cacert.pem" 2>$null

    if ($pyBin -and (Test-Path $pyBin) -and $pyStdlib -and (Test-Path $pyStdlib)) {
        Ok "Python already built at $PYTHON_DIST"
        return
    }

    Write-Host "  Python not found at $PYTHON_DIST"
    Write-Host "  Build it in WSL:"
    Write-Host ""
    Write-Host "    wsl -d Ubuntu-26.04 bash -c 'cd $WSL_BUILD_DIR && ./build_all.sh --clean'"
    Write-Host ""
    Write-Host "  Or step by step:"
    Write-Host "    wsl -d Ubuntu-26.04 bash -c 'cd $WSL_BUILD_DIR && ./build_all.sh --step deps'"
    Write-Host "    wsl -d Ubuntu-26.04 bash -c 'cd $WSL_BUILD_DIR && ./build_all.sh --step cpython'"
    Write-Host "    wsl -d Ubuntu-26.04 bash -c 'cd $WSL_BUILD_DIR && ./build_all.sh --step package'"
    Write-Host ""
    Write-Host "  Then copy the dist folder manually from WSL:"
    Write-Host "    wsl -d Ubuntu-26.04 bash -c 'cp -r $WSL_BUILD_DIR/dist /mnt/c/Users/Aradhya/zetla_python_dist'"
    Write-Host "    (Update `$PYTHON_DIST` in this script to point to the copied path)"
    Write-Host ""
    Fail "Python build required. Run build_all.sh in WSL first."
}

# Step 2: Copy to Android project + build APK 
function Copy-And-Build {
    Step "Step 2: Copying artifacts to Android project"

    # Convert WSL paths to Windows
    $distWin = wsl wslpath -w "$PYTHON_DIST"

    # --- Python binary -> jniLibs ---
    $jniLibs = "$Root\Zetla\app\src\main\jniLibs\arm64-v8a"
    $pyDst = "$jniLibs\libpython.so"
    $pySrc = "$distWin\python"

    if (-not (Test-Path $pyDst) -or (Get-Item $pySrc).LastWriteTime -gt (Get-Item $pyDst).LastWriteTime) {
        New-Item -ItemType Directory -Force -Path $jniLibs | Out-Null
        Copy-Item -Force $pySrc $pyDst
        Ok "Copied libpython.so"
    } else {
        Ok "libpython.so up to date"
    }

    # --- Stdlib zip + cacert -> app/assets ---
    $assets = "$Root\Zetla\app\src\main\assets"

    foreach ($file in @("python314t.zip", "cacert.pem")) {
        $src = "$distWin\$file"
        $dst = "$assets\$file"
        if (-not (Test-Path $dst) -or (Get-Item $src).LastWriteTime -gt (Get-Item $dst).LastWriteTime) {
            Copy-Item -Force $src $dst
            Ok "Copied $file to app/assets"
        } else {
            Ok "$file up to date in app/assets"
        }
    }

    # --- Stdlib zip + cacert -> data/assets (also needed by data module) ---
    $dataAssets = "$Root\Zetla\data\src\main\assets"
    foreach ($file in @("python314t.zip", "cacert.pem")) {
        $src = "$distWin\$file"
        $dst = "$dataAssets\$file"
        if (-not (Test-Path $dst) -or (Get-Item $src).LastWriteTime -gt (Get-Item $dst).LastWriteTime) {
            Copy-Item -Force $src $dst
            Ok "Copied $file to data/assets"
        } else {
            Ok "$file up to date in data/assets"
        }
    }

    # --- Vosk model ---
    if (-not (Test-Path "$VOSK_MODEL_DIR\am\final.mdl")) {
        Step "Downloading Vosk model"
        $zipFile = "$env:TEMP\vosk-model.zip"
        try {
            Invoke-WebRequest -Uri $VOSK_MODEL_URL -OutFile $zipFile
        } catch {
            Fail "Vosk download failed. Download manually from: $VOSK_MODEL_URL"
        }
        $extractDir = "$env:TEMP\vosk-extract"
        Expand-Archive -Path $zipFile -DestinationPath $extractDir -Force
        $extracted = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1
        if ($extracted) {
            Copy-Item -Recurse -Force "$($extracted.FullName)\*" $VOSK_MODEL_DIR
            Ok "Vosk model installed"
        } else {
            Fail "Could not find extracted Vosk model"
        }
        Remove-Item -Recurse -Force $zipFile, $extractDir -ErrorAction SilentlyContinue
    } else {
        Ok "Vosk model already present"
    }

    # --- Build APK ---
    Step "Building release APK"
    Push-Location "$Root\Zetla"
    try {
        & ".\gradlew.bat" assembleRelease --no-daemon
        if ($LASTEXITCODE -ne 0) { Fail "Gradle build failed" }
    } finally {
        Pop-Location
    }

    $apk = "$Root\Zetla\app\build\outputs\apk\release\app-release.apk"
    if (Test-Path $apk) {
        $size = [math]::Round((Get-Item $apk).Length / 1MB, 1)
        Ok "Release APK: $apk ($size MB)"
    } else {
        Fail "APK not found"
    }
}

# Main 
Write-Host "╔══════════════════════════════════════╗" -ForegroundColor Yellow
Write-Host "║       Zetla Build Pipeline           ║" -ForegroundColor Yellow
Write-Host "╚══════════════════════════════════════╝" -ForegroundColor Yellow

Build-Python
Copy-And-Build

Write-Host "`n╔══════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║          Build Complete!             ║" -ForegroundColor Green
Write-Host "╚══════════════════════════════════════╝" -ForegroundColor Green
