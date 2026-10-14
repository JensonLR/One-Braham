# ONE BRAHAM - Scripts

Every build, run and release action is a script in this folder. Nothing is done
by hand, and nothing is documented here that is not also implemented here.

The Windows scripts are PowerShell with a one-line `.bat` wrapper each, so a
command line exists exactly once. `OneBraham.psm1` is the shared core: it finds
the engine (`UE_ROOT`, then `UE_EDITOR`/`UE_UAT`, then the launcher's registry
entry, then `%ProgramFiles%\Epic Games\UE_5.x`, newest first), runs every tool
with its output logged to `Saved/Logs/Scripts/`, and stops on the first non-zero
exit code. Any script takes `-DryRun` (or `OB_DRYRUN=1` in the environment) to
print the exact tool command lines without running them.

`README_BUILD.md` and `README_RUN.md` at the repository root are the
walk-throughs; this file is the index.

## First run

| Script | What it does |
| --- | --- |
| `Setup.ps1` / `Setup.bat` | Finds UE 5.5+ and checks its version, checks every plugin `OneBraham.uproject` enables is in the engine, checks Python 3 and the Steam client, generates the IDE project files, then runs the engine-free checks. `-SkipProjectFiles`, `-SkipChecks`. |

## Compile

| Script | What it does |
| --- | --- |
| `BuildEditor.ps1` / `BuildEditor.bat` | Compiles with Unreal Build Tool. `-Target Editor` (default), `Game`, `Server` or `All`; `-Configuration Development` (default), `DebugGame`, `Shipping`; `-Clean`. Runs `check_cpp_symbols.py` and `check_uht_contracts.py` after a successful compile. |

## Content

| Script | What it does |
| --- | --- |
| `BuildContent.bat` | Runs `Tools/Python/onebraham/run_all.py` in the editor: materials, the map, props, weapons, the cast, audio, input, UI, VFX, then validation. Exit code 0 means every step passed. |
| `BuildContent.sh` | The same on a Linux host. |
| `Import_Font.bat` | Imports the UI font and points `F_OneBrahamSans` at it. |

`BuildContent` takes the same switches as `run_all.py`:

```
BuildContent.bat                          everything
BuildContent.bat --only weapons           just the weapons
BuildContent.bat --skip ui,vfx            everything except UI and VFX
BuildContent.bat --text-only              only the steps that need no editor
```

## Validate

| Script | What it does |
| --- | --- |
| `Validate.ps1` / `Validate.bat` | `-Check Project` the engine-free ladder and `validate_project.py`; `Maps` MAP CHECK on the persistent map; `References` the DataValidation commandlet over `/Game/OneBraham`; `Cook` a cook-only pass that fails on cook errors and missing references. Default `All`. Reports in `Saved/Validation/`. |

## Package

| Script | What it does |
| --- | --- |
| `PackageClient.ps1` / `PackageClient.bat` | The client: `BuildCookRun -build -cook -stage -pak -iostore -archive`. `-Configuration Shipping` (default) or `Development`; `-Platform Linux` for the Linux development client. Output `Build\WindowsClient\Windows\OneBraham.exe`. |
| `PackageServer.ps1` / `PackageServer.bat` | The dedicated server: the same, with `-server -noclient -serverplatform`. `-Platform Win64` (default) or `Linux` (needs `LINUX_MULTIARCH_ROOT` on a Windows host). Output `Build\WindowsServer\WindowsServer\OneBrahamServer.exe`, `Build/LinuxServer/LinuxServer/OneBrahamServer.sh`. |
| `Package.bat Client` / `Server` / `LinuxServer` `[Configuration]` | The short form: calls the two scripts above. |
| `Package.sh Server` / `Client` `[Configuration]` | Packages on a Linux host: the Linux server natively, or the Linux development client. |

Neither packaging script passes `-allmaps`: the cook list is `MapsToCook` in
`Config/DefaultGame.ini`, and a map that is not on it does not ship. Both check
that the executable exists before they report success.

## Run

| Script | What it does |
| --- | --- |
| `RunServer.ps1` / `RunServer.bat` | Runs the packaged dedicated server on `L_Persistent_OneBraham`, port 7777, `-server -log -MaxPlayers=16`. `-Map=` (a map URL, `?` options and all), `-Port=` and `-Mode=FireDrill` change it; anything else is passed through. |
| `RunClient.ps1` / `RunClient.bat [address or map URL]` | Runs the packaged client: the main menu, a server (`127.0.0.1`), or a listen host (`L_Persistent_OneBraham?listen`). |
| `RunServer.sh` / `RunClient.sh` | The same on Linux (`MAP=` in the environment picks the map). |

A dedicated server needs one UDP port open: 7777 by default, or the `-Port` you
give it. Proximity and team voice travel inside the game connection.

## Test

| Script | What it does |
| --- | --- |
| `RunAll.bat` / `RunAll.sh` | Every check that needs no engine: the whole `run_all.py` pipeline plus the validator, with nothing but Python 3. Exit 0 only when nothing failed; with no editor installed the content steps report SKIPPED. |
| `RunTests.bat` | All `OneBraham.*` automation tests, headless in the editor with `-NullRHI`. |
| `RunTests.bat OneBraham.Weapons` | One category. Categories are listed in `Docs/18_Testing_Performance.md`. |
| `RunTests.bat smoke` | `OneBraham.Multiplayer.Smoke`: the match-boot composition check. The bot-driven twelve-player run is in `Docs/18_Testing_Performance.md` §3. |

## Release

| Script | What it does |
| --- | --- |
| `SteamUpload.bat <steamcmd> <content>` | Uploads the packaged build with SteamPipe. Never automatic. The checklist in `Docs/20_Release_Checklists.md` is signed off first. |
| `steam_app_build.vdf` | The SteamPipe script. Replace the placeholder AppId and depot ids before the first upload. |

## Environment variables

| Variable | Used by | Example |
| --- | --- | --- |
| `UE_ROOT` | every `.ps1`, `Package.sh` | `C:\Program Files\Epic Games\UE_5.5` |
| `UE_EDITOR` | `BuildContent`, `Import_Font`, `RunTests`, and the `.ps1` scripts as a fallback | `...\Engine\Binaries\Win64\UnrealEditor-Cmd.exe` |
| `UE_UAT` | `Package.sh`, and the `.ps1` scripts as a fallback | `...\Engine\Build\BatchFiles\RunUAT.bat` |
| `LINUX_MULTIARCH_ROOT` | `PackageServer.ps1 -Platform Linux` on Windows | the cross-compile toolchain Epic lists for the engine version |
| `OB_DRYRUN` | every `.ps1` | `1` prints the commands instead of running them |

On a standard Windows workstation with the engine installed by the launcher,
none of them needs setting.

## The order that matters

1. `Setup.bat`
2. `BuildEditor.bat` — the reflected classes must exist before the content scripts run
3. `BuildContent.bat`
4. `Validate.bat`
5. `RunTests.bat`
6. `PackageClient.bat` and `PackageServer.bat` (or `Package.sh Server` on Linux)
7. `RunServer.bat`, then `RunClient.bat 127.0.0.1` to play it
8. `SteamUpload.bat` when the release checklist is signed off

`RunAll.bat` (or `RunAll.sh`) can run at any point: it is the engine-free ladder,
and it is what a CI box without an engine runs.
