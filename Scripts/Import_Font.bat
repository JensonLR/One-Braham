@echo off
REM ===========================================================================
REM  ONE BRAHAM - import the UI font
REM ---------------------------------------------------------------------------
REM  The HUD falls back to Roboto until F_OneBrahamSans exists. This script
REM  imports a licensed grotesque and sets the project font in one step.
REM
REM  Where to get one that is safe to ship: Docs/15_Asset_Generation_Prompts.md
REM  lists OFL-licensed grotesques that read as the brand without imitating
REM  anybody's wordmark.
REM
REM  Usage:
REM      Import_Font.bat "C:\fonts\YourFont-Regular.ttf"
REM      Import_Font.bat "C:\fonts\YourFont-Regular.ttf" "OneBrahamSans"
REM ===========================================================================

setlocal

set FONT_FILE=%1
set DEST_NAME=%2
if "%DEST_NAME%"=="" set DEST_NAME=OneBrahamSans

if "%FONT_FILE%"=="" (
    echo [OB] usage: Import_Font.bat ^<path to .ttf^> [asset name]
    exit /b 1
)

if not exist "%FONT_FILE%" (
    echo [OB] no font at %FONT_FILE%
    exit /b 1
)

set GAME_DIR=%~dp0..
set DEST_DIR=%GAME_DIR%\Content\UI\Fonts

if not exist "%DEST_DIR%" mkdir "%DEST_DIR%"

copy /y "%FONT_FILE%" "%DEST_DIR%\" >nul
echo [OB] copied %FONT_FILE% to %DEST_DIR%

set EDITOR=%UE_EDITOR%
if "%EDITOR%"=="" set EDITOR=%ProgramFiles%\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe

echo [OB] importing as /Game/OneBraham/UI/Fonts/%DEST_NAME%
"%EDITOR%" "%GAME_DIR%\OneBraham.uproject" ^
    -run=pythonscript ^
    -script="Tools/Python/onebraham/import_font.py" ^
    -unattended -nopause -stdout -ExtraArgs="%DEST_NAME%"

endlocal
