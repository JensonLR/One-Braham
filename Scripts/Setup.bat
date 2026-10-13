@echo off
REM ===========================================================================
REM  ONE BRAHAM - first-run setup
REM ---------------------------------------------------------------------------
REM  Checks that everything the project needs is where it should be, and says
REM  exactly what to install if it is not. Run this before the first build.
REM
REM  What it looks for:
REM    1. Unreal Engine 5.5 or newer
REM    2. The project's plugins (they ship with the engine; the list is in
REM       OneBraham.uproject - this checks the two that are not always installed:
REM       OnlineSubsystemSteam and ChaosClothPlugin)
REM    3. Python 3 for the text-only tooling
REM    4. A Steam client, for the overlay during development
REM ===========================================================================

setlocal enabledelayedexpansion
set PROBLEMS=0

echo.
echo   ONE BRAHAM - environment check
echo   ==============================
echo.

REM --- 1. Engine -------------------------------------------------------------
set ENGINE_FOUND=0
for %%V in (5.5 5.6 5.7) do (
    if exist "%ProgramFiles%\Epic Games\UE_%%V\Engine\Binaries\Win64\UnrealEditor.exe" (
        echo   [OK]   Unreal Engine %%V found
        set ENGINE_FOUND=1
    )
)
if "%ENGINE_FOUND%"=="0" (
    echo   [FAIL] Unreal Engine 5.5+ not found in the default location.
    echo          Install it from the Epic Games Launcher, or set UE_EDITOR.
    set /a PROBLEMS+=1
)

REM --- 2. Plugins ------------------------------------------------------------
if exist "%ProgramFiles%\Epic Games\UE_5.5\Engine\Plugins\Runtime\Online\OnlineSubsystemSteam" (
    echo   [OK]   OnlineSubsystemSteam present
) else (
    echo   [WARN] OnlineSubsystemSteam not in the default engine path.
    echo          Install it from the Marketplace ^(it is free^) or the engine's
    echo          optional components.
)

if exist "%ProgramFiles%\Epic Games\UE_5.5\Engine\Plugins\Experimental\ChaosClothAsset" (
    echo   [OK]   Chaos Cloth present
) else (
    echo   [WARN] ChaosClothPlugin not found. Cloth simulation on the archetype
    echo          clothing will fall back to the body mesh.
)

REM --- 3. Python -------------------------------------------------------------
python --version >nul 2>&1
if errorlevel 1 (
    echo   [WARN] python is not on PATH. The text-only tools ^(input ini
    echo          generation^) will not run; the editor-side Python is unaffected.
) else (
    for /f "tokens=*" %%v in ('python --version 2^>^&1') do echo   [OK]   %%v
)

REM --- 4. Steam --------------------------------------------------------------
if exist "%ProgramFiles(x86)%\Steam\steam.exe" (
    echo   [OK]   Steam client found
) else (
    echo   [WARN] Steam client not found in the default location. The overlay and
    echo          the listen-server invite flow need it; a dedicated server does not.
)

echo.
if "%PROBLEMS%"=="0" (
    echo   Setup looks good. Next:
    echo     Scripts\BuildContent.bat      build the content
    echo     Scripts\Package.bat Client     package the client
    echo     Scripts\RunServer.bat          run a dedicated server
) else (
    echo   %PROBLEMS% blocking problem^(s^). Fix them, then run Setup.bat again.
)
echo.

endlocal
