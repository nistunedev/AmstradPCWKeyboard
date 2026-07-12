@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "TRANSFER_DIR=%PROJECT_DIR%cpm_transfer"

set "BUILD_TIMESTAMP=%DATE% %TIME:~0,8%"
> "%PROJECT_DIR%build_timestamp.inc" echo         db 'Build: %BUILD_TIMESTAMP%',13,10,'$'

if not exist "%TRANSFER_DIR%" mkdir "%TRANSFER_DIR%"

echo Assembling KEYTEST.COM...
pasmo --bin "%PROJECT_DIR%keytest.asm" "%PROJECT_DIR%KEYTEST.COM"
if errorlevel 1 goto :error


echo Preparing CP/M transfer folder...
copy /Y "%PROJECT_DIR%KEYTEST.COM" "%TRANSFER_DIR%\KEYTEST.COM" > nul
copy /Y "%PROJECT_DIR%install.sub" "%TRANSFER_DIR%\INSTALL.SUB" > nul

echo.
echo Build completed successfully.
echo Headerless CP/M COM files are ready in:
echo   %TRANSFER_DIR%
echo.
echo To update the disk safely, map cpm_transfer as M: in CP/M Box and run:
echo   SUBMIT M:INSTALL
echo.
echo Do not use iDSK to write CPM_14_keyboard.dsk; see idsk_issues.md.
pause
goto :eof

:error
echo.
echo Build failed.
pause
exit /b 1