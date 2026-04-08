$ErrorActionPreference = "Stop"
$SCRIPT_DIR = $PSScriptRoot

if (!(Test-Path "$SCRIPT_DIR\engine.exe")) {
    Write-Host "engine.exe not found! Please run build.ps1 first." -ForegroundColor Red
    exit 1
}

& "$SCRIPT_DIR\engine.exe"
