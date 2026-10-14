@echo off
REM ===========================================================================
REM  ONE BRAHAM - package (the short form)
REM ---------------------------------------------------------------------------
REM      Package.bat Client                 Windows shipping client
REM      Package.bat Client Development     Windows development client, for QA
REM      Package.bat Server                 Windows shipping dedicated server
REM      Package.bat LinuxServer            Linux dedicated server (cross-compiled)
REM
REM  It calls PackageClient.ps1 / PackageServer.ps1, which hold the BuildCookRun
REM  command lines. This file used to hold its own, and its "Server" packaged a
REM  client: it never passed -server -noclient.
REM ===========================================================================
setlocal
set OB_PS=powershell
where pwsh >nul 2>&1 && set OB_PS=pwsh

set TARGET=%~1
if "%TARGET%"=="" set TARGET=Client
set CONFIG=%~2
if "%CONFIG%"=="" set CONFIG=Shipping

if /I "%TARGET%"=="Client" (
    %OB_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0PackageClient.ps1" -Platform Win64 -Configuration %CONFIG%
    exit /b %errorlevel%
)
if /I "%TARGET%"=="Server" (
    %OB_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0PackageServer.ps1" -Platform Win64 -Configuration %CONFIG%
    exit /b %errorlevel%
)
if /I "%TARGET%"=="LinuxServer" (
    %OB_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0PackageServer.ps1" -Platform Linux -Configuration %CONFIG%
    exit /b %errorlevel%
)

echo [OB] Unknown target "%TARGET%". Use Client, Server or LinuxServer.
exit /b 1
