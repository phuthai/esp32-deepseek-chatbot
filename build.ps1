#requires -version 5.1
# Build the DeepSeek ESP32-S3 chatbot and merge everything into one .bin.
# All real work happens in tools/build.py (also handles the Windows long-path
# workaround).
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Test-Path "tools\arduino-cli.exe")) {
    Write-Host "[!] Toolchain not found. Run:  python tools\setup_toolchain.py" -ForegroundColor Yellow
    exit 1
}

python tools\build.py
exit $LASTEXITCODE
