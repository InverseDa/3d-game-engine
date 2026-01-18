@echo off
chcp 65001 >nul
setlocal

set "ROOT_DIR=%~dp0"

echo [1/2] Auto-discovering modules...
powershell -ExecutionPolicy Bypass -File "%ROOT_DIR%GenerateIncludes.ps1"

echo.
echo [2/2] Running Sharpmake...

set "SHARPMAKE_EXE=%ROOT_DIR%Engine\Builder\Sharpmake\Sharpmake.Application.exe"

set "MAIN_SCRIPT=Engine/Builder/LimitlessBuilder.cs"

"%SHARPMAKE_EXE%" "/sources('%MAIN_SCRIPT%')"

if %errorlevel% neq 0 (
    echo Error: Sharpmake failed!
    pause
    exit /b %errorlevel%
)

echo.
echo Done! Limitless Engine project generated.
pause
