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

for %%p in (EmailViewer HtmlViewer MarkdownViewer NoteKeeper PrettyViewer SecretVault SshClient) do (
    set "PLUGIN_DIR=%BUILD_DIR%\%%p\Release"
    set "DLL_FILE=!PLUGIN_DIR!\%%p.dll"
    set "DEST_DIR=%INSTALL_DIR%\%%p"

    if not exist "!DLL_FILE!" (
        echo [SKIP] %%p - DLL not found
    ) else (
        if not exist "!DEST_DIR!" mkdir "!DEST_DIR!"
        copy /y "!DLL_FILE!" "!DEST_DIR!\" >nul
        if exist "!PLUGIN_DIR!\*.lua" copy /y "!PLUGIN_DIR!\*.lua" "!DEST_DIR!\" >nul
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
