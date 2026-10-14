# ONE BRAHAM - building it

From a fresh clone to a packaged client and dedicated server. Every step is a
script; each one stops on the first failure and logs to `Saved/Logs/Scripts/`.
`Scripts/README.md` lists every switch.

> **State of the tree.** The C++ has not yet been compiled by an Unreal Engine
> toolchain, and the content pipeline has not yet run inside an editor. The
> engine-free checks (step 0) pass on every commit. Expect step 2 to produce a
> list of compile errors the first time; `HANDOVER.md` §4.1 is the plan for
> working it.

## What you need

| | |
| --- | --- |
| Windows 10/11 x64 | the client is Windows-first; the server also builds for Linux. A Mac can develop the game but not package it (see Working on a Mac) |
| Unreal Engine 5.5 or newer | the Epic Games Launcher install builds the editor, the client and the content. **The dedicated server (step 7) needs a source build** of the engine from Epic's GitHub ([Epic: dedicated servers](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine)), plus the Linux cross-compile toolchain for a Linux server |
| Visual Studio 2022 | the "Game development with C++" workload, and the .NET desktop components UBT uses |
| Python 3.9+ | on `PATH`, for the engine-free checks |
| Steam client | running, for the overlay and invites in development |
| PowerShell | Windows PowerShell 5.1 (built in) or PowerShell 7 |

## The sequence

```
0.  Scripts\RunAll.bat                 the engine-free checks: Python only, seconds
1.  Scripts\Setup.bat                  engine, plugins, toolchain; generates project files
2.  Scripts\BuildEditor.bat            compiles OneBrahamEditor (Development, Win64)
3.  Scripts\BuildContent.bat           builds every generated asset and the map, in the editor
4.  Scripts\Validate.bat               project, MAP CHECK, data validation, cook
5.  Scripts\RunTests.bat               the automation suite, headless
6.  Scripts\PackageClient.bat          Build\WindowsClient\Windows\OneBraham.exe
7.  Scripts\PackageServer.bat          Build\WindowsServer\WindowsServer\OneBrahamServer.exe
```

Each step proves something the next one relies on:

| Step | Proves | If it fails |
| --- | --- | --- |
| 0 | the documents, the generators and the C++ agree: declarations have definitions, reflection contracts hold, every documented number is the real one | the checker names the file and the line; fix the tree before anything else |
| 1 | the engine is 5.5+, and every plugin the project enables is installed | install what it names from the launcher or Fab |
| 2 | the module compiles | work the compiler's list top-down, re-run step 0 after each batch |
| 3 | every builder ran: materials, map, props, weapons, cast, audio, input, UI, VFX | the report in `Saved/Build/` names the failed asset |
| 4 | the map is sound, no asset references something missing, the content cooks | `Saved/Validation/` has one report per check |
| 5 | the match rules, weapons, damage, voice, input and replication contracts hold at runtime | the automation log names the test |
| 6–7 | the shipping builds exist where the run scripts look for them | the archive step in the BuildCookRun log |

## Working on a Mac

Step by step, from an empty Mac to playing in the editor: `Docs/22_First_Day_Mac.md`.

A Mac can run the editor, compile the game module, build the content and play
in the editor. It cannot package the shipped builds: the client is packaged on
Windows and the server on Windows or Linux. Requirements for UE 5.5, from
Epic's [macOS development requirements](https://dev.epicgames.com/documentation/en-us/unreal-engine/macos-development-requirements-for-unreal-engine):

| | |
| --- | --- |
| macOS | Ventura 13.5 or newer |
| Xcode | 15.2 or newer, 15.4+ recommended. Open it once after installing, and accept the licence |
| Hardware | Apple Silicon. 16 GB is the minimum and 32 GB is recommended. Nanite and virtual shadow maps need an M2 or newer; on an M1 the engine draws the fallback meshes |
| Python 3 | for step 0 (`python3` from Xcode's command line tools is enough) |

```
0.  Scripts/RunAll.sh                   the engine-free checks
2.  Scripts/BuildEditor.sh --generate   Xcode project, then compile OneBrahamEditor (Mac, Development)
3.  Scripts/BuildContent.sh             every generated asset and the map, in the editor
```

Both scripts find a Launcher install at `/Users/Shared/Epic Games/UE_5.5`.
For anything else, set `UE_ROOT`. Then open `OneBraham.uproject` and play in
the editor. Steam works on a Mac with the Steam client running. Without it, online
services are off and practice and LAN games still work.

## Other targets

```
Scripts\BuildEditor.bat -Target All                       editor, game and server
Scripts\BuildEditor.bat -Target Server -Configuration Shipping
Scripts\PackageClient.bat -Configuration Development      a QA client with the console
Scripts\PackageServer.bat -Platform Linux                 Linux server, cross-compiled
                                                          (needs LINUX_MULTIARCH_ROOT)
Scripts/Package.sh Server                                 Linux server, on a Linux host
```

## Seeing a build without running it

Every PowerShell script accepts `-DryRun` and prints the exact Unreal Build Tool,
UAT and editor command lines it would run. That is the quickest way to answer
"what does packaging actually do":

```
Scripts\PackageServer.bat -DryRun
```

## Content and the cook list

No `.uasset` is committed: `Content/OneBraham/` is generated by
`Tools/Python/onebraham/build_*.py` inside the editor (step 3). The maps that ship
are `MapsToCook` in `Config/DefaultGame.ini`; packaging never uses `-allmaps`.

## Continuous integration

A machine with no engine runs step 0 (`Scripts/RunAll.sh` on Linux). A build agent
with the engine runs steps 1–7 in order, each as its own stage, and archives
`Build/` and `Saved/Logs/Scripts/`. `Docs/17_Build_Automation.md` has the
pipeline table and what each generator writes.
