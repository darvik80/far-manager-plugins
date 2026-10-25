@echo off
setlocal enabledelayedexpansion

echo ========================================
echo  Markdown Viewer Plugin Uninstaller
echo ========================================
echo.

REM Try to find FAR Manager installation
set "FAR_PATH="

if exist "%ProgramFiles%\Far Manager\Far.exe" (
    set "FAR_PATH=%ProgramFiles%\Far Manager"
    goto :found
)

if exist "%ProgramFiles(x86)%\Far Manager\Far.exe" (
    set "FAR_PATH=%ProgramFiles(x86)%\Far Manager"
    goto :found
)

if exist "%LOCALAPPDATA%\Far Manager\Far.exe" (
    set "FAR_PATH=%LOCALAPPDATA%\Far Manager"
    goto :found
)

if exist "%APPDATA%\Far Manager\Far.exe" (
    set "FAR_PATH=%APPDATA%\Far Manager"
    goto :found
)

where Far.exe >nul 2>&1
if %errorlevel% equ 0 (
    for /f "delims=" %%i in ('where Far.exe') do (
        set "FAR_PATH=%%~dpi"
        set "FAR_PATH=!FAR_PATH:~0,-1!"
        goto :found
    )
)

for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Far Manager" /ve 2^>nul') do (
    if exist "%%b\Far.exe" (
        set "FAR_PATH=%%b"
        goto :found
    )
)

for /f "tokens=2*" %%a in ('reg query "HKCU\SOFTWARE\Far Manager" /ve 2^>nul') do (
    if exist "%%b\Far.exe" (
        set "FAR_PATH=%%b"
        goto :found
    )
)

:notfound
echo [WARNING] Could not automatically detect FAR Manager installation.
echo.
set /p "FAR_PATH=Enter FAR Manager path: "

if not exist "%FAR_PATH%\Far.exe" (
    echo.
    echo [ERROR] Far.exe not found in: %FAR_PATH%
    pause
    exit /b 1
)

:found
echo [OK] Found FAR Manager: %FAR_PATH%
echo.

set "PLUGIN_DIR=%FAR_PATH%\Plugins\MarkdownViewer"

if not exist "%PLUGIN_DIR%" (
    echo [INFO] Plugin is not installed.
    echo.
    pause
    exit /b 0
)

echo Plugin directory: %PLUGIN_DIR%
echo.
echo This will remove:
echo   - MarkdownViewer.dll
echo   - test.md (if exists)
echo   - The MarkdownViewer folder
echo.
set /p "CONFIRM=Are you sure? (Y/N): "

if /i not "%CONFIRM%"=="Y" (
    echo.
    echo Uninstall cancelled.
    pause
    exit /b 0
)

echo.
echo Removing plugin...

rmdir /s /q "%PLUGIN_DIR%"
if errorlevel 1 (
    echo.
    echo [ERROR] Failed to remove directory.
    echo Try closing FAR Manager and running as Administrator.
    echo.
    pause
    exit /b 1
)

echo [OK] Plugin removed successfully!
echo.
echo Please restart FAR Manager or press Ctrl+Alt+F9
echo to reload plugins.
echo.
pause
