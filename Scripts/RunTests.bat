@echo off
REM ===========================================================================
REM  ONE BRAHAM - automation tests
REM ---------------------------------------------------------------------------
REM  Runs the project's automation tests: the unit tests behind -ExecCmds and
REM  the functional tests that drive a real match in PIE.
REM
REM      RunTests.bat                        all OneBraham.* tests
REM      RunTests.bat OneBraham.Weapons      one category
REM      RunTests.bat smoke                  the 12-player dedicated smoke test
REM
REM  See Docs/18_Testing_Performance.md for what each category covers and for
REM  the manual QA pass that automation cannot replace.
REM ===========================================================================

setlocal

set GAME_DIR=%~dp0..
set PROJECT=%GAME_DIR%\OneBraham.uproject
set EDITOR=%UE_EDITOR%

if "%EDITOR%"=="" (
    set EDITOR=%ProgramFiles%\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
)

set FILTER=%1
if "%FILTER%"=="" set FILTER=OneBraham

if /I "%FILTER%"=="smoke" (
    echo [OB] running the 12-player dedicated smoke test
    call "%GAME_DIR%\Scripts\RunServer.bat" -ExecCmds="Automation RunTests OneBraham.Multiplayer.Smoke;Quit" -testexit="Automation Test Queue Empty"
    exit /b %errorlevel%
)

echo [OB] running automation tests: %FILTER%
"%EDITOR%" "%PROJECT%" ^
    -ExecCmds="Automation RunTests %FILTER%;Quit" ^
    -TestExit="Automation Test Queue Empty" ^
    -unattended -nopause -nosplash -NullRHI -stdout -FullStdOutLogOutput

endlocal
