# ONE BRAHAM — project structure

What is in the repository, what it is for, and the conventions that keep it
navigable at 40,000 files.

---

## 1. Repository root

```
One-Braham/
├── OneBraham.uproject            the project descriptor (UE 5.5, 13 plugins)
├── README.md                     build-from-clone instructions
├── .gitignore                    UE conventions: no Binaries/, no Intermediate/, no DerivedDataCache/
├── Config/                       every ini, all hand-authored, all commented
│   ├── DefaultEngine.ini         renderer, collision, net, streaming, Steam
│   ├── DefaultGame.ini           modes, branding, tags, anti-cheat, cook list
│   ├── DefaultInput.ini          engine input + the generated [OneBraham.Input] block
│   ├── DefaultEditor.ini         PIE defaults, Python import paths
│   ├── DefaultGameUserSettings.ini
│   ├── DefaultScalability.ini    quality tiers 0–4
│   ├── DefaultDeviceProfiles.ini
│   └── {Windows,LinuxServer}/    platform overrides
├── Content/
│   ├── OneBraham/                everything this project authors (see §3)
│   └── ThirdParty/                created when marketplace content is imported; see Docs/16
├── Docs/                         this documentation set (21 documents)
├── Scripts/                      build, run, test, package, ship
├── Source/
│   ├── OneBraham/                the game module
│   ├── OneBraham.Target.cs
│   ├── OneBrahamEditor.Target.cs
│   └── OneBrahamServer.Target.cs
├── Tools/Python/                 the content pipeline: 12 builders, 17 checkers, a report
│                                 (`report_uncalled.py`) and 4 library modules
└── Saved/                        generated reports (not committed, except as build evidence)
```

Nothing in `Content/OneBraham` is hand-placed that a script can place. Nothing in
`Tools/Python` writes outside `Content/`, `Config/` and `Saved/`.

---

## 2. The C++ module

`Source/OneBraham` is thirteen systems plus the test suite, fourteen folders in
all. Each one is a system with a single owner:

| Folder | Contents | Belongs to |
| --- | --- | --- |
| `Core/` | Types, tags, settings, game instance, game state, player state, controller, game mode, spawn points, save/settings, the console and its QA commands, the `ob.NetDebug` overlay and the `ob.Bench.Skydeck` flythrough | PART 3 |
| `Characters/` | Character, movement, health, downed, ragdoll | PART 4 |
| `Weapons/` | Weapon base, weapon component, inventory, projectile | PART 5 |
| `Access/` | Access zone (the door), keycard, override terminal, fire shutter, breach interface | PART 6 |
| `Lockdown/` | the stage director and the purge | PART 6 |
| `Extraction/` | the Skydeck winch state machine | PART 6 |
| `Interaction/` | the interaction interface and the interact component | PART 3 |
| `Bots/` | the bot controller and behaviour | PART 3 |
| `Voice/` | types, line set, codec, subsystem, proximity component | PART 7 |
| `UI/` | HUD types, style, HUD component, HUD widget, crosshair, compass | PART 8 |
| `Audio/` | procedural audio, types, bank asset, audio manager component | PART 12 |
| `VFX/` | VFX statics, camera shake | PART 11 |
| `Data/` | weapon definition/registry, match settings, character data asset | PART 3 |
| `Online/` | the Steam subsystem: career record, achievements, stats, rich presence, overlay | PART 19 |
| `Tests/` | the automation suite, one file per system it covers | PART 18 |

Conventions that hold across all of it:

* **`OneBraham` prefix on every type.** No exceptions, no exceptions for
  interfaces, no exceptions for structs the engine generated.
* **Headers are self-contained.** Every header includes what it uses; the
  project never relies on `Engine.h` inclusion order.
* **Log categories, not `UE_LOG(LogTemp)`.** `OB_LOG`, `OB_WARN`, `OB_ERROR` and
  `OB_VERBOSE`, with the category named in `OneBraham.h`.
* **Replicated state is `UPROPERTY(ReplicatedUsing=OnRep_X)`.** No raw replicated
  members, no `DOREPLIFETIME` on a pointer the client should not own.
* **Timers replicate a start time, never a countdown.** See
  `Docs/07_Multiplayer_Voice.md` §5.
* **`bUseUnity = true`** for build speed, which means every header must compile
  alone. Non-unity builds are run monthly in CI to catch the leaks.

---

## 3. Content layout

```
Content/OneBraham/
├── Maps/          L_Persistent_OneBraham, F00–F15, the FIRE DRILL arena
├── Environment/   architecture, kit meshes, the ground floor
├── Materials/     M_OB_* masters, MI_* instances, PM_OB_* physical materials
├── Props/         props, loot cards, gameplay blueprints
├── Weapons/       weapon meshes, blueprints, DA_Weapon*, the registry
├── Characters/    bodies, grooms, clothing, ABP, DA_CharacterRegistry
├── Audio/         SC_*, SM_*, SA_*, A_*, M_*, DA_AudioBank
├── VFX/           NS_OB_*, BP_OBCameraShake_*, M_OB_Decal_*
├── UI/            WBP_*, DT_HUDStrings, F_OneBrahamSans, T_UI_*
├── Input/         IA_*, IMC_*, DA_InputConfig
├── Data/          match settings, character data assets
└── Voice/         Waves/<Archetype>/<LineId>.wav
```

Every folder has a `README.md` saying what belongs in it, which script fills it
and what still needs a human. That is the single most useful thing in the
repository for anybody joining the project.

---

## 4. Naming

| Prefix | Meaning | Example |
| --- | --- | --- |
| `L_` | level | `L_OneBraham_F07` |
| `SM_` / `SK_` | static / skeletal mesh | `SM_OB_Desk_Standard` |
| `M_` / `MI_` / `PM_` | master material / instance / physical | `MI_OB_Brand_PrimaryBlue` |
| `NS_` | Niagara system | `NS_OB_Impact_Concrete` |
| `BP_` / `WBP_` / `ABP_` | blueprint / widget / animation blueprint | `WBP_OneBrahamHUD` |
| `DA_` | data asset | `DA_WeaponRegistry` |
| `DT_` | data table | `DT_HUDStrings` |
| `IA_` / `IMC_` | input action / mapping context | `IA_Move` |
| `SC_` / `SM_` / `SA_` | sound class / submix / attenuation | `SA_OneBraham_WeaponClose` |
| `A_` / `M_` (in Audio) | ambient / music cue | `A_Lobby_Bed` |
| `T_` | texture | `T_UI_Icon_Keycard` |
| `F_` | font | `F_OneBrahamSans` |
| `V_` | voice line id | `V_CONTACT_ENEMY` |
| `OB.` | gameplay tag | `OB.Access.Tier.Staff` |

Gameplay tags are the one namespace that carries meaning across C++, Blueprint
and data: `OB.Access.Tier.*`, `OB.Weapon.*`, `OB.Character.Archetype.*`,
`OB.Area.*`, `OB.State.*`, `OB.Cue.*`. A tag is a stable interface; a renamed
asset is not.

---

## 5. Where a change goes

| I want to… | Edit |
| --- | --- |
| change a weapon's damage | `Tools/Python/onebraham/build_weapons.py`, then rebuild |
| change a surface's roughness | `build_environment.py` `SURFACES` |
| add a prop to a floor | `build_props.py`, then `--dress` |
| change the HUD | `Source/OneBraham/UI/OneBrahamHUDWidget.cpp` (or override in `WBP_OneBrahamHUD`) |
| add a voice line | `Source/OneBraham/Voice/OneBrahamVoiceLineSet.cpp`, then regenerate `Docs/14_Voice_Script.md` |
| change the mix | `Docs/12_Audio.md` first, then `build_audio.py` |
| add a doc reference | `Docs/`, and link it from `Docs/00_README.md` |

The rule underneath the table: **the document is the specification, the script is
the implementation, and the validator is the proof.** A change that skips the
script will be overwritten by the next build; a change that skips the validator
will not be noticed until it is a bug report.
