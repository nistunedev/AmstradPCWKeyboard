@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "PASMO=%PROJECT_DIR%pasmo.exe"
set "IDSK=%PROJECT_DIR%iDSK.exe"
set "SOURCE=%PROJECT_DIR%keytest.asm"
set "OUTPUT=%PROJECT_DIR%KEYTEST.COM"
set "DISK=%PROJECT_DIR%CPM_14_keyboard.dsk"

if not exist "%PASMO%" (
    echo ERROR: PASMO not found at "%PASMO%"
    exit /b 1
)

if not exist "%IDSK%" (
    echo ERROR: iDSK not found at "%IDSK%"
    exit /b 1
)

if not exist "%SOURCE%" (
    echo ERROR: Source file not found at "%SOURCE%"
    exit /b 1
)

if not exist "%DISK%" (
    echo ERROR: Disk image not found at "%DISK%"
    exit /b 1
)

echo Assembling KEYTEST.COM...
"%PASMO%" --bin "%SOURCE%" "%OUTPUT%"
if errorlevel 1 (
    echo ERROR: PASMO assembly failed.
    exit /b 1
)

echo Importing KEYTEST.COM into CPM_14_keyboard.dsk...
"%IDSK%" "%DISK%" -i "%OUTPUT%" -t 1 -f
if errorlevel 1 (
    echo ERROR: iDSK import failed.
    exit /b 1
)

echo Disk catalog:
"%IDSK%" "%DISK%" -l
if errorlevel 1 (
    echo ERROR: iDSK catalog listing failed.
    exit /b 1
)

echo Build and disk update completed successfully.
exit /b 0
