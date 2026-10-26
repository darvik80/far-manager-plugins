@echo off
setlocal enabledelayedexpansion

if "%~1"=="" (
    echo Usage: %~nx0 ^<PluginName^>
    echo Example: %~nx0 EmailViewer
    exit /b 1
)

set "PLUGIN_NAME=%~1"
set "BUILD_DIR=D:\Projects\cpp\far-manager\build-msvc\plugins\%PLUGIN_NAME%\Release"
set "INSTALL_DIR=C:\Program Files\Far Manager\Plugins\%PLUGIN_NAME%"
set "DLL_FILE=%BUILD_DIR%\%PLUGIN_NAME%.dll"

echo Installing plugin: %PLUGIN_NAME%
echo.

if not exist "%DLL_FILE%" (
    echo ERROR: DLL not found: %DLL_FILE%
    echo Build the plugin first.
    exit /b 1
)

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator privileges...
    powershell -Command "Start-Process '%~f0' -ArgumentList '%PLUGIN_NAME%' -Verb RunAs"
    exit /b
)

if not exist "%INSTALL_DIR%" (
    echo Creating directory: %INSTALL_DIR%
    mkdir "%INSTALL_DIR%"
)

echo Copying %DLL_FILE% -^> %INSTALL_DIR%\
copy /y "%DLL_FILE%" "%INSTALL_DIR%\" >nul
set "COPY_OK=%errorlevel%"

if exist "%BUILD_DIR%\*.lua" (
    echo Copying Lua scripts -^> %INSTALL_DIR%\
    copy /y "%BUILD_DIR%\*.lua" "%INSTALL_DIR%\" >nul
    if errorlevel 1 set "COPY_OK=1"
)

if "%COPY_OK%"=="0" (
    echo.
    echo Plugin installed successfully.
) else (
    echo.
    echo ERROR: Failed to copy plugin files.
)

echo.
pause
