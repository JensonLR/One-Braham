@echo off
REM ===========================================================================
REM  ONE BRAHAM - automation tests
REM ---------------------------------------------------------------------------
REM  Runs the project's automation tests: the unit tests behind -ExecCmds and
REM  the functional tests that drive a real match in PIE.
REM
REM      RunTests.bat                        all OneBraham.* tests
REM      RunTests.bat OneBraham.Weapons      one category
REM      RunTests.bat smoke                  the match-boot composition test
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

REM "smoke" is the match-boot composition test. It is an editor-context test
REM (it loads classes and config the way a booting server does), so it runs in
REM the headless editor like every other test. It used to be pointed at the
REM packaged Shipping server, which has no automation framework to run it.
if /I "%FILTER%"=="smoke" set FILTER=OneBraham.Multiplayer.Smoke

echo [OB] running automation tests: %FILTER%
"%EDITOR%" "%PROJECT%" ^
    -ExecCmds="Automation RunTests %FILTER%;Quit" ^
    -TestExit="Automation Test Queue Empty" ^
    -unattended -nopause -nosplash -NullRHI -stdout -FullStdOutLogOutput

endlocal
