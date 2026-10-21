@echo off
REM Game Transfer Hub - Build Script with PKG Recognition
REM This script rebuilds the app with the new .pkg file recognition feature

setlocal enabledelayedexpansion

echo.
echo ========================================
echo Game Transfer Hub - Build with PKG Support
echo ========================================
echo.

REM Check if cmake exists
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake not found in PATH
    echo Please install CMake or add it to your PATH
    pause
    exit /b 1
)

REM Get directory
cd /d "%~dp0"

echo Current directory: %CD%
echo.

REM Clean old build (optional)
if "%1"=="--clean" (
    echo Cleaning old build...
    if exist build (
        rmdir /s /q build
        echo Build directory cleaned.
    )
)

REM Configure
echo Step 1/3: Configuring with CMake...
if exist build (
    echo Using existing build directory...
) else (
    echo Creating new build directory...
)

cmake -B build -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: CMake configuration failed!
    echo Check that cmake is installed and in your PATH
    pause
    exit /b 1
)

echo Configuration complete!
echo.

REM Build
echo Step 2/3: Building Game Transfer Hub...
echo This may take 5-15 minutes depending on your system...
echo.

cmake --build build --config Release

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================
    echo BUILD FAILED
    echo ========================================
    echo.
    echo The build failed. Common reasons:
    echo.
    echo 1. Missing WTL (Windows Template Library)
    echo    → Install via Visual Studio Installer
    echo    → Search for "ATL" in Individual Components
    echo    → See: BUILD_AND_DEPLOY.md for details
    echo.
    echo 2. Missing Windows SDK
    echo    → Install via Visual Studio Installer
    echo    → Search for "Windows SDK"
    echo.
    echo 3. Visual Studio not installed
    echo    → Install Visual Studio 2022 Community Edition (free)
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================
echo BUILD SUCCESS!
echo ========================================
echo.

REM Find executable
set EXE_PATH=build\Release\GameTransferHubApp.exe

if exist "%EXE_PATH%" (
    echo Executable created: %EXE_PATH%
    echo Size:
    for /F "tokens=*" %%A in ('dir /b "%EXE_PATH%"') do set SIZE=%%~zA
    REM Convert to MB
    set /a SIZE_MB=SIZE/1024/1024
    echo !SIZE_MB! MB
    echo.
    echo Features:
    echo  ✅ .pkg file recognition
    echo  ✅ Auto-detect title, platform, region, version
    echo  ✅ Works with traditional folder structure too
    echo  ✅ Games and Updates tabs fully functional
    echo.
    echo Next steps:
    echo  1. Replace your old executable with the new one
    echo  2. Run the app
    echo  3. Add storage path in Settings
    echo  4. Click "Scan All Storages" in Games tab
    echo  5. Enjoy! 🎮
) else (
    echo ERROR: Executable not found at %EXE_PATH%
    echo Build may have failed during linking.
)

echo.
echo To deploy the executable:
echo  copy /Y "%EXE_PATH%" "C:\Program Files\GameTransferHub\GameTransferHubApp.exe"
echo.

pause
