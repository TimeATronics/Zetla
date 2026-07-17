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

    # Check if at least one ABI has been built
    $found = $false
    foreach ($abi in @("arm64-v8a", "armeabi-v7a")) {
        $bin = wsl wslpath -w "$WSL_BUILD_DIR/dist/$abi/bin/python" 2>$null
        $stdlib = wsl wslpath -w "$WSL_BUILD_DIR/dist/$abi/bin/python314t.zip" 2>$null
        if ($bin -and (Test-Path $bin) -and $stdlib -and (Test-Path $stdlib)) {
            $found = $true
            break
        }
    }
    if ($found) { Ok "Python built in WSL"; return }

    Write-Host "  Python not found. Build both ABIs in WSL:"
    Write-Host ""
    Write-Host "    wsl -d Ubuntu-26.04 bash -c 'cd $WSL_BUILD_DIR && ./build_all.sh --abi arm64-v8a --clean'"
    Write-Host "    wsl -d Ubuntu-26.04 bash -c 'cd $WSL_BUILD_DIR && ./build_all.sh --abi armeabi-v7a --clean'"
    Write-Host ""
    Fail "Python build required. Run build_all.sh in WSL first."
}

# Step 2: Copy to Android project + build APK 
function Copy-And-Build {
    Step "Step 2: Copying artifacts to Android project"

    $ABIS = @("arm64-v8a", "armeabi-v7a")

    # --- Python binary -> jniLibs (per ABI) ---
    foreach ($abi in $ABIS) {
        $abiDist = wsl wslpath -w "$WSL_BUILD_DIR/dist/$abi/bin" 2>$null
        if (-not $abiDist -or -not (Test-Path "$abiDist\python")) {
            Write-Host "  [WARN] Python binary not found for $abi at $abiDist" -ForegroundColor Yellow
            Write-Host "  Build it first: wsl bash -c 'cd $WSL_BUILD_DIR && ./build_all.sh --abi $abi --clean'" -ForegroundColor Yellow
            continue
        }

        $jniDir = "$Root\Zetla\app\src\main\jniLibs\$abi"
        $pyDst = "$jniDir\libpython.so"
        $pySrc = "$abiDist\python"

        if (-not (Test-Path $pyDst) -or (Get-Item $pySrc).LastWriteTime -gt (Get-Item $pyDst).LastWriteTime) {
            New-Item -ItemType Directory -Force -Path $jniDir | Out-Null
            Copy-Item -Force $pySrc $pyDst
            Ok "Copied libpython.so for $abi"
        } else {
            Ok "libpython.so up to date for $abi"
        }
    }

    # --- Stdlib zip + cacert -> app/assets (same for all ABIs) ---
    $assets = "$Root\Zetla\app\src\main\assets"
    $distSrc = wsl wslpath -w "$WSL_BUILD_DIR/dist/arm64-v8a/bin" 2>$null
    # Fallback: try any ABI's dist
    if (-not $distSrc -or -not (Test-Path "$distSrc\python3.14t.zip")) {
        foreach ($abi in $ABIS) {
            $try = wsl wslpath -w "$WSL_BUILD_DIR/dist/$abi/bin" 2>$null
            if ($try -and (Test-Path "$try\python3.14t.zip")) { $distSrc = $try; break }
        }
    }

    if ($distSrc -and (Test-Path "$distSrc\python3.14t.zip")) {
        foreach ($file in @("python314t.zip", "cacert.pem")) {
            $src = "$distSrc\$file"
            $dst = "$assets\$file"
            if (-not (Test-Path $dst) -or (Get-Item $src).LastWriteTime -gt (Get-Item $dst).LastWriteTime) {
                Copy-Item -Force $src $dst
                Ok "Copied $file to app/assets"
            } else {
                Ok "$file up to date in app/assets"
            }
        }


    } else {
        Write-Host "  [WARN] Stdlib zip not found. Build Python first." -ForegroundColor Yellow
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

    # --- Build split APKs ---
    Step "Building release APKs (one per ABI)"
    Push-Location "$Root\Zetla"
    try {
        & ".\gradlew.bat" assembleRelease --no-daemon
        if ($LASTEXITCODE -ne 0) { Fail "Gradle build failed" }
    } finally {
        Pop-Location
    }

    $apkDir = "$Root\Zetla\app\build\outputs\apk\release"
    $apks = Get-ChildItem "$apkDir\*.apk" | Where-Object { $_.Name -ne "app-release-*.apk" -or $true }
    if ($apks.Count -gt 0) {
        foreach ($apk in $apks) {
            $size = [math]::Round($apk.Length / 1MB, 1)
            Ok "APK: $($apk.Name) ($size MB)"
        }
    } else {
        Fail "No APKs found in $apkDir"
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
