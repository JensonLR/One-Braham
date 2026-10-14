# ONE BRAHAM — configuration

Every ini in the project, what is in it, and which line matters if something is
wrong. These files are hand-authored, commented at the point of change, and they
are the single source of truth for anything the game reads at startup.

Location: `Config/`. UE reads them in this order, later winning:

```
DefaultEngine.ini → <Platform>/<Platform>Engine.ini → DefaultGame.ini → DefaultInput.ini
```

---

## 1. `DefaultEngine.ini`

### Maps and classes

```ini
[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Game/OneBraham/Maps/L_MainMenu.L_MainMenu
EditorStartupMap=/Game/OneBraham/Maps/L_Persistent_OneBraham.L_Persistent_OneBraham
ServerDefaultMap=/Game/OneBraham/Maps/L_Persistent_OneBraham.L_Persistent_OneBraham
GlobalDefaultGameMode=/Script/OneBraham.OneBrahamGameMode
GameInstanceClass=/Script/OneBraham.OneBrahamGameInstance
TransitionMap=/Game/OneBraham/Maps/L_Loading.L_Loading
```

A client starts in the main menu; a dedicated server starts in the match map;
the editor opens the building. `L_MainMenu` names its own game mode
(`AOneBrahamMenuGameMode`) in its world settings, and so does `L_Loading` (the
engine's base game mode), so the global default is the match's.

If the game boots into an empty world, this is the block that broke.

### Cook list (`DefaultGame.ini`)

`+MapsToCook` lists the four maps `build_environment.py` makes. Every line needs
its `+`. A plain `MapsToCook=` line replaces the value before it, and that is how
this list once cooked only its last entry.

### Renderer

| Setting | Value | Why |
| --- | --- | --- |
| `r.DefaultFeature.AntiAliasing` | 4 (TSR) | TSR over TAA: better at 1080p with the metal and glass in this game |
| `r.DynamicGlobalIlluminationMethod` | 1 (Lumen) | |
| `r.ReflectionMethod` | 1 (Lumen) | |
| `r.Lumen.HardwareRayTracing` | 0 | the game must look like itself on a GTX 1660 |
| `r.Shadow.Virtual.Enable` | 1 | VSM at 90 m cascades |
| `r.Nanite` | 1 | architecture only |
| `r.SkinCache.CompileShaders` | 1 | six archetypes with cloth |
| `r.GPUSkin.Support16BitBoneIndex` | 1 | |

### Collision

Fifteen physical surfaces, all named:

```ini
[/Script/Engine.PhysicsSettings]
+PhysicalSurfaces=(Type=SurfaceType1,Name="Marble")
+PhysicalSurfaces=(Type=SurfaceType2,Name="Carpet")
...
```

Ten custom trace channels and the collision profiles that use them:

| Channel | Used by |
| --- | --- |
| `ECC_GameTraceChannel1` → `EC_OneBrahamWeapon` | bullet traces |
| `2` → `EC_OneBrahamBreachable` | breach damage |
| `3` → `EC_OneBrahamInteraction` | interaction sphere traces |
| `4` → `EC_OneBrahamVision` | bot line of sight |
| `5` → `EC_OneBrahamHearing` | sound occlusion propagation |
| `6` → `EC_OneBrahamSoundOcclusion` | footsteps and gunfire through floors |
| `7` → `EC_OneBrahamThrowable` | utility arcs |
| `8` → `EC_OneBrahamCover` | cover queries |
| `9` → `EC_OneBrahamNavModifier` | door and shutter nav |
| `10` → `EC_OneBrahamCamera` | the extraction cut and death camera |

Profiles: `OB_Weapon`, `OB_Breachable`, `OB_Interactable`, `OB_PlayerCapsule`,
`OB_Cover`, `OB_Projectile`, `OB_Shutter`, `OB_PostProcess`.

**If bullets pass through a wall, the wall's profile name was mistyped.** That is
the only way this can fail, and it fails silently.

### Networking

```ini
[/Script/OnlineSubsystemUtils.IpNetDriver]
NetServerMaxTickRate=60
MaxClientRate=100000
MaxInternetClientRate=100000
[ConsoleVariables]
net.UseAdaptiveNetUpdateFrequency=1
```

### Steam

```ini
[/Script/OnlineSubsystemSteam.SteamNetDriver]
NetConnectionClassName="OnlineSubsystemSteam.SteamNetConnection"
[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="OnlineSubsystemSteam.SteamNetDriver",DriverClassNameFallback="OnlineSubsystemUtils.IpNetDriver")
+NetDriverDefinitions=(DefName="DemoNetDriver",DriverClassName="/Script/Engine.DemoNetDriver",DriverClassNameFallback="/Script/Engine.DemoNetDriver")
[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480          ; replaced by the real AppID at release, see Scripts/steam_app_build.vdf
```

The game driver is Steam's, falling back to IP when Steam is not running. A
Steam lobby resolves to a `steam.<id>` P2P address that only the Steam driver can
reach, so without these rows every lobby join fails. `ClearArray` comes first
because the engine's own `GameNetDriver` row would otherwise be matched before
this one. `validate_project.py` fails the build if any of this is missing.
`Achievement_N_Id` lists every achievement; see `Docs/19`.

`480` is Spacewar, the standard test AppID. It must be replaced before a public
build, and the release checklist says so twice because a shipped build pointing
at Spacewar is the mistake that cannot be undone.

### Levels, not World Partition

Off everywhere. `L_Persistent_OneBraham` holds the shell and every gameplay
anchor, and each floor is an always-loaded sublevel. World Partition streams by a
2D grid in XY; sixteen storeys stacked on one 52 × 44 m footprint all fall in the
same cells, so a partitioned tower could only load all of itself or none of it,
and a partitioned world cannot hold the per-floor sublevels the builder makes.
The FIRE DRILL arena is a single plain level.

---

## 1b. Project Settings -> One Braham

`UOneBrahamGameSettings` is a `UDeveloperSettings` class, so everything in it is
editable in Project Settings and stored in `DefaultEngine.ini` (the class is
`config = Engine`) under `[/Script/OneBraham.OneBrahamGameSettings]`. It carries the brand colours, the
match and security tuning, and the content registries.

**The content registries resolve in three steps.** Every one of them, in this
order:

| Step | Source | When it is right |
| --- | --- | --- |
| 1 | the settings field (`WeaponRegistry`, `VoiceLineRegistry`, `CharacterRegistry`, `CommonMappingContext`, `DefaultMappingContext`, `DefaultMatchSettings`) | you are pointing the game at content somewhere else, or testing one asset |
| 2 | the conventional path (`/Game/OneBraham/Data/DA_WeaponRegistry`, `/Game/OneBraham/Voice/DA_VoiceLines`, `DA_MatchSettings_<Mode>`) | the normal case: the pipeline writes there |
| 3 | a compiled-in or runtime default with a warning in the log | a code-only checkout with no content built yet |

The two mapping-context fields are the exception to step 2: there is no
conventional path, because the generated `IMC_` assets carry no keys. Keys come
from `[OneBraham.Input]` through the runtime bindings context the controller
builds (`Docs/03` section 7), which is also step 3 for input.

Step 3 is why the game is playable before anybody runs the content pipeline; step
1 is why a designer never has to touch C++ to move an asset. Both existed here,
and four of the six fields were read by nobody: `WeaponRegistry`,
`VoiceLineRegistry`, `DefaultMatchSettings` and both mapping contexts were
editable and inert, and the code loaded its content from the hard-coded path
instead. `check_uht_contracts.py` now fails the build on a settings field that no
source file reads, which is the only way to keep that from happening again: a
setting that does nothing looks exactly like a setting that works.

## 2. `DefaultGame.ini`

| Block | Contents |
| --- | --- |
| `[/Script/EngineSettings.GeneralProjectSettings]` | project name, `CompanyName`, `ProjectDisplayedTitle` = `ONE BRAHAM`, copyright line, project version 1.0.0 |
| `[/Script/UnrealEd.ProjectPackagingSettings]` | shipping, IoStore on, compression, cook lists (5 maps), no editor content, `bSkipEditorContent=True` |
| `[/Script/Engine.GameSession]` | `MaxPlayers=16`, `MaxSplitscreensPerConnection=1` |
| `[/Script/GameplayTags.GameplayTagsSettings]` | ~105 `+GameplayTagList=(Tag="OB...` entries |

There is no project section in this file, deliberately. Match rules are per-mode
`UOneBrahamMatchSettingsAsset`s that fall back to their C++ defaults. The project
settings (colours, anti-cheat thresholds, registries) are `config = Engine`, so they
live in `DefaultEngine.ini`. An earlier version of this file carried a 31-line
`[/Script/OneBraham.OneBrahamMatchSettings]` section for a class that does not
exist, and the anti-cheat thresholds in a file their class never reads. The engine
ignored both without a warning. `validate_project.py` now fails the build if any
key under `[/Script/OneBraham.*]`:

- names no config property of that class;
- sits in a file the class does not read; or
- gives an enum a value that is not one of its names.

The gameplay tags in this file are **declarative and required**: a tag used in
C++ that is not declared here fails at cook time on a shipping build and works
fine in the editor. That asymmetry has cost every UE team a release; the project
declares every tag explicitly and `validate_project.py` checks the count.

---

## 3. `DefaultInput.ini`

Two halves:

1. **Engine sections** — the legacy axes and the `[/Script/Engine.InputSettings]`
   block: `bUseMouseForTouch=False`, `DefaultViewportMouseCaptureMode`,
   `bEnableMouseSmoothing=False` (a shooter with mouse smoothing on is a shooter
   that feels wrong and a support ticket that never stops).
2. **`[OneBraham.Input]`** — generated by `Tools/Python/onebraham/generate_input_ini.py`
   between `; BEGIN GENERATED` and `; END GENERATED` markers. 41 actions, 72
   bindings. Never edit inside the markers: the next run overwrites it.

```ini
; BEGIN GENERATED - do not edit, generated by generate_input_ini.py
+ActionMappings=(ActionName="IA_Fire",Key=LeftMouseButton,bShift=False,...)
...
; END GENERATED
```

The generator reads the action list from `build_input.py`, which reads the
bindable-field names from `UOneBrahamGameSettings`, so a new input in C++ appears
in the ini after one build step and cannot be forgotten.

---

## 4. `DefaultEditor.ini`

```ini
[/Script/UnrealEd.LevelEditorPlaySettings]
PlayNetMode=PIE_Client
PlayNumberOfClients=3
RunUnderOneProcess=True
[/Script/PythonScriptPlugin.PythonScriptPluginSettings]
+AdditionalPaths=(Path="Tools/Python")
```

The Python plugin itself is enabled in `OneBraham.uproject` (`PythonScriptPlugin`,
editor only). Epic's docs say it must be enabled per project, and
`-run=pythonscript`, which every content builder runs through, does nothing
without it.

`RunUnderOneProcess=True` matters: three clients in one process is the only way
to iterate on the door and shutter logic without a second machine, and it makes
the voice loopback CVar (`ob.VoiceLoopback 1`) usable.

---

## 5. Scalability

`DefaultScalability.ini` defines tiers 0–4 with the game's own overrides rather
than the engine defaults, because the engine's "High" is a general-purpose High
and this game's expensive items are specific: Lumen reflections, virtual shadow
maps, cloth, and the Skydeck's floodlight shadows.

| Tier | Name | Lumen | VSM | Cloth | Nanite |
| --- | --- | --- | --- | --- | --- |
| 0 | Low | off (DFAO) | off | off | on |
| 1 | Medium | GI only | on, 45 m | 4 assets | on |
| 2 | High (default) | GI + reflections | 90 m | 8 assets | on |
| 3 | Epic | GI + reflections, high quality | 120 m | 8 assets | on |
| 4 | Ultra | as Epic, 1440p target | 150 m | 8 assets | on |

---

## 6. Platform overrides

| File | Overrides |
| --- | --- |
| `Windows/WindowsEngine.ini` | DX12, SM6, hardware GPU scheduling, `r.GTSyncType=2` |
| `LinuxServer/LinuxServerEngine.ini` | the headless renderer and audio settings, a 60 Hz net tick, Steam server ports |

The Linux server config is the one that must never be forgotten: a server that
initialises an audio device is a server that leaks it over a 6-hour session.
