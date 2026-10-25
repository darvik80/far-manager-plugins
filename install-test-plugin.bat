@echo off
echo Installing TestPlugin...

set "SOURCE=%~dp0build-msvc\plugins\TestPlugin\Release\TestPlugin.dll"
set "DEST=C:\Program Files\Far Manager\Plugins\TestPlugin\TestPlugin.dll"

if not exist "%SOURCE%" (
    echo [ERROR] TestPlugin.dll not found
    pause
    exit /b 1
)

if not exist "C:\Program Files\Far Manager\Plugins\TestPlugin" (
    mkdir "C:\Program Files\Far Manager\Plugins\TestPlugin"
)

copy /y "%SOURCE%" "%DEST%"
if errorlevel 1 (
    echo [ERROR] Copy failed
    pause
    exit /b 1
)

echo [OK] TestPlugin installed
echo Restart FAR Manager and press F11
pause
