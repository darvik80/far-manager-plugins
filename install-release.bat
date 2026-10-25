@echo off
setlocal
rem Self-elevation: request admin rights if not elevated
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator rights...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
echo Installing Release MarkdownViewer...
set "BUILD_DIR=%~dp0build-msvc\plugins\MarkdownViewer\Release"
set "PLUGIN_DIR=C:\Program Files\Far Manager\Plugins\MarkdownViewer"
if not exist "%BUILD_DIR%\MarkdownViewer.dll" (
    echo [ERROR] Release build not found: %BUILD_DIR%\MarkdownViewer.dll
    pause
    exit /b 1
)
taskkill /IM Far.exe /F >nul 2>&1
if not exist "%PLUGIN_DIR%" mkdir "%PLUGIN_DIR%"
copy /y "%BUILD_DIR%\MarkdownViewer.dll" "%PLUGIN_DIR%\" >nul
if errorlevel 1 (
    echo [ERROR] DLL copy failed - close FAR Manager and retry
    pause
    exit /b 1
)
echo [OK] Installed:
dir /b "%PLUGIN_DIR%\MarkdownViewer.*"
echo.
echo Start FAR Manager and open a .md file
pause
