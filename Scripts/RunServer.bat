@echo off
REM ===========================================================================
REM  ONE BRAHAM - dedicated server
REM ---------------------------------------------------------------------------
REM  Runs the Shipping dedicated server against the cooked build. This is the
REM  real thing: no renderer, no local player, 60 Hz net tick, and the whole
REM  match simulation - lockdown, extraction, purge, AI-free PvP.
REM
REM  Set the map and mode from the command line:
REM      RunServer.bat                          ASCENT on the persistent map
REM      RunServer.bat -Mode=FireDrill          FIRE DRILL, 6v6
REM      RunServer.bat -Port=7810               a second instance on one box
REM
REM  The server writes its own log to Saved\Logs\OneBrahamServer.log. If a match
REM  is not starting, that log will say why on the first ten lines.
REM ===========================================================================

setlocal

set GAME_DIR=%~dp0..
set BUILD_DIR=%GAME_DIR%\Build\WindowsServer
set SERVER_EXE=%BUILD_DIR%\OneBrahamServer.exe

if not exist "%SERVER_EXE%" (
    echo.
    echo   OneBrahamServer.exe was not found at:
    echo     %SERVER_EXE%
    echo.
    echo   Package the server first:
    echo     Scripts\Package.bat Server
    echo.
    exit /b 1
)

set DEFAULT_ARGS=OneBrahamServer-L_Persistent_OneBraham -server -log -SteamServerName=One-Braham-London -MaxPlayers=16

if "%~1"=="" (
    echo [OB] starting dedicated server with defaults
    "%SERVER_EXE%" %DEFAULT_ARGS%
) else (
    echo [OB] starting dedicated server with: %*
    "%SERVER_EXE%" %DEFAULT_ARGS% %*
)

endlocal
