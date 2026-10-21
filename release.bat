@echo off
REM Game Transfer Hub - Release Build Script
REM Creates a complete release package with installer

setlocal enabledelayedexpansion

echo.
echo ====================================
echo Game Transfer Hub - Release Builder
echo ====================================
echo.
echo This script will:
echo  1. Build the application
echo  2. Create release package
echo  3. Generate installer (if NSIS installed)
echo.

cd /d "%~dp0"

REM Check if NSIS is installed
set NSIS_PATH=
for %%P in ("C:\Program Files\NSIS" "C:\Program Files (x86)\NSIS") do (
    if exist "%%P\makensis.exe" (
        set NSIS_PATH=%%P
    )
)

REM Step 1: Build
echo [1/3] Building Game Transfer Hub...
echo.

cmake --build build --config Release

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED!
    echo.
    echo Make sure you have:
    echo  - Visual Studio 2022 installed
    echo  - ATL/WTL components installed
    echo.
    pause
    exit /b 1
)

echo Build complete!
echo.

REM Check if executable exists
if not exist "build\Release\GameTransferHubApp.exe" (
    echo ERROR: Executable not created!
    pause
    exit /b 1
)

REM Step 2: Create release directory
echo [2/3] Preparing release package...
echo.

if exist "release" (
    rmdir /s /q release
)
mkdir release

REM Copy executable and dependencies
copy /Y "build\Release\GameTransferHubApp.exe" "release\"
copy /Y "build\_deps\curl-build\lib\Release\libcurl.dll" "release\" 2>nul
copy /Y "build\_deps\libssh2-build\src\Release\libssh2.dll" "release\" 2>nul

REM Copy documentation
copy /Y "README_FIRST.md" "release\README.txt"
copy /Y "GAME_RECOGNITION_GUIDE.md" "release\"
copy /Y "QUICK_REFERENCE.md" "release\"

echo Release package created in: release\
echo.

REM Step 3: Create installer
if not "!NSIS_PATH!"=="" (
    echo [3/3] Creating installer...
    echo.

    "!NSIS_PATH!\makensis.exe" /V4 "GameTransferHub.nsi"

    if %ERRORLEVEL% EQU 0 (
        echo.
        echo ====================================
        echo RELEASE COMPLETE!
        echo ====================================
        echo.
        echo Files created:
        echo  - release\                     (Portable version)
        echo  - GameTransferHub-Setup-Latest.exe  (Installer)
        echo.
        echo Installation:
        echo  1. Run: GameTransferHub-Setup-Latest.exe
        echo  2. Follow on-screen instructions
        echo  3. Or use portable version from release\ folder
        echo.
        echo Features:
        echo  ✅ .pkg file recognition
        echo  ✅ Auto-detect title, platform, region
        echo  ✅ Games and Updates tabs working
        echo  ✅ Flexible file organization
        echo.
    ) else (
        echo Installer creation failed.
        echo You can still use the portable version in release\ folder.
    )
) else (
    echo.
    echo NSIS not found - skipping installer creation
    echo.
    echo Note: You can install NSIS from:
    echo   https://nsis.sourceforge.io/Download
    echo.
    echo For now, you can use the portable version:
    echo   release\GameTransferHubApp.exe
    echo.
)

echo.
echo To distribute:
echo  - Portable: Zip the release\ folder
echo  - Installer: Send GameTransferHub-Setup-Latest.exe
echo.

pause
