@echo off
:: install.bat — Install Visual Layer Inspector to your .nuke folder
::
:: Double-click this file or run from a command prompt.
:: Works for both Nuke 14 and Nuke 16.

setlocal enabledelayedexpansion

set NUKE_DIR=%USERPROFILE%\.nuke

echo.
echo  Visual Layer Inspector v18.3 — Installer
echo  ==========================================
echo.
echo  Install location: %NUKE_DIR%
echo.

if not exist "%NUKE_DIR%" (
    echo  Creating .nuke folder...
    mkdir "%NUKE_DIR%"
)

:: -------------------------------------------------------------------
::  Copy C++ plugins into version subfolders
:: -------------------------------------------------------------------
set VLI_DIR=%NUKE_DIR%\VisualLayerInspector

if not exist "%VLI_DIR%\nuke14" mkdir "%VLI_DIR%\nuke14" 2>nul
if not exist "%VLI_DIR%\nuke16" mkdir "%VLI_DIR%\nuke16" 2>nul

if exist "%~dp0nuke14\VisualLayerInspector.dll" (
    echo  Installing Nuke 14 plugin...
    copy /Y "%~dp0nuke14\VisualLayerInspector.dll" "%VLI_DIR%\nuke14\" >nul
    echo    OK: VisualLayerInspector.dll [Nuke 14]
)

if exist "%~dp0nuke16\VisualLayerInspector.dll" (
    echo  Installing Nuke 16 plugin...
    copy /Y "%~dp0nuke16\VisualLayerInspector.dll" "%VLI_DIR%\nuke16\" >nul
    echo    OK: VisualLayerInspector.dll [Nuke 16]
)

:: -------------------------------------------------------------------
::  Copy Python version
:: -------------------------------------------------------------------
if exist "%~dp0visual_layer_inspector.py" (
    echo  Installing Python version...
    copy /Y "%~dp0visual_layer_inspector.py" "%NUKE_DIR%\visual_layer_inspector.py" >nul
    if !errorlevel!==0 (
        echo    OK: visual_layer_inspector.py
    ) else (
        echo    FAILED
    )
)

:: -------------------------------------------------------------------
::  Create init.py if it doesn't exist
:: -------------------------------------------------------------------
if not exist "%NUKE_DIR%\init.py" (
    echo  Installing init.py...
    copy /Y "%~dp0init.py" "%NUKE_DIR%\init.py" >nul
    echo    OK: init.py installed
) else (
    echo  init.py already exists — skipping.
    echo    Make sure it contains: nuke.pluginAddPath(".")
)

:: -------------------------------------------------------------------
::  Add menu entry if not already present
:: -------------------------------------------------------------------
if not exist "%NUKE_DIR%\menu.py" (
    echo  Installing menu.py...
    copy /Y "%~dp0menu.py" "%NUKE_DIR%\menu.py" >nul
    echo    OK: menu.py installed
) else (
    findstr /C:"visual_layer_inspector" "%NUKE_DIR%\menu.py" >nul 2>&1
    if !errorlevel! neq 0 (
        echo  Adding menu entry to existing menu.py...
        echo.>> "%NUKE_DIR%\menu.py"
        type "%~dp0menu.py" >> "%NUKE_DIR%\menu.py"
        echo    OK: menu entry added
    ) else (
        echo  menu.py already has Visual Layer Inspector entry — skipping.
    )
)

:: -------------------------------------------------------------------
::  Nuke version note
:: -------------------------------------------------------------------
echo.
echo  Both Nuke 14 and 16 plugins installed — init.py
echo  auto-detects which version to load at startup.

echo.
echo  ==========================================
echo  Done! Restart Nuke to use the inspector.
echo  ==========================================
echo.
pause
endlocal
