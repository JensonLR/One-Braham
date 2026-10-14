@echo off
REM ===========================================================================
REM  ONE BRAHAM - every check that needs no engine
REM ---------------------------------------------------------------------------
REM  Runs the content pipeline and the project validator with nothing but
REM  Python 3: no Unreal install, no build, no editor. This is the command a
REM  fresh clone can run, a CI box without an engine can run, and a contributor
REM  can run before pushing.
REM
REM      RunAll.bat                    the engine-free steps, then the validator
REM      RunAll.bat --only calls       pass switches straight to run_all.py
REM      RunAll.bat --config-only      the validator's config-only pass
REM
REM  With no editor on the machine the content steps report SKIPPED rather than
REM  failing, and the exit code stays 0 when nothing failed - so this script's
REM  exit code means "the project is consistent", not "there is an engine here".
REM
REM  See Docs/17_Build_Automation.md for what each step checks and
REM  Docs/18_Testing_Performance.md for the tests that need the editor.
REM ===========================================================================

setlocal enabledelayedexpansion

set GAME_DIR=%~dp0..
set PY_ROOT=%GAME_DIR%\Tools\Python
set CONFIG_ONLY=

REM Collect our own switches, forwarding the rest to run_all.py.
set PASSTHROUGH=
for %%A in (%*) do (
    if /I "%%A"=="--config-only" (
        set CONFIG_ONLY=--config-only
    ) else (
        set PASSTHROUGH=!PASSTHROUGH! %%A
    )
)

REM --- Python ---------------------------------------------------------------
REM python3 first (a real install), then the Windows launcher. The Microsoft
REM Store's python3.exe stub runs and returns nothing, so it is checked by
REM asking for a version rather than by finding it on the PATH.
set PYTHON=
for %%P in (python3.exe python.exe py.exe) do (
    if "!PYTHON!"=="" (
        %%P -c "import sys; raise SystemExit(0 if sys.version_info[0] == 3 else 1)" >nul 2>&1
        if !errorlevel! == 0 set PYTHON=%%P
    )
)

if "!PYTHON!"=="" (
    echo.
    echo   [OB] No Python 3 found on the PATH.
    echo.
    echo   The engine-free checks are Python programs. Install Python 3.11 or
    echo   newer from python.org, or run Scripts\Setup.bat, which checks for it
    echo   and says what is missing.
    echo.
    exit /b 1
)

echo.
echo   ONE BRAHAM - engine-free checks
echo   ==============================
echo.

REM --- 1. The pipeline ------------------------------------------------------
set PYTHONPATH=%PY_ROOT%
!PYTHON! "%PY_ROOT%\onebraham\run_all.py" %PASSTHROUGH%
set RUN_ALL_CODE=!errorlevel!

REM --- 2. The validator -----------------------------------------------------
echo.
!PYTHON! "%PY_ROOT%\onebraham\validate_project.py" %CONFIG_ONLY%
set VALIDATE_CODE=!errorlevel!

echo.
if not "!RUN_ALL_CODE!"=="0" (
    echo   [OB] build checks FAILED ^(exit !RUN_ALL_CODE!^)
    exit /b !RUN_ALL_CODE!
)
if not "!VALIDATE_CODE!"=="0" (
    echo   [OB] validator FAILED ^(exit !VALIDATE_CODE!^)
    exit /b !VALIDATE_CODE!
)

echo   [OB] all engine-free checks passed.
endlocal
exit /b 0
