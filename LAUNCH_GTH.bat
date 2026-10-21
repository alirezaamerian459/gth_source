@echo off
REM GameTransferHub Launch Script
REM This batch file launches the GTH application after build completes

echo.
echo ========================================
echo   GameTransferHub Launcher
echo ========================================
echo.

set PROJECT_DIR=C:\Users\DrGame\Desktop\gth new
set BUILD_DIR=%PROJECT_DIR%\build
set APP_EXE=%BUILD_DIR%\src\app\Release\GameTransferHub.exe

REM Check if executable exists
if exist "%APP_EXE%" (
    echo [OK] Found executable: %APP_EXE%
    echo.
    echo Launching GameTransferHub...
    echo.
    start "" "%APP_EXE%"
    echo.
    echo [SUCCESS] Application launched!
    echo.
    echo Dashboard should be visible with:
    echo   - Storage metrics
    echo   - Network Connection section
    echo   - IP Address: 192.168.x.x
    echo   - Port: 5000
    echo   - Copy button for PS5
    echo.
    timeout /t 3
) else (
    echo [ERROR] Executable not found at: %APP_EXE%
    echo.
    echo Please ensure the build has completed first:
    echo   cd %BUILD_DIR%
    echo   cmake --build . --config Release
    echo.
    echo Then run this script again.
    pause
)
