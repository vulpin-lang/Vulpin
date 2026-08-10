@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "BINARY=%PROJECT_ROOT%\src\vulpin.exe"

if not exist "%BINARY%" (
    echo [vulpin] Building from source...
    where gcc >nul 2>&1
    if errorlevel 1 (
        echo [vulpin] ERROR: gcc not found.
        exit /b 1
    )
    pushd "%PROJECT_ROOT%\src"
    gcc -O2 -o vulpin.exe main.c lexer.c parser.c vm.c vulpin.c -lm
    if errorlevel 1 (
        echo [vulpin] ERROR: Build failed.
        popd
        exit /b 1
    )
    popd
    echo [vulpin] Build complete.
)

"%BINARY%" %*
endlocal
