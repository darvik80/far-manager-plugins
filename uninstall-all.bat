@echo off
setlocal enabledelayedexpansion

set "INSTALL_DIR=C:\Program Files\Far Manager\Plugins"

echo Uninstalling all plugins...
echo.

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

for %%p in (EmailViewer HtmlViewer MarkdownViewer PrettyViewer SecretVault SshClient) do (
    set "DEST_DIR=%INSTALL_DIR%\%%p"

    if exist "!DEST_DIR!" (
        rmdir /s /q "!DEST_DIR!"
        if exist "!DEST_DIR!" (
            echo [FAIL] %%p
        ) else (
            echo [OK]   %%p
        )
    ) else (
        echo [SKIP] %%p - not installed
    )
)

echo.
echo Done.
pause
