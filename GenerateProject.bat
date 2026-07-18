@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "LIMITLESS_BUILDER=%ROOT_DIR%Engine\Builder\LimitlessBuilder.bat"

if not exist "%LIMITLESS_BUILDER%" (
    echo Error: LimitlessBuilder was not found: %LIMITLESS_BUILDER%
    exit /b 1
)

call "%LIMITLESS_BUILDER%" sln --platform Win64 --config Debug --type Game
exit /b %errorlevel%
