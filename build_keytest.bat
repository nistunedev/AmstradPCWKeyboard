@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "DSK=%PROJECT_DIR%cpm_14_keyboard.dsk"

set "BUILD_TIMESTAMP=%DATE% %TIME:~0,8%"
> "%PROJECT_DIR%build_timestamp.inc" echo         db 'Build: %BUILD_TIMESTAMP%',13,10,'$'

echo Assembling KEYTEST.COM...
pasmo --bin "%PROJECT_DIR%keytest.asm" "%PROJECT_DIR%KEYTEST.COM"
if errorlevel 1 goto :error

echo.
echo Updating cpm_14_keyboard.dsk...
echo DSK : %DSK%

echo Removing existing KEYTEST.COM...
idsk "%DSK%" -r KEYTEST.COM
if errorlevel 1 (
    echo WARNING: Existing KEYTEST.COM was not found or could not be removed.
)

echo Importing rebuilt KEYTEST.COM...
idsk "%DSK%" -f -i "%PROJECT_DIR%KEYTEST.COM" -t 2
if errorlevel 1 goto :error

echo.
echo Build completed successfully.
echo Updated disk image:
echo   %DSK%
echo.
echo Output file:
echo   %PROJECT_DIR%KEYTEST.COM
pause
goto :eof

:error
echo.
echo Build failed.
pause
exit /b 1
