@echo off
setlocal

set "PROJECT_DIR=D:\PCW\projects\AmstradPCWKeyboard\"
set "DSK=%PROJECT_DIR%cpm_14_keyboard.dsk"

echo Assembling KEYTEST.COM...
pasmo --bin "%PROJECT_DIR%keytest.asm" "%PROJECT_DIR%KEYTEST.COM"
if errorlevel 1 goto :error

echo.
echo Updating cpm_14_keyboard.dsk...
echo DSK : %DSK%

idsk "%DSK%" -f -i "%PROJECT_DIR%KEYTEST.COM"
if errorlevel 1 goto :error

echo.
echo Build completed successfully.
echo Updated disk image:
echo   %DSK%
echo.
echo Output file:
echo   %PROJECT_DIR%KEYTEST.COM
goto :eof

:error
echo.
echo Build failed.
exit /b 1
