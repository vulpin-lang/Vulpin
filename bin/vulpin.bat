@echo off
setlocal

:: Change to the project root directory (one level up from bin)
cd /d "%~dp0.."

:: Now we're in the project root
set "PROJECT_ROOT=%CD%"
set "SRC_FILE=%PROJECT_ROOT%\src\vulpin.rs"
set "EXE_FILE=%PROJECT_ROOT%\vulpin.exe"

if not exist "%SRC_FILE%" (
    echo Error: Source file not found at %SRC_FILE%
)

if "%~1"=="" (
    echo Usage: vulpin [command]
)

rustc -C opt-level=3 "%SRC_FILE%" -o "%EXE_FILE%"

if errorlevel 1 (
    echo Compilation failed!
    exit /b 1
)

"%EXE_FILE%" %*

endlocal
