@echo off
setlocal enabledelayedexpansion

if "%~1"=="" (
    echo Usage: %~nx0 ^<plugin-name^>
    echo Example: %~nx0 html-viewer
    exit /b 1
)

set "PLUGIN_NAME=%~1"
set "SOURCE_DIR=%~dp0lua-plugins\%PLUGIN_NAME%"
set "DEST_DIR=%APPDATA%\Far Manager\Profile\Macros\scripts"

echo Installing Lua plugin: %PLUGIN_NAME%
echo.

if not exist "%SOURCE_DIR%\%PLUGIN_NAME%.lua" (
    echo ERROR: Lua script not found: %SOURCE_DIR%\%PLUGIN_NAME%.lua
    exit /b 1
)

if not exist "%DEST_DIR%" (
    echo Creating macros directory: %DEST_DIR%
    mkdir "%DEST_DIR%"
)

echo Copying %PLUGIN_NAME%.lua -^> %DEST_DIR%\
copy /y "%SOURCE_DIR%\%PLUGIN_NAME%.lua" "%DEST_DIR%\" >nul

if %errorlevel% equ 0 (
    echo.
    echo Plugin installed successfully.
    echo Restart FAR Manager to activate.
) else (
    echo.
    echo ERROR: Failed to copy script.
)

echo.
pause
