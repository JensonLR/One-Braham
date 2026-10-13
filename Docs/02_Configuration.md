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
GameDefaultMap=/Game/OneBraham/Maps/L_Persistent_OneBraham.L_Persistent_OneBraham
EditorStartupMap=/Game/OneBraham/Maps/L_Persistent_OneBraham.L_Persistent_OneBraham
GlobalDefaultGameMode=/Script/OneBraham.OneBrahamGameMode
GameInstanceClass=/Script/OneBraham.OneBrahamGameInstance
```

If the game boots into an empty world, this is the block that broke.

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
MaxClientRate=96000
MaxInternetClientRate=96000
[/Script/Engine.NetDriver]
bReplicateUsingRegisteredSubObjectList=True
net.UseAdaptiveNetUpdateFrequency=1
```

### Steam

```ini
[/Script/OnlineSubsystemSteam.SteamNetDriver]
NetConnectionClassName=OnlineSubsystemSteam.SteamNetConnection
[/Script/Engine.GameEngine]
+NetDriverDefinitions=(...OnlineSubsystemSteam.SteamNetDriver...)
[OnlineSubsystemSteam]
bEnabled=True
SteamDevAppId=480          ; replaced by the real AppID at release, see Scripts/steam_app_build.vdf
```

`480` is Spacewar, the standard test AppID. It must be replaced before a public
build, and the release checklist says so twice because a shipped build pointing
at Spacewar is the mistake that cannot be undone.

### World Partition

On for the map, off for the FIRE DRILL arena (which fits in one cell and streams
worse as a partitioned level than as a plain one).

---

## 2. `DefaultGame.ini`

| Block | Contents |
| --- | --- |
| `[/Script/EngineSettings.GeneralProjectSettings]` | project name, `CompanyName`, `ProjectDisplayedTitle` = `ONE BRAHAM`, copyright line, project version 1.0.0 |
| `[/Script/UnrealEd.ProjectPackagingSettings]` | shipping, IoStore on, compression, cook lists (5 maps), no editor content, `bSkipEditorContent=True` |
| `[/Script/Engine.GameSession]` | `MaxPlayers=16`, `MaxSplitscreensPerConnection=1` |
| `[OneBraham.Modes]` | ASCENT / FIRE DRILL / NIGHT SHIFT defaults |
| `[OneBrahamAntiCheat]` | speed 1.15× / 0.4 s, fire-rate 1.20×, tolerance 12 cm, report-and-kick |
| `[/Script/GameplayTags.GameplayTagsSettings]` | ~105 `+GameplayTagList=(Tag="OB...` entries |
| `[/Script/OneBraham.OneBrahamGameSettings]` | the seven UI colours, interaction distance, default mode |

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
| `LinuxServer/LinuxServerEngine.ini` | `-nullrhi`, no audio device, World Partition on, `bUseFixedFrameRate=1` |

The Linux server config is the one that must never be forgotten: a server that
initialises an audio device is a server that leaks it over a 6-hour session.
