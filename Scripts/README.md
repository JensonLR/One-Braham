# ONE BRAHAM - Scripts

Every build, run and release action is a script in this folder. Nothing is done
by hand, and nothing is documented here that is not also implemented here.

## First run

| Script | What it does |
| --- | --- |
| `Setup.bat` | Checks for UE 5.5+, the Steam and Chaos Cloth plugins, Python and Steam. Says exactly what to install if anything is missing. |

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

## Package

| Script | What it does |
| --- | --- |
| `Package.bat Client` | Windows shipping client. The thing that goes on Steam. |
| `Package.bat Client Development` | Windows development client, for QA. |
| `Package.bat Server` | Windows dedicated server. |
| `Package.bat LinuxServer` | Linux dedicated server, for rented boxes. |
| `Package.sh Server` | The same, built on a Linux host. |

Packaging is `BuildCookRun` with `-cook -allmaps -stage -pak -iostore -prereqs
-archive`. The cook list is in `Config/DefaultGame.ini`; a map that is not in
that list does not ship.

## Run

| Script | What it does |
| --- | --- |
| `RunServer.bat` | Runs the packaged dedicated server: `-server -log -MaxPlayers=16`, ASCENT on the persistent map by default. |
| `RunClient.bat [address]` | Runs the packaged client, to the main menu or straight to a server. |
| `RunServer.sh` / `RunClient.sh` | The same on Linux. |

A dedicated server needs 7777/udp for game traffic and 7778/udp for voice.

## Test

| Script | What it does |
| --- | --- |
| `RunAll.bat` / `RunAll.sh` | Every check that needs no engine: the whole `run_all.py` pipeline plus the validator, with nothing but Python 3. Exit 0 only when nothing failed; with no editor installed the content steps report SKIPPED. |
| `RunTests.bat` | All `OneBraham.*` automation tests, headless with `-NullRHI`. |
| `RunTests.bat OneBraham.Weapons` | One category. Categories are listed in `Docs/18_Testing_Performance.md`. |
| `RunTests.bat smoke` | The smoke category on a dedicated server: the match-boot composition check. The bot-driven twelve-player run is in `Docs/18_Testing_Performance.md` §3. |
| `SmokePackaged.sh [--shots N] [--timeout S] [-- <game args>]` | macOS: runs the staged Mac Development/Test build with `-OBSmoke`, prints the `[OB.SMOKE]` checks and screenshot paths, and exits non-zero on FAIL, timeout, or no verdict. `OB_SMOKE_APP` overrides the binary. The game's own exit code is always 0 on Mac, so the PASS/FAIL line is the verdict. Details in `Docs/18_Testing_Performance.md` §3. |

## Release

| Script | What it does |
| --- | --- |
| `SteamUpload.bat <steamcmd> <content>` | Uploads the packaged build with SteamPipe. Never automatic. The checklist in `Docs/20_Release_Checklists.md` is signed off first. |
| `steam_app_build.vdf` | The SteamPipe script. Replace the placeholder AppId and depot ids before the first upload. |

## Environment variables

| Variable | Used by | Example |
| --- | --- | --- |
| `UE_EDITOR` | `BuildContent`, `Import_Font`, `RunTests` | `C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe` |
| `UE_UAT` | `Package.bat` | `C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\RunUAT.bat` |

Both fall back to the default Epic Games install path, so on a standard Windows
workstation neither needs setting.

## The order that matters

1. `Setup.bat`
2. `RunAll.bat` (or `RunAll.sh`) — the engine-free checks. On a fresh clone this
   is the whole pipeline minus the content steps, and it is what a CI box without
   an engine runs.
3. `BuildContent.bat` (the C++ must have compiled at least once so the reflected
   classes exist before the content scripts run)
4. `RunTests.bat`
5. `Package.bat Client` / `Package.bat LinuxServer`
6. `RunServer.bat`, then `RunClient.bat` to play it
7. `SteamUpload.bat` when the release checklist is signed off
