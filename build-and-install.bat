@echo off
setlocal enabledelayedexpansion

echo ========================================
echo  Build ^& Install Markdown Viewer
echo ========================================
echo.

REM Check for CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] CMake not found. Please install CMake first.
    pause
    exit /b 1
)

REM Check for g++
where g++ >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] g++ not found. Please install MinGW-w64 first.
    pause
    exit /b 1
)

set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build"

REM Build
echo [1/3] Building plugin...
echo.

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cd /d "%BUILD_DIR%"

cmake .. -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=g++ >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

make 2>&1
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo.
echo [OK] Build successful.
echo.

REM Find FAR Manager
echo [2/3] Locating FAR Manager...

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

echo [WARNING] Could not detect FAR Manager automatically.
set /p "FAR_PATH=Enter FAR Manager path: "

if not exist "%FAR_PATH%\Far.exe" (
    echo [ERROR] Far.exe not found in: %FAR_PATH%
    pause
    exit /b 1
)

:found
echo [OK] FAR Manager: %FAR_PATH%
echo.

REM Install
echo [3/3] Installing plugin...

set "PLUGIN_DIR=%FAR_PATH%\Plugins\MarkdownViewer"
if not exist "%PLUGIN_DIR%" mkdir "%PLUGIN_DIR%"

copy /y "%BUILD_DIR%\plugins\MarkdownViewer\libMarkdownViewer.dll" "%PLUGIN_DIR%\MarkdownViewer.dll" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy. Try running as Administrator.
    pause
    exit /b 1
)

echo [OK] Installed to: %PLUGIN_DIR%\MarkdownViewer.dll
echo.
echo ========================================
echo  Done!
echo ========================================
echo.
echo Restart FAR Manager or press Ctrl+Alt+F9
echo Select a .md file, press F11, choose Markdown Viewer
echo.
pause
