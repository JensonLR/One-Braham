@echo off
REM ===========================================================================
REM  ONE BRAHAM - client
REM ---------------------------------------------------------------------------
REM  Launches the packaged shipping client and joins a server.
REM
REM      RunClient.bat                          Steam, main menu
REM      RunClient.bat 127.0.0.1                connect to a local dedicated server
REM      RunClient.bat 127.0.0.1 -windowed -ResX=1600 -ResY=900
REM
REM  A client built with Steam support launches from Steam. Launching the exe
REM  directly bypasses the overlay but not the OnlineSubsystemSteam AppId, which
REM  is read from the packaged ini.
REM ===========================================================================

setlocal

set GAME_DIR=%~dp0..
set BUILD_DIR=%GAME_DIR%\Build\WindowsClient
set CLIENT_EXE=%BUILD_DIR%\OneBraham.exe

if not exist "%CLIENT_EXE%" (
    echo.
    echo   OneBraham.exe was not found at:
    echo     %CLIENT_EXE%
    echo.
    echo   Package the client first:
    echo     Scripts\Package.bat Client
    echo.
    exit /b 1
)

set ADDRESS=%1

if "%ADDRESS%"=="" (
    echo [OB] launching the client to the main menu
    "%CLIENT_EXE%"
) else (
    shift
    echo [OB] connecting to %ADDRESS%
    "%CLIENT_EXE%" %ADDRESS% -log %*
)

endlocal
