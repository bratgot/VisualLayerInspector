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
::  Create init.py if it doesn't exist
:: -------------------------------------------------------------------
if not exist "%NUKE_DIR%\init.py" (
    echo  Installing init.py...
    copy /Y "%~dp0init.py" "%NUKE_DIR%\init.py" >nul
    echo    OK: init.py installed
) else (
    echo  init.py already exists — skipping.
    echo    Make sure it loads the VisualLayerInspector plugin path.
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
