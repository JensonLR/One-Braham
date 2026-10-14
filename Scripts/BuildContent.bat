@echo off
REM ===========================================================================
REM  ONE BRAHAM - build every piece of content
REM ---------------------------------------------------------------------------
REM  Runs Tools/Python/onebraham/run_all.py inside the editor: materials, the
REM  map, props, weapons, the cast, audio, input, UI and VFX - then validates
REM  the project. Exit code 0 means every step succeeded.
REM
REM      BuildContent.bat                 everything
REM      BuildContent.bat --only weapons  just the weapons
REM      BuildContent.bat --text-only     only the steps that need no editor
REM ===========================================================================

setlocal

set GAME_DIR=%~dp0..
set PROJECT=%GAME_DIR%\OneBraham.uproject
set EDITOR=%UE_EDITOR%

if "%EDITOR%"=="" (
    if exist "%ProgramFiles%\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (
        set EDITOR=%ProgramFiles%\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
    )
)

if "%EDITOR%"=="" (
    echo [OB] Set UE_EDITOR to your UnrealEditor-Cmd.exe path, e.g.
    echo      set UE_EDITOR=C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
    exit /b 1
)

echo [OB] building content with %EDITOR%
"%EDITOR%" "%PROJECT%" -run=pythonscript -script="Tools/Python/onebraham/run_all.py" -unattended -nopause -stdout -FullStdOutLogOutput -ExtraArgs=%*

endlocal
