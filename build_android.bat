@echo off
rem ============================================================
rem  编译 Android 端 (arm64-v8a), 产物 build\libfenghua_core.so
rem  需要: NDK r27 (其它 r25+ 也行), CMake 3.22
rem  按需改下面两个路径
rem ============================================================
setlocal
set "ANDROID_HOME=D:\Android\Sdk"
set "NDK=%ANDROID_HOME%\ndk\27.0.12077973"
set "CMAKE_BIN=%ANDROID_HOME%\cmake\3.22.1\bin"
set "TOOLCHAIN=%NDK%\build\cmake\android.toolchain.cmake"

if not exist "%TOOLCHAIN%" (
  echo [ERROR] NDK toolchain not found: %TOOLCHAIN%
  echo         edit this file and point NDK to your own NDK path
  exit /b 1
)

set "PATH=%CMAKE_BIN%;%PATH%"
cd /d "%~dp0"

cmake -B build ^
  -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
  -DANDROID_ABI=arm64-v8a ^
  -DANDROID_PLATFORM=android-24 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -G Ninja
if %ERRORLEVEL% NEQ 0 exit /b 1

cmake --build build
if %ERRORLEVEL% NEQ 0 exit /b 1

for %%f in (build\libfenghua_core.so) do echo [OK] %%~ff  (%%~zf bytes)
echo.
echo 推送到设备:
echo   adb push build\libfenghua_core.so /data/local/tmp/fenghua_core
echo   adb shell su -c "chmod 755 /data/local/tmp/fenghua_core"
echo   adb shell su -c "/data/local/tmp/fenghua_core --driver stub --config /data/adb/fenghua/mappings.json"
endlocal
