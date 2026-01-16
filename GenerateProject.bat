@echo off
setlocal

:: 获取当前脚本所在目录 (Root)
set "ROOT_DIR=%~dp0"

echo [1/2] Auto-discovering modules...
:: 调用根目录下的 PS1
powershell -ExecutionPolicy Bypass -File "%ROOT_DIR%GenerateIncludes.ps1"

echo.
echo [2/2] Running Sharpmake...

:: 🟢 关键修改：指向深层目录的 Sharpmake 和 Main 文件
:: Sharpmake Exe 路径 (exe路径可以用反斜杠，没问题)
set "SHARPMAKE_EXE=%ROOT_DIR%Engine\Builder\Sharpmake\Sharpmake.Application.exe"

:: Main 脚本路径 (必须用相对路径 + 正斜杠，否则 Sharpmake 报错 CS1009)
set "MAIN_SCRIPT=Engine/Builder/Engine.Build.cs"

:: 执行
"%SHARPMAKE_EXE%" "/sources('%MAIN_SCRIPT%')"

if %errorlevel% neq 0 (
    echo Error: Sharpmake failed!
    pause
    exit /b %errorlevel%
)

echo.
echo Done! Limitless Engine project generated.
pause