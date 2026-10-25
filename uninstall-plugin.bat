@echo off
setlocal enabledelayedexpansion

if "%~1"=="" (
    echo Usage: %~nx0 ^<PluginName^>
    echo Example: %~nx0 EmailViewer
    exit /b 1
)

set "PLUGIN_NAME=%~1"
set "INSTALL_DIR=C:\Program Files\Far Manager\Plugins\%PLUGIN_NAME%"

echo Removing plugin: %PLUGIN_NAME%
echo.

if not exist "%INSTALL_DIR%" (
    echo Plugin directory not found: %INSTALL_DIR%
    echo Nothing to remove.
    goto :done
)

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator privileges...
    powershell -Command "Start-Process '%~f0' -ArgumentList '%PLUGIN_NAME%' -Verb RunAs"
    exit /b
)

echo Deleting %INSTALL_DIR% ...
rmdir /s /q "%INSTALL_DIR%"

if exist "%INSTALL_DIR%" (
    echo.
    echo ERROR: Failed to remove plugin directory.
    echo Make sure FAR Manager is closed.
) else (
    echo.
    echo Plugin removed successfully.
)

:done
echo.
pause
