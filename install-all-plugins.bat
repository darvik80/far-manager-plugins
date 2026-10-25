@echo off
echo Installing all plugins...

set "BUILD_DIR=%~dp0build-msvc\plugins"

echo Installing MarkdownViewer...
if not exist "C:\Program Files\Far Manager\Plugins\MarkdownViewer" (
    mkdir "C:\Program Files\Far Manager\Plugins\MarkdownViewer"
)
copy /y "%BUILD_DIR%\MarkdownViewer\Release\MarkdownViewer.dll" "C:\Program Files\Far Manager\Plugins\MarkdownViewer\" >nul
if errorlevel 1 (
    echo [ERROR] Failed to install MarkdownViewer
) else (
    echo [OK] MarkdownViewer installed
)

echo Installing TestPlugin...
if not exist "C:\Program Files\Far Manager\Plugins\TestPlugin" (
    mkdir "C:\Program Files\Far Manager\Plugins\TestPlugin"
)
copy /y "%BUILD_DIR%\TestPlugin\Release\TestPlugin.dll" "C:\Program Files\Far Manager\Plugins\TestPlugin\" >nul
if errorlevel 1 (
    echo [ERROR] Failed to install TestPlugin
) else (
    echo [OK] TestPlugin installed
)

echo.
echo All plugins installed!
echo Restart FAR Manager and press F11
pause
