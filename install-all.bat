@echo off
setlocal enabledelayedexpansion

set "BUILD_DIR=D:\Projects\cpp\far-manager\build-msvc\plugins"
set "INSTALL_DIR=C:\Program Files\Far Manager\Plugins"

echo Installing all plugins...
echo.

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

for %%p in (EmailViewer MarkdownViewer PrettyViewer SshClient) do (
    set "DLL_FILE=%BUILD_DIR%\%%p\Release\%%p.dll"
    set "DEST_DIR=%INSTALL_DIR%\%%p"

    if not exist "!DLL_FILE!" (
        echo [SKIP] %%p - DLL not found
    ) else (
        if not exist "!DEST_DIR!" mkdir "!DEST_DIR!"
        copy /y "!DLL_FILE!" "!DEST_DIR!\" >nul
        if !errorlevel! equ 0 (
            echo [OK]   %%p
        ) else (
            echo [FAIL] %%p
        )
    )
)

echo.
echo Done.
pause
