@echo off
REM ===========================================================================
REM  ONE BRAHAM - Steam build upload
REM ---------------------------------------------------------------------------
REM  Uploads the packaged shipping build with steamcmd. This is the last step of
REM  the release checklist and it is never automatic: a build is uploaded after
REM  the checklist in Docs/20_Release_Checklists.md has been signed off.
REM
REM  Before the first upload:
REM    1. Set the shipping AppId in Config/DefaultEngine.ini
REM       [/Script/OnlineSubsystemSteam.SteamNetDriver] and SteamDevAppId.
REM    2. Replace 480 in the depots file with the real depot ids.
REM    3. Set steamcmd login ^(never commit credentials^).
REM
REM  Usage:
REM      SteamUpload.bat "C:\steamcmd" "C:\builds\OneBraham\steam"
REM ===========================================================================

setlocal

set STEAMCMD=%1
set CONTENTDIR=%2
set SCRIPTFILE=%~dp0steam_app_build.vdf

if "%STEAMCMD%"=="" set STEAMCMD=C:\steamcmd
if "%CONTENTDIR%"=="" set CONTENTDIR=%~dp0..\Build\Steam

if not exist "%STEAMCMD%\steamcmd.exe" (
    echo [OB] steamcmd not found at %STEAMCMD%\steamcmd.exe
    exit /b 1
)

echo [OB] uploading %CONTENTDIR% with %STEAMCMD%
"%STEAMCMD%\steamcmd.exe" +login one_braham +run_app_build "%SCRIPTFILE%" +quit

endlocal
