@echo off
setlocal

set "PLUGIN_DIR=C:\Program Files\Far Manager\Plugins\EmailViewer"

echo Removing EmailViewer plugin...
echo.

if not exist "%PLUGIN_DIR%" (
    echo Plugin directory not found: %PLUGIN_DIR%
    echo Nothing to remove.
    goto :done
)

:: Check for admin rights
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

echo Deleting %PLUGIN_DIR% ...
rmdir /s /q "%PLUGIN_DIR%"

if exist "%PLUGIN_DIR%" (
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
