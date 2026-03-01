@echo off
:: install.bat — Copy built plugins to version-specific directories
::
:: Run from the repo root after building both versions:
::   install.bat

set NUKE_HOME=%USERPROFILE%\.nuke
set VLI_DIR=%NUKE_HOME%\VisualLayerInspector

:: Create directories
if not exist "%VLI_DIR%\nuke14" mkdir "%VLI_DIR%\nuke14"
if not exist "%VLI_DIR%\nuke16" mkdir "%VLI_DIR%\nuke16"

:: Copy plugins — adjust source paths if your build dirs differ
if exist "build-nuke14\Release\VisualLayerInspector.dll" (
    copy /Y "build-nuke14\Release\VisualLayerInspector.dll" "%VLI_DIR%\nuke14\"
    echo Installed Nuke 14 build
) else (
    echo WARNING: build-nuke14\Release\VisualLayerInspector.dll not found — skipping
)

if exist "build-nuke16\Release\VisualLayerInspector.dll" (
    copy /Y "build-nuke16\Release\VisualLayerInspector.dll" "%VLI_DIR%\nuke16\"
    echo Installed Nuke 16 build
) else (
    echo WARNING: build-nuke16\Release\VisualLayerInspector.dll not found — skipping
)

echo.
echo Done. Directory layout:
dir /s /b "%VLI_DIR%\*.dll" 2>nul
