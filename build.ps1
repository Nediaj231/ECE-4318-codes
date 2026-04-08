$ErrorActionPreference = "Stop"

$GCC = "gcc"
if (Test-Path "C:\msys64\mingw64\bin\gcc.exe") {
    $GCC = "C:\msys64\mingw64\bin\gcc.exe"
    $env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
} else {
    try {
        Get-Command gcc -ErrorAction Stop | Out-Null
    } catch {
        Write-Host "gcc is not recognized. Please install a C compiler like MinGW-w64." -ForegroundColor Red
        exit 1
    }
}

& $GCC -O2 -o engine.exe Updated_engine.c

if ($LASTEXITCODE -eq 0) {
    Write-Host "Successfully built engine.exe." -ForegroundColor Green
} else {
    Write-Host "Compilation failed. Exit code: $LASTEXITCODE" -ForegroundColor Red
}
