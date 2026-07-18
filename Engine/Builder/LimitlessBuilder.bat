@echo off
setlocal

set "NODE_EXE="
if defined LIMITLESS_BUILDER_NODE set "NODE_EXE=%LIMITLESS_BUILDER_NODE%"
if not defined NODE_EXE (
    for /f "tokens=2,*" %%A in ('reg.exe query "HKCU\Environment" /v LIMITLESS_BUILDER_NODE 2^>nul ^| findstr /R /C:"REG_SZ"') do (
        if exist "%%B" set "NODE_EXE=%%B"
    )
)

if not defined NODE_EXE (
    for /f "delims=" %%I in ('where.exe node 2^>nul') do (
        set "NODE_EXE=%%I"
        goto :VerifyNode
    )
)

if not defined NODE_EXE call :FindNodeAppPath "HKCU\Software\Microsoft\Windows\CurrentVersion\App Paths\node.exe"
if not defined NODE_EXE call :FindNodeAppPath "HKLM\Software\Microsoft\Windows\CurrentVersion\App Paths\node.exe"
if not defined NODE_EXE call :FindNodeAppPath "HKLM\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\App Paths\node.exe"
if not defined NODE_EXE call :FindNodeInstallPath "HKLM\Software\Node.js"
if not defined NODE_EXE call :FindNodeFromPersistedPath "HKCU\Environment"
if not defined NODE_EXE call :FindNodeFromPersistedPath "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"

:VerifyNode
if not defined NODE_EXE goto :NodeNotFound
if not exist "%NODE_EXE%" (
    echo Error: LIMITLESS_BUILDER_NODE does not point to node.exe: "%NODE_EXE%"
    exit /b 1
)

set "NODE_MAJOR=0"
set "NODE_MINOR=0"
for /f "tokens=1,2 delims=." %%A in ('""%NODE_EXE%" -p "process.versions.node""') do (
    set "NODE_MAJOR=%%A"
    set "NODE_MINOR=%%B"
)
if %NODE_MAJOR% LSS 22 goto :UnsupportedNode
if %NODE_MAJOR% EQU 22 if %NODE_MINOR% LSS 6 goto :UnsupportedNode

if not defined LIMITLESS_BUILDER_NINJA (
    for /f "tokens=2,*" %%A in ('reg.exe query "HKCU\Environment" /v LIMITLESS_BUILDER_NINJA 2^>nul ^| findstr /R /C:"REG_SZ"') do (
        if exist "%%B" set "LIMITLESS_BUILDER_NINJA=%%B"
    )
)
if not defined LIMITLESS_BUILDER_NINJA call :FindNinja
"%NODE_EXE%" --no-warnings --experimental-strip-types "%~dp0Source\Cli\CommandLine.ts" %*
exit /b %errorlevel%

:FindNodeAppPath
for /f "tokens=2,*" %%A in ('reg.exe query "%~1" /ve 2^>nul ^| findstr /R /C:"REG_SZ"') do (
    if exist "%%B" set "NODE_EXE=%%B"
)
exit /b 0

:FindNodeInstallPath
for /f "tokens=2,*" %%A in ('reg.exe query "%~1" /v InstallPath 2^>nul ^| findstr /R /C:"REG_SZ"') do (
    if exist "%%B\node.exe" set "NODE_EXE=%%B\node.exe"
)
exit /b 0

:FindNodeFromPersistedPath
for /f "tokens=2,*" %%A in ('reg.exe query "%~1" /v Path 2^>nul ^| findstr /R /C:"REG_.*SZ"') do (
    call :FindNodeInPath "%%B"
    if defined NODE_EXE exit /b 0
)
exit /b 0

:FindNodeInPath
set "SEARCH_PATH=%~1"
call set "SEARCH_PATH=%%SEARCH_PATH%%"
for %%I in ("%SEARCH_PATH:;=" "%") do (
    if exist "%%~I\node.exe" call :TryNode "%%~I\node.exe"
)
exit /b 0

:FindNinja
for /f "delims=" %%I in ('where.exe ninja.exe 2^>nul') do (
    set "LIMITLESS_BUILDER_NINJA=%%I"
    exit /b 0
)
call :FindNinjaFromPersistedPath "HKCU\Environment"
if not defined LIMITLESS_BUILDER_NINJA call :FindNinjaFromPersistedPath "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
exit /b 0

:FindNinjaFromPersistedPath
for /f "tokens=2,*" %%A in ('reg.exe query "%~1" /v Path 2^>nul ^| findstr /R /C:"REG_.*SZ"') do (
    call :FindNinjaInPath "%%B"
    if defined LIMITLESS_BUILDER_NINJA exit /b 0
)
exit /b 0

:FindNinjaInPath
set "SEARCH_PATH=%~1"
call set "SEARCH_PATH=%%SEARCH_PATH%%"
for %%I in ("%SEARCH_PATH:;=" "%") do (
    if exist "%%~I\ninja.exe" set "LIMITLESS_BUILDER_NINJA=%%~I\ninja.exe"
)
exit /b 0

:TryNode
set "CANDIDATE_NODE=%~1"
set "CANDIDATE_MAJOR=0"
set "CANDIDATE_MINOR=0"
for /f "tokens=1,2 delims=." %%A in ('""%CANDIDATE_NODE%" -p "process.versions.node""') do (
    set "CANDIDATE_MAJOR=%%A"
    set "CANDIDATE_MINOR=%%B"
)
if %CANDIDATE_MAJOR% LSS 22 exit /b 0
if %CANDIDATE_MAJOR% EQU 22 if %CANDIDATE_MINOR% LSS 6 exit /b 0
set "NODE_EXE=%CANDIDATE_NODE%"
exit /b 0

:NodeNotFound
echo Error: Node.js 22.6 or newer was not found.
echo Install Node.js from https://nodejs.org/ or set LIMITLESS_BUILDER_NODE to node.exe in Rider's build environment.
exit /b 1

:UnsupportedNode
echo Error: Node.js 22.6 or newer is required.
exit /b 1
