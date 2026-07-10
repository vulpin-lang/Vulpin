@echo off
setlocal enabledelayedexpansion
set "ORIG_DIR=%CD%"
set "INPUT_ARG=%~1"
cd /d "%~dp0.."
set "PROJECT_ROOT=%CD%"
set "SRC_FILE=%PROJECT_ROOT%\src\vulpin.rs"
set "EXE_FILE=%PROJECT_ROOT%\vulpin.exe"
if not exist "%SRC_FILE%" (
    echo Error: Source file not found at %SRC_FILE%
    exit /b 1
)
if "%INPUT_ARG%"=="" (
    echo Usage: vulpin [command] [args...]
    echo Example: vulpin app.vul
    exit /b 0
)
if exist "%ORIG_DIR%\%INPUT_ARG%" (
    set "RESOLVED_ARG=%ORIG_DIR%\%INPUT_ARG%"
) else (
    set "RESOLVED_ARG=%INPUT_ARG%"
)
rustc -C opt-level=3 "%SRC_FILE%" -o "%EXE_FILE%"

if errorlevel 1 (
    echo Compilation failed!
    exit /b 1
)
"%EXE_FILE%" "%RESOLVED_ARG%"

endlocal
