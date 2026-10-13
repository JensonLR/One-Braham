@echo off
REM ===========================================================================
REM  ONE BRAHAM - package
REM ---------------------------------------------------------------------------
REM  Cooks and packages a build with Unreal Automation Tool. Every artifact the
REM  release checklist expects is produced here; nothing is packaged by hand.
REM
REM      Package.bat Client           Windows shipping client
REM      Package.bat Server           Windows shipping dedicated server
REM      Package.bat LinuxServer      Linux dedicated server (the rented boxes)
REM      Package.bat Client Development   dev build, for QA
REM
REM  The cooked build is written to Build\<target>. The content cook list lives
REM  in Config/DefaultGame.ini, so a map added to the project but not to the
REM  cook list is a map that does not ship - add it there first.
REM ===========================================================================

setlocal

set GAME_DIR=%~dp0..
set PROJECT=%GAME_DIR%\OneBraham.uproject
set UAT=%UE_UAT%

if "%UAT%"=="" (
    if exist "%ProgramFiles%\Epic Games\UE_5.5\Engine\Build\BatchFiles\RunUAT.bat" (
        set UAT=%ProgramFiles%\Epic Games\UE_5.5\Engine\Build\BatchFiles\RunUAT.bat
    )
)

if "%UAT%"=="" (
    echo [OB] Set UE_UAT to your RunUAT.bat path.
    exit /b 1
)

set TARGET=%1
if "%TARGET%"=="" set TARGET=Client
set CONFIG=%2
if "%CONFIG%"=="" set CONFIG=Shipping

if /I "%TARGET%"=="Server" (
    set PLATFORM=Win64
    set OUNAME=OneBrahamServer
    set OUTDIR=%GAME_DIR%\Build\WindowsServer
) else if /I "%TARGET%"=="LinuxServer" (
    set PLATFORM=Linux
    set OUNAME=OneBrahamServer
    set OUTDIR=%GAME_DIR%\Build\LinuxServer
) else (
    set PLATFORM=Win64
    set OUNAME=OneBraham
    set OUTDIR=%GAME_DIR%\Build\WindowsClient
)

echo [OB] packaging %OUNAME% for %PLATFORM% (%CONFIG%)

"%UAT%" BuildCookRun ^
    -project="%PROJECT%" ^
    -noP4 ^
    -platform=%PLATFORM% ^
    -clientconfig=%CONFIG% ^
    -serverconfig=%CONFIG% ^
    -cook ^
    -allmaps ^
    -build ^
    -stage ^
    -pak ^
    -iostore ^
    -compressed ^
    -prereqs ^
    -archive ^
    -archivedirectory="%OUTDIR%"

if errorlevel 1 (
    echo [OB] packaging FAILED
    exit /b 1
)

echo [OB] packaged to %OUTDIR%
endlocal
