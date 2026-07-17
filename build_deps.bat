@echo off
setlocal EnableDelayedExpansion

REM build_deps.bat - Build libcurl for Android (arm64-v8a + armeabi-v7a)
REM Run from: C:\Users\Aradhya\MyFiles\Projects\zetla

set NDK=%LOCALAPPDATA%\Android\Sdk\ndk\28.2.13676358
set TOOLCHAIN=%NDK%\build\cmake\android.toolchain.cmake
set NINJA=C:\Users\Aradhya\MyFiles\Applications\mingw64\bin\ninja.exe
set DEPS=%~dp0deps_build
set OPENSSL_BASE=https://github.com/217heidai/openssl_for_android/releases/download/4.0.1

if not exist "%DEPS%\openssl" mkdir "%DEPS%\openssl"
if not exist "%DEPS%\curl" mkdir "%DEPS%\curl"

REM  Step 1: Download + extract OpenSSL 
call :build_openssl arm64-v8a
call :build_openssl armeabi-v7a
goto :after_openssl

:build_openssl
set ABI=%~1
if exist "%DEPS%\openssl\%ABI%\include" (
    echo OpenSSL %ABI% already extracted, skipping
    goto :eof
)
echo Downloading OpenSSL for %ABI%...
curl -L -o "%DEPS%\OpenSSL_%ABI%.tar.gz" "%OPENSSL_BASE%/OpenSSL_4.0.1_%ABI%.tar.gz"
echo Extracting OpenSSL for %ABI%...
mkdir "%DEPS%\openssl\%ABI%" 2>nul
tar -xzf "%DEPS%\OpenSSL_%ABI%.tar.gz" -C "%DEPS%\openssl\%ABI%" --strip-components=1
del "%DEPS%\OpenSSL_%ABI%.tar.gz"
echo OpenSSL %ABI% ready
goto :eof

:after_openssl

REM  Step 2: Clone libcurl 
if exist "%DEPS%\curl\CMakeLists.txt" (
    echo libcurl already cloned
) else (
    echo Cloning libcurl...
    git clone --depth 1 https://github.com/curl/curl.git "%DEPS%\curl"
)

REM  Step 3: Build libcurl per ABI 
call :build_curl arm64-v8a
call :build_curl armeabi-v7a
goto :after_curl

:build_curl
set ABI=%~1
set SAFEABI=%ABI:-=_%
set BUILDDIR=%DEPS%\curl_build_%SAFEABI%
set SSLDIR=%DEPS%\openssl\%ABI%

if exist "%BUILDDIR%\lib\libcurl.a" (
    echo libcurl %ABI% already built, skipping
    goto :eof
)

echo.
echo === Building libcurl for %ABI% ===
mkdir "%BUILDDIR%" 2>nul

"%NINJA%" --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: ninja not found at %NINJA%
    exit /b 1
)

cmake -G Ninja ^
    -B "%BUILDDIR%" ^
    -S "%DEPS%\curl" ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
    -DANDROID_ABI=%ABI% ^
    -DANDROID_PLATFORM=android-26 ^
    -DCURL_STATICLIB=ON ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DCURL_USE_OPENSSL=ON ^
    -DCURL_ZLIB=OFF ^
    -DCURL_USE_LIBPSL=OFF ^
    -DCURL_USE_LIBSSH2=OFF ^
    -DCURL_USE_GSSAPI=OFF ^
    -DCURL_DISABLE_LDAP=ON ^
    -DCURL_DISABLE_LDAPS=ON ^
    -DBUILD_CURL_EXE=OFF ^
    -DBUILD_TESTING=OFF ^
    -DOPENSSL_INCLUDE_DIR="%SSLDIR%/include" ^
    -DOPENSSL_SSL_LIBRARY="%SSLDIR%/lib/libssl.a" ^
    -DOPENSSL_CRYPTO_LIBRARY="%SSLDIR%/lib/libcrypto.a"
if %ERRORLEVEL% neq 0 (
    echo CMake configure failed for %ABI%
    exit /b 1
)

"%NINJA%" -C "%BUILDDIR%"
if %ERRORLEVEL% neq 0 (
    echo Ninja build failed for %ABI%
    exit /b 1
)

echo libcurl %ABI% built successfully
goto :eof

:after_curl

REM  Step 4: Copy into Android project 
set ANDROID_LIBS=%~dp0Zetla\data\src\main\libs

call :copy_results arm64-v8a
call :copy_results armeabi-v7a
goto :after_copy

:copy_results
set ABI=%~1
set SAFEABI=%ABI:-=_%
set BUILDDIR=%DEPS%\curl_build_%SAFEABI%
set SSLDIR=%DEPS%\openssl\%ABI%

mkdir "%ANDROID_LIBS%\%ABI%\lib" 2>nul
mkdir "%ANDROID_LIBS%\%ABI%\include\openssl" 2>nul

copy /Y "%BUILDDIR%\lib\libcurl.a" "%ANDROID_LIBS%\%ABI%\lib\libcurl.a"
echo Copied libcurl.a for %ABI%

copy /Y "%SSLDIR%\lib\libssl.a" "%ANDROID_LIBS%\%ABI%\lib\libssl.a"
copy /Y "%SSLDIR%\lib\libcrypto.a" "%ANDROID_LIBS%\%ABI%\lib\libcrypto.a"
echo Copied OpenSSL libs for %ABI%

copy /Y "%SSLDIR%\include\openssl\*.h" "%ANDROID_LIBS%\%ABI%\include\openssl\"
echo Copied OpenSSL headers for %ABI%
goto :eof

:after_copy

echo.
echo === All done! ===
echo Libraries ready at: %ANDROID_LIBS%

endlocal
