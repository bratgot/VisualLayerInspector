@echo off
:: build_all.bat — Build VisualLayerInspector for both Nuke 14 and Nuke 16
::
:: Usage:  build_all.bat
:: Run from the repo root (C:\dev\VisualLayerInspector)
::
:: Prerequisites:
::   - Both Nuke versions installed
::   - Qt 5.15 and Qt 6.5.3 SDKs installed
::   - Run from a "x64 Native Tools Command Prompt for VS 2022"

setlocal enabledelayedexpansion

:: -------------------------------------------------------------------
::  Configuration — adjust these paths to match your setup
:: -------------------------------------------------------------------
set NUKE14_DIR=C:/Program Files/Nuke14.1v8
set NUKE16_DIR=C:/Program Files/Nuke16.0v8

set QT5_SDK=C:/Qt/5.15.2/msvc2019_64
set QT6_SDK=C:/Qt/6.5.3/msvc2019_64

set INSTALL_DIR=%USERPROFILE%\.nuke\VisualLayerInspector

:: -------------------------------------------------------------------
::  Remember current branch and stash any uncommitted changes
:: -------------------------------------------------------------------
for /f "tokens=*" %%a in ('git rev-parse --abbrev-ref HEAD') do set ORIGINAL_BRANCH=%%a
echo Current branch: !ORIGINAL_BRANCH!

:: Check if there are uncommitted changes to stash
set STASHED=0
git diff --quiet --exit-code 2>nul
if !errorlevel! neq 0 (
    set STASHED=1
) else (
    git diff --cached --quiet --exit-code 2>nul
    if !errorlevel! neq 0 set STASHED=1
)

if !STASHED!==1 (
    echo Stashing uncommitted changes...
    git stash push -m "build_all: auto-stash before multi-version build"
    if !errorlevel! neq 0 (
        echo ERROR: git stash failed
        exit /b 1
    )
) else (
    echo No uncommitted changes to stash.
)
echo.

:: -------------------------------------------------------------------
::  Build Nuke 14
:: -------------------------------------------------------------------
echo ============================================================
echo  Building for Nuke 14.1 (Qt 5^)
echo ============================================================

git checkout nuke/14.1
if !errorlevel! neq 0 (
    echo ERROR: Could not checkout nuke/14.1 branch
    goto :error
)

if exist build-nuke14 rd /s /q build-nuke14
mkdir build-nuke14
pushd build-nuke14

cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DNUKE_INSTALL_DIR="%NUKE14_DIR%" ^
    -DQT5_SDK_DIR="%QT5_SDK%" ^
    -DCMAKE_PREFIX_PATH="%QT5_SDK%"

if !errorlevel! neq 0 (
    echo ERROR: CMake configure failed for Nuke 14
    popd
    goto :error
)

cmake --build . --config Release
if !errorlevel! neq 0 (
    echo ERROR: Build failed for Nuke 14
    popd
    goto :error
)

popd
echo.
echo Nuke 14 build OK: build-nuke14\Release\VisualLayerInspector.dll
echo.

:: -------------------------------------------------------------------
::  Build Nuke 16
:: -------------------------------------------------------------------
echo ============================================================
echo  Building for Nuke 16 (Qt 6^)
echo ============================================================

git checkout main
if !errorlevel! neq 0 (
    echo ERROR: Could not checkout main branch
    goto :error
)

if exist build-nuke16 rd /s /q build-nuke16
mkdir build-nuke16
pushd build-nuke16

cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DNUKE_INSTALL_DIR="%NUKE16_DIR%" ^
    -DQT6_SDK_DIR="%QT6_SDK%" ^
    -DCMAKE_PREFIX_PATH="%QT6_SDK%"

if !errorlevel! neq 0 (
    echo ERROR: CMake configure failed for Nuke 16
    popd
    goto :error
)

cmake --build . --config Release
if !errorlevel! neq 0 (
    echo ERROR: Build failed for Nuke 16
    popd
    goto :error
)

popd
echo.
echo Nuke 16 build OK: build-nuke16\Release\VisualLayerInspector.dll
echo.

:: -------------------------------------------------------------------
::  Install
:: -------------------------------------------------------------------
echo ============================================================
echo  Installing to %INSTALL_DIR%
echo ============================================================

if not exist "%INSTALL_DIR%\nuke14" mkdir "%INSTALL_DIR%\nuke14" 2>nul
if not exist "%INSTALL_DIR%\nuke16" mkdir "%INSTALL_DIR%\nuke16" 2>nul

copy /Y "build-nuke14\Release\VisualLayerInspector.dll" "%INSTALL_DIR%\nuke14\"
if !errorlevel! neq 0 (
    echo WARNING: Failed to copy Nuke 14 DLL
)

copy /Y "build-nuke16\Release\VisualLayerInspector.dll" "%INSTALL_DIR%\nuke16\"
if !errorlevel! neq 0 (
    echo WARNING: Failed to copy Nuke 16 DLL
)

copy /Y "src\visual_layer_inspector.py" "%USERPROFILE%\.nuke\"
if !errorlevel! neq 0 (
    echo WARNING: Failed to copy Python script
)

:: -------------------------------------------------------------------
::  Return to original branch and restore stash
:: -------------------------------------------------------------------
git checkout !ORIGINAL_BRANCH!
if !STASHED!==1 (
    echo Restoring stashed changes...
    git stash pop
)

echo.
echo ============================================================
echo  Done! Both versions installed:
echo    Nuke 14: %INSTALL_DIR%\nuke14\VisualLayerInspector.dll
echo    Nuke 16: %INSTALL_DIR%\nuke16\VisualLayerInspector.dll
echo    Python:  %USERPROFILE%\.nuke\visual_layer_inspector.py
echo ============================================================
goto :end

:error
echo.
echo Build failed. Returning to original branch...
git checkout !ORIGINAL_BRANCH!
if !STASHED!==1 (
    echo Restoring stashed changes...
    git stash pop
)
pause
exit /b 1

:end
pause
endlocal
