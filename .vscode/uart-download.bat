@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "WORK_DIR=%SCRIPT_DIR%\.."
for %%I in ("%WORK_DIR%") do set "WORK_DIR=%%~fI"

set "TOOL=%WORK_DIR%\tools\official\GD32_Mass_Production_Tool\GD32MassProductionTool_CMD.exe"
set "TOOL_DIR=%WORK_DIR%\tools\official\GD32_Mass_Production_Tool"
set "IMAGE=%WORK_DIR%\scripts\images\image-all.bin"
set "CHIP=GD32VW553"
set "BAUD=%~1"
set "ASSIST=without_gd32f303"
set "ERASE=page_erase"

if "%BAUD%"=="" set "BAUD=921600"

if not exist "%TOOL%" (
    echo Missing UART download tool: %TOOL%
    exit /b 1
)

if not exist "%IMAGE%" (
    echo Missing image: %IMAGE%
    echo Run "Build ALL" first.
    exit /b 1
)

echo.
echo UART download requires the chip ROM bootloader mode:
echo   1. Hold BOOT so PC8/BOOT0 is high.
echo   2. Keep PB1/BOOT1 low. On the mini board, connect B01 to GND while downloading.
echo   3. Press RESET once, then release RESET while still holding BOOT.
echo   4. Make sure COM5 is not opened by the serial monitor.
echo.
echo Tool:  %TOOL%
echo Image: %IMAGE%
echo Chip:  %CHIP%
echo Baud:  %BAUD%
echo Mode:  %ASSIST%
echo Erase: %ERASE%
echo.

pushd "%TOOL_DIR%"
"%TOOL%" download %CHIP% %BAUD% %ASSIST% "%IMAGE%" %ERASE%
set "RET=%ERRORLEVEL%"
popd
exit /b %RET%
