@echo off
setlocal
cd /d "%~dp0"

set "BIN=dist\DeepSeekChat_ESP32S3_4MB.bin"
if not exist "%BIN%" (
  echo [!] %BIN% not found - run build.bat first.
  exit /b 1
)

set "ESPTOOL="
for /d %%D in (arduino-data\packages\esp32\tools\esptool_py\*) do if exist "%%D\esptool.exe" set "ESPTOOL=%%D\esptool.exe"
if "%ESPTOOL%"=="" (
  echo [!] esptool not found. Run:  python tools\setup_toolchain.py
  exit /b 1
)

set "PORT=%~1"
if "%PORT%"=="" (
  echo Usage: flash.bat COM5
  echo.
  echo Detected serial ports:
  "tools\arduino-cli.exe" --config-file arduino-cli.yaml board list
  exit /b 1
)

if not exist "build-temp" mkdir "build-temp"
set TEMP=%CD%\build-temp
set TMP=%CD%\build-temp

echo Flashing %BIN% to %PORT% ...
"%ESPTOOL%" --chip esp32s3 --port %PORT% -b 460800 --before default_reset --after hard_reset ^
  write_flash 0x0 "%BIN%"
if errorlevel 1 (
  echo.
  echo [!] Flash failed. Hold BOOT, tap RST, release BOOT, then run this again.
  exit /b 1
)
echo.
echo Done. Open a serial terminal on %PORT% at 115200 baud.
