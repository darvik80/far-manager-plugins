@echo off
setlocal enabledelayedexpansion

echo ========================================
echo  Build ^& Install (MSVC)
echo ========================================
echo.

REM Check for CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] CMake not found.
    pause
    exit /b 1
)

REM Check for MSVC
where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNING] cl.exe not in PATH. Trying to find Visual Studio...

    set "VS_PATH="
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )

    if not defined VS_PATH (
        echo [ERROR] Visual Studio 2022 not found.
        pause
        exit /b 1
    )

    echo [OK] Found VS: !VS_PATH!
    call "!VS_PATH!" >nul 2>&1
)

set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build-msvc"

REM Build
echo.
echo [1/3] Building with MSVC...
echo.

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

cmake .. -G "Visual Studio 17 2022" -A x64 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

cmake --build . --config Release
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

set "DLL_SOURCE=%BUILD_DIR%\plugins\MarkdownViewer\Release\MarkdownViewer.dll"
set "DLL_DEST=%PLUGIN_DIR%\MarkdownViewer.dll"

copy /y "%DLL_SOURCE%" "%DLL_DEST%" >nul
if errorlevel 1 (
    echo.
    echo [ERROR] Failed to copy.
    echo Try running this script as Administrator:
    echo   Right-click on build-and-install-msvc.bat
    echo   Select "Run as administrator"
    echo.
    pause
    exit /b 1
)

echo [OK] Installed: %DLL_DEST%
echo.
echo ========================================
echo  Done!
echo ========================================
echo.
echo Restart FAR Manager or press Ctrl+Alt+F9
echo Select a .md file, press F11, choose Markdown Viewer
echo.
pause
