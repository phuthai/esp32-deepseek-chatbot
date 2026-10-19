@echo off
setlocal
cd /d "%~dp0"

if not exist "tools\build.py" (
  echo [!] tools\build.py is missing.
  exit /b 1
)
if not exist "tools\arduino-cli.exe" (
  echo [!] Toolchain not found.
  echo     Run:  python tools\setup_toolchain.py
  exit /b 1
)

python tools\build.py %*
if errorlevel 1 exit /b 1
