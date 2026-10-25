@echo off
setlocal enabledelayedexpansion

echo ========================================
echo  Markdown Viewer Plugin Installer
echo ========================================
echo.

REM Check if DLL exists
set "DLL_SOURCE=%~dp0build\plugins\MarkdownViewer\libMarkdownViewer.dll"
if not exist "%DLL_SOURCE%" (
    echo [ERROR] DLL not found: %DLL_SOURCE%
    echo.
    echo Please build the plugin first:
    echo   cd build
    echo   cmake .. -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=g++
    echo   make
    echo.
    pause
    exit /b 1
)

echo [OK] Found DLL: %DLL_SOURCE%
echo.

REM Try to find FAR Manager installation
set "FAR_PATH="

REM Check common installation paths
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

REM Check if FAR is in PATH
where Far.exe >nul 2>&1
if %errorlevel% equ 0 (
    for /f "delims=" %%i in ('where Far.exe') do (
        set "FAR_PATH=%%~dpi"
        set "FAR_PATH=!FAR_PATH:~0,-1!"
        goto :found
    )
)

REM Check registry
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
echo Please enter the path to your FAR Manager installation manually:
echo (e.g., C:\Program Files\Far Manager)
echo.
set /p "FAR_PATH=FAR Manager path: "

if not exist "%FAR_PATH%\Far.exe" (
    echo.
    echo [ERROR] Far.exe not found in: %FAR_PATH%
    echo.
    pause
    exit /b 1
)

:found
echo [OK] Found FAR Manager: %FAR_PATH%
echo.

REM Create plugin directory
set "PLUGIN_DIR=%FAR_PATH%\Plugins\MarkdownViewer"
echo Creating plugin directory: %PLUGIN_DIR%

if not exist "%PLUGIN_DIR%" (
    mkdir "%PLUGIN_DIR%"
    if errorlevel 1 (
        echo.
        echo [ERROR] Failed to create directory. Try running as Administrator.
        echo.
        pause
        exit /b 1
    )
    echo [OK] Directory created.
) else (
    echo [OK] Directory already exists.
)
echo.

REM Copy DLL
set "DLL_DEST=%PLUGIN_DIR%\MarkdownViewer.dll"
echo Installing plugin: %DLL_DEST%

copy /y "%DLL_SOURCE%" "%DLL_DEST%" >nul
if errorlevel 1 (
    echo.
    echo [ERROR] Failed to copy DLL. Try running as Administrator.
    echo.
    pause
    exit /b 1
)

echo [OK] Plugin installed successfully!
echo.

REM Create test file
set "TEST_FILE=%PLUGIN_DIR%\test.md"
if not exist "%TEST_FILE%" (
    echo Creating test file...
    (
        echo # Markdown Viewer Test
        echo.
        echo This is a **test document** for the Markdown Viewer plugin.
        echo.
        echo ## Code Example
        echo.
        echo ```cpp
        echo #include ^<iostream^>
        echo.
        echo int main() {
        echo     std::cout ^<^< "Hello, FAR Manager!" ^<^< std::endl;
        echo     return 0;
        echo }
        echo ```
        echo.
        echo ## Features
        echo.
        echo - **Bold text**
        echo - *Italic text*
        echo - `Inline code`
        echo - [Links](https://example.com)
        echo.
        echo ---
        echo.
        echo *Press F2 to toggle raw mode, Esc to close*
    ) > "%TEST_FILE%"
    echo [OK] Test file created: %TEST_FILE%
)

echo.
echo ========================================
echo  Installation Complete!
echo ========================================
echo.
echo Next steps:
echo   1. Restart FAR Manager or press Ctrl+Alt+F9
echo   2. Select a .md file in FAR
echo   3. Press F11 and select "Markdown Viewer"
echo   4. Or type: mdview
echo.
echo To uninstall, simply delete the folder:
echo   %PLUGIN_DIR%
echo.
pause
