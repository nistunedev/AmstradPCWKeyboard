@echo off
setlocal

rem Build PCW-side CP/M diagnostics into a host transfer folder.
rem Pasmo Z80 assembler: https://pasmo.speccy.org/
rem
rem Do not use iDSK or cpmtools to modify CPM_14_keyboard.dsk. It is a
rem SAMdisk Extended DSK whose geometry those tools misinterpret. Copy the
rem generated files through CP/M Box drive M: using PIP instead.

set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%cpm_transfer"
set "PASMO=%PROJECT_DIR%pasmo.exe"
set "SOURCE=%PROJECT_DIR%keytest.asm"
set "OUTPUT=%BUILD_DIR%\KEYTEST.COM"
set "HELLO_SOURCE=%PROJECT_DIR%keyhello.asm"
set "HELLO_OUTPUT=%BUILD_DIR%\KEYHELLO.COM"
set "SUBMIT_SOURCE=%PROJECT_DIR%install.sub"
set "SUBMIT_OUTPUT=%BUILD_DIR%\INSTALL.SUB"

if not exist "%PASMO%" (
    echo ERROR: PASMO not found at "%PASMO%"
    exit /b 1
)

if not exist "%SOURCE%" (
    echo ERROR: Source file not found at "%SOURCE%"
    exit /b 1
)

if not exist "%HELLO_SOURCE%" (
    echo ERROR: Source file not found at "%HELLO_SOURCE%"
    exit /b 1
)

if not exist "%SUBMIT_SOURCE%" (
    echo ERROR: Submit file not found at "%SUBMIT_SOURCE%"
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if errorlevel 1 (
    echo ERROR: Could not create transfer directory "%BUILD_DIR%".
    exit /b 1
)

if exist "%OUTPUT%" del /q "%OUTPUT%"
if exist "%HELLO_OUTPUT%" del /q "%HELLO_OUTPUT%"
if exist "%SUBMIT_OUTPUT%" del /q "%SUBMIT_OUTPUT%"

echo Assembling KEYTEST.COM...
"%PASMO%" --bin "%SOURCE%" "%OUTPUT%"
if errorlevel 1 (
    echo ERROR: PASMO assembly failed.
    exit /b 1
)

echo Assembling KEYHELLO.COM...
"%PASMO%" --bin "%HELLO_SOURCE%" "%HELLO_OUTPUT%"
if errorlevel 1 (
    echo ERROR: KEYHELLO assembly failed.
    exit /b 1
)

copy /y "%SUBMIT_SOURCE%" "%SUBMIT_OUTPUT%" >nul
if errorlevel 1 (
    echo ERROR: Could not copy INSTALL.SUB to the transfer directory.
    exit /b 1
)

echo.
echo Build completed successfully.
echo Map this folder as drive M: in CP/M Box:
echo   %BUILD_DIR%
echo.
echo Then copy the files from CP/M:
echo   SUBMIT M:INSTALL
exit /b 0
