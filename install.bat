@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copies the built plugin into the per-user LV2 folder, or into a folder given as the first argument.
setlocal
set "SRC=%~dp0build\canary-plus.lv2"
set "DEST=%~1"
if "%DEST%"=="" set "DEST=%APPDATA%\LV2"
if not exist "%SRC%\canary-plus.dll" (
    echo Plugin not built yet. Run build.bat first.
    exit /b 1
)
if not exist "%DEST%\canary-plus.lv2" mkdir "%DEST%\canary-plus.lv2"
copy /y "%SRC%\*" "%DEST%\canary-plus.lv2\" >nul || exit /b 1
echo Installed to %DEST%\canary-plus.lv2
