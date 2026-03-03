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
::  Detect Nuke version and copy the right DLL
:: -------------------------------------------------------------------
set INSTALLED_DLL=0

if exist "%~dp0nuke16\VisualLayerInspector.dll" (
    echo  Installing Nuke 16 plugin...
    copy /Y "%~dp0nuke16\VisualLayerInspector.dll" "%NUKE_DIR%\VisualLayerInspector.dll" >nul
    if !errorlevel!==0 (
        echo    OK: VisualLayerInspector.dll [Nuke 16]
        set INSTALLED_DLL=1
    ) else (
        echo    FAILED — is Nuke running? Close it and try again.
    )
)

if !INSTALLED_DLL!==0 (
    if exist "%~dp0nuke14\VisualLayerInspector.dll" (
        echo  Installing Nuke 14 plugin...
        copy /Y "%~dp0nuke14\VisualLayerInspector.dll" "%NUKE_DIR%\VisualLayerInspector.dll" >nul
        if !errorlevel!==0 (
            echo    OK: VisualLayerInspector.dll [Nuke 14]
            set INSTALLED_DLL=1
        ) else (
            echo    FAILED — is Nuke running? Close it and try again.
        )
    )
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
    echo  Creating init.py...
    echo nuke.pluginAddPath(".")> "%NUKE_DIR%\init.py"
    echo    OK: init.py created
) else (
    echo  init.py already exists — skipping.
    echo    Make sure it contains: nuke.pluginAddPath(".")
)

:: -------------------------------------------------------------------
::  Add menu entry if not already present
:: -------------------------------------------------------------------
if not exist "%NUKE_DIR%\menu.py" (
    echo  Creating menu.py with Visual Layer Inspector entry...
    (
        echo import visual_layer_inspector
        echo nuke.menu^("Nuke"^).addCommand^(
        echo     "Filter/Visual Layer Inspector ^(Python^)",
        echo     "visual_layer_inspector.launch^(^)",
        echo     ""
        echo ^)
    ) > "%NUKE_DIR%\menu.py"
    echo    OK: menu.py created
) else (
    findstr /C:"visual_layer_inspector" "%NUKE_DIR%\menu.py" >nul 2>&1
    if !errorlevel! neq 0 (
        echo  Adding menu entry to existing menu.py...
        echo.>> "%NUKE_DIR%\menu.py"
        echo import visual_layer_inspector>> "%NUKE_DIR%\menu.py"
        echo nuke.menu("Nuke").addCommand("Filter/Visual Layer Inspector (Python)", "visual_layer_inspector.launch()", "")>> "%NUKE_DIR%\menu.py"
        echo    OK: menu entry added
    ) else (
        echo  menu.py already has Visual Layer Inspector entry — skipping.
    )
)

:: -------------------------------------------------------------------
::  Nuke version note
:: -------------------------------------------------------------------
echo.
if !INSTALLED_DLL!==1 (
    echo  NOTE: The installer picked one DLL. If you need the other
    echo  Nuke version, manually copy the correct DLL from:
    if exist "%~dp0nuke14\VisualLayerInspector.dll" echo    nuke14\VisualLayerInspector.dll
    if exist "%~dp0nuke16\VisualLayerInspector.dll" echo    nuke16\VisualLayerInspector.dll
    echo  to: %NUKE_DIR%\VisualLayerInspector.dll
) else (
    echo  No C++ plugin found in package — Python version installed only.
)

echo.
echo  ==========================================
echo  Done! Restart Nuke to use the inspector.
echo  ==========================================
echo.
pause
endlocal
