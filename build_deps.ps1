$ErrorActionPreference = "Stop"

$NDK = "$env:LOCALAPPDATA\Android\Sdk\ndk\28.2.13676358"
$TOOLCHAIN = "$NDK/build/cmake/android.toolchain.cmake"
$DEPS = "$PSScriptRoot\deps_build"
$OPENSSL_BASE = "https://github.com/217heidai/openssl_for_android/releases/download/4.0.1"
$NINJA = "C:\Users\Aradhya\MyFiles\Applications\mingw64\bin\ninja.exe"

$ABIS = @("arm64-v8a", "armeabi-v7a")
$ANDROID_PLATFORM = "android-26"

#  Step 1: Setup directories 
New-Item -ItemType Directory -Force -Path "$DEPS\openssl" | Out-Null
New-Item -ItemType Directory -Force -Path "$DEPS\curl" | Out-Null

#  Step 2: Download + extract OpenSSL 
foreach ($abi in $ABIS) {
    $tarName = "OpenSSL_4.0.1_$abi.tar.gz"
    $tarPath = "$DEPS\$tarName"
    $extractDir = "$DEPS\openssl\$abi"

    if (Test-Path "$extractDir\include") {
        Write-Host "OpenSSL $abi already extracted, skipping"
        continue
    }

    Write-Host "Downloading OpenSSL for $abi..."
    Invoke-WebRequest -Uri "$OPENSSL_BASE/$tarName" -OutFile $tarPath

    Write-Host "Extracting OpenSSL for $abi..."
    New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
    tar -xzf $tarPath -C $extractDir --strip-components=1

    Remove-Item $tarPath
    Write-Host "OpenSSL $abi ready"
}

#  Step 3: Clone libcurl 
$curlSrc = "$DEPS\curl"
if (-not (Test-Path "$curlSrc\CMakeLists.txt")) {
    Write-Host "Cloning libcurl..."
    git clone --depth 1 https://github.com/curl/curl.git $curlSrc
} else {
    Write-Host "libcurl already cloned"
}

#  Step 4: Build libcurl per ABI 
foreach ($abi in $ABIS) {
    $safeAbi = $abi -replace '-','_'
    $buildDir = "$DEPS\curl_build_$safeAbi"
    $installDir = "$buildDir\install"
    $sslDir = "$DEPS\openssl\$abi"

    if (Test-Path "$buildDir\lib\libcurl.a") {
        Write-Host "libcurl $abi already built, skipping"
        continue
    }

    Write-Host "`n=== Building libcurl for $abi ==="
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    # CMake configure — build args as array to avoid backtick expansion issues
    $cmakeArgs = @(
        "-G", "Ninja"
        "-B", $buildDir
        "-S", $curlSrc
        "-DCMAKE_MAKE_PROGRAM=$NINJA"
        "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN"
        "-DANDROID_ABI=$abi"
        "-DANDROID_PLATFORM=$ANDROID_PLATFORM"
        "-DCMAKE_INSTALL_PREFIX=$installDir"
        "-DCURL_STATICLIB=ON"
        "-DBUILD_SHARED_LIBS=OFF"
        "-DCURL_USE_OPENSSL=ON"
        "-DCURL_ZLIB=OFF"
        "-DBUILD_CURL_EXE=OFF"
        "-DBUILD_TESTING=OFF"
        "-DOPENSSL_INCLUDE_DIR=$sslDir/include"
        "-DOPENSSL_SSL_LIBRARY=$sslDir/lib/libssl.a"
        "-DOPENSSL_CRYPTO_LIBRARY=$sslDir/lib/libcrypto.a"
    )

    Write-Host "cmake $($cmakeArgs -join ' ')"
    & cmake @cmakeArgs

    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $abi" }

    # Build
    & $NINJA -C $buildDir
    if ($LASTEXITCODE -ne 0) { throw "Ninja build failed for $abi" }

    Write-Host "libcurl $abi built successfully"
}

#  Step 5: Copy into Android project 
$ANDROID_LIBS = "$PSScriptRoot\Zetla\app\src\main\libs"

foreach ($abi in $ABIS) {
    $safeAbi = $abi -replace '-','_'
    $buildDir = "$DEPS\curl_build_$safeAbi"
    $sslDir = "$DEPS\openssl\$abi"
    $destLib = "$ANDROID_LIBS\$abi\lib"
    $destInc = "$ANDROID_LIBS\$abi\include\openssl"

    New-Item -ItemType Directory -Force -Path $destLib | Out-Null
    New-Item -ItemType Directory -Force -Path $destInc | Out-Null

    # Copy libcurl.a
    Copy-Item "$buildDir\lib\libcurl.a" "$destLib\libcurl.a" -Force
    Write-Host "Copied libcurl.a for $abi"

    # Copy OpenSSL headers
    Copy-Item "$sslDir\include\openssl\*.h" $destInc -Force
    Write-Host "Copied OpenSSL headers for $abi"
}

Write-Host "`n=== All done! ==="
Write-Host "Libraries ready at: $ANDROID_LIBS"
