@echo off
setlocal enabledelayedexpansion

echo ========================================
echo  Build all plugins (MSVC)
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
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    )

    if not defined VS_PATH (
        echo [ERROR] Visual Studio not found.
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
echo [1/2] Configuring with CMake...
echo.

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

cmake .. -A x64
if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

echo.
echo [2/2] Building Release...
echo.

cmake --build . --config Release
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo.
echo ========================================
echo  Build successful!
echo ========================================
echo.
echo DLLs: %BUILD_DIR%\plugins\*\Release\*.dll
echo.
echo Install: install-all.bat
echo.
pause
