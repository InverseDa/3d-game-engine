@echo off
chcp 65001 >nul
setlocal

set "ROOT_DIR=%~dp0"
set "SHARPMAKE_ROOT=%ROOT_DIR%External\Sharpmake"
set "SHARPMAKE_PROJECT=%SHARPMAKE_ROOT%\Sharpmake.Application\Sharpmake.Application.csproj"
set "SHARPMAKE_CONFIG=Release"
set "SHARPMAKE_FRAMEWORK=net8.0"
set "SHARPMAKE_OUTPUT_DIR=%SHARPMAKE_ROOT%\Sharpmake.Application\bin\%SHARPMAKE_CONFIG%\%SHARPMAKE_FRAMEWORK%"

echo [1/4] Auto-discovering modules...
powershell -ExecutionPolicy Bypass -File "%ROOT_DIR%GenerateIncludes.ps1"

echo.
echo [2/4] Validating Sharpmake toolchain...

if not exist "%SHARPMAKE_ROOT%" (
    echo Error: Sharpmake submodule not found at %SHARPMAKE_ROOT%
    echo Run: git submodule update --init --recursive
    pause
    exit /b 1
)

where dotnet >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: dotnet SDK is required to build and run Sharpmake.
    pause
    exit /b 1
)

set "MAIN_SCRIPT=Engine/Builder/LimitlessBuilder.cs"

echo.
echo [3/4] Building Sharpmake...
dotnet build "%SHARPMAKE_PROJECT%" -c %SHARPMAKE_CONFIG% -nologo

if %errorlevel% neq 0 (
    echo Error: Sharpmake build failed!
    pause
    exit /b %errorlevel%
)

set "SHARPMAKE_EXE=%SHARPMAKE_OUTPUT_DIR%\Sharpmake.Application.exe"
set "SHARPMAKE_DLL=%SHARPMAKE_OUTPUT_DIR%\Sharpmake.Application.dll"

echo.
echo [4/4] Running Sharpmake...
if exist "%SHARPMAKE_EXE%" (
    "%SHARPMAKE_EXE%" "/sources('%MAIN_SCRIPT%')"
) else (
    if exist "%SHARPMAKE_DLL%" (
        dotnet "%SHARPMAKE_DLL%" "/sources('%MAIN_SCRIPT%')"
    ) else (
        echo Error: No runnable Sharpmake output found in %SHARPMAKE_OUTPUT_DIR%
        pause
        exit /b 1
    )
)

if %errorlevel% neq 0 (
    echo Error: Sharpmake failed!
    pause
    exit /b %errorlevel%
)

echo.
echo Done! Limitless Engine project generated.
pause
