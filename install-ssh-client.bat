@echo off
setlocal
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator rights...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
echo Installing Release SshClient...
set "BUILD_DIR=%~dp0build-msvc\plugins\SshClient\Release"
set "PLUGIN_DIR=C:\Program Files\Far Manager\Plugins\SshClient"
if not exist "%BUILD_DIR%\SshClient.dll" (
    echo [ERROR] Release build not found: %BUILD_DIR%\SshClient.dll
    pause
    exit /b 1
)
taskkill /IM Far.exe /F >nul 2>&1
if not exist "%PLUGIN_DIR%" mkdir "%PLUGIN_DIR%"
copy /y "%BUILD_DIR%\SshClient.dll" "%PLUGIN_DIR%\" >nul
if errorlevel 1 (
    echo [ERROR] DLL copy failed - close FAR Manager and retry
    pause
    exit /b 1
)
set "CRYPTO=%USERPROFILE%\scoop\apps\openssl\current\libcrypto-4-x64.dll"
if exist "%CRYPTO%" (
    copy /y "%CRYPTO%" "%PLUGIN_DIR%\" >nul
) else (
    echo [WARN] libcrypto-4-x64.dll was not copied. SSH will not load until that DLL sits next to SshClient.dll.
)
echo [OK] Installed:
dir /b "%PLUGIN_DIR%"
echo.
echo Start FAR Manager, press F11 or Alt+F1, and choose SSH
pause
