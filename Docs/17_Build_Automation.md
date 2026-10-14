# ONE BRAHAM — build automation

Every asset and every configuration file in this project is produced by a script.
This document is the map of those scripts: what each one builds, in what order,
what it needs, and what it does when a piece of content is not there yet.

**The rule the whole pipeline is built on:** a build must never stop because a
piece of art does not exist. It reports it, replaces it with something that works,
and keeps going — and then the validator fails the *release* build if the
placeholder is still in use. That is the difference between a pipeline a studio
can work in and a pipeline that gets bypassed by the first artist in a hurry.

---

## 1. The scripts

| Script | Builds | Needs the editor | Idempotent |
| --- | --- | --- | --- |
| `generate_input_ini.py` | `Config/DefaultInput.ini` `[OneBraham.Input]`, 42 actions, 68 bindings | no | yes |
| `build_materials.py` | 15 physical materials, 6 masters (node graphs, one of them the post-process interaction outline), 28 instances | yes | yes |
| `build_environment.py` | the map: 8 areas, 16 levels, 40 rooms, 10 access zones, 5 terminals, 4 breach walls, 28 shutters | yes | yes |
| `build_props.py` | 12 gameplay blueprints, 4 loot cards, the prop manifest, the dressing pass | yes | yes |
| `build_weapons.py` | 10 weapon definitions, 10 blueprints, `DA_WeaponRegistry` | yes | yes |
| `build_characters.py` | the cast payload + `ABP_OneBrahamCharacter` when the skeleton exists | yes | yes |
| `apply_cast_payload.py` | writes the payload onto `DA_CharacterRegistry`, verifies 6 archetypes | yes | yes |
| `build_audio.py` | 12 sound classes, 3 submixes, 4 mixes, 8 attenuations, 11 ambience, 8 music, `DA_AudioBank` | yes | yes |
| `build_input.py` | 42 input actions, 4 mapping contexts, `DA_InputConfig` | yes | yes |
| `build_ui.py` | 7 widget blueprints, the HUD string table, the font slot, the palette check | yes | yes |
| `build_vfx.py` | 18 Niagara systems, 3 camera shakes, 6 decal materials, the authoring sheet | yes | yes |
| `import_font.py` | font faces from `Content/UI/Fonts` | yes | yes |
| `check_cpp_symbols.py` | reads `Source/OneBraham` and reports every member function declared with no definition; also a scoped enum compared with `>=`/`<`/`|`, a member whose name is also an engine property of an ancestor class, a read of an engine member UE5 made private (`APlayerState::PlayerId`, `AActor::bReplicates`...), a parameter or local named like a member of its own class, audio played inside a loop over the player controllers, `IsValid()` asked of a `TSoftObjectPtr`/`TSoftClassPtr` (true only once the asset is in memory, so it never gates a `LoadSynchronous` from a cold start: `IsNull()` is the test), the same function defined twice, and an integer literal with a float suffix (`0f` is not C++; `0.f` is) | **no** | yes |
| `check_uht_contracts.py` | UE reflection contracts: RPC bodies, RepNotify callbacks and their previous-value parameter type, replicated TMaps and structs that carry one, replicated properties named in a `DOREPLIFETIME`, widgets that animate in a `NativeTick` the engine never calls, duplicate type names, `TSoftObjectPtr`s that name a type existing nowhere in the module, and settings fields that no code reads | **no** | yes |
| `check_soft_paths.py` | every `/Game/` path the C++ loads by name, against the directories and name prefixes the pipeline writes | **no** | yes |
| `check_calls.py` | call sites whose argument count cannot match the project's own declaration (defaults honoured, qualified calls skipped) | **no** | yes |
| `check_gameplay_tags.py` | every tag the code requests, declared natively or in `DefaultGame.ini`; resolves `Printf`-assembled tags from local string tables; and every tag the documentation names, the 147-entry registry block in Docs/03 included | **no** | yes |
| `check_building_layout.py` | the floor/area table the map is built from against the floor table the game describes (`DA_BuildingLayout` / its compiled-in default) | **no** | yes |
| `check_input_actions.py` | every generated `IA_` action against the `UOneBrahamInputConfig` field that points at it, every field against its action, every `DefaultInput.ini` binding and prompt against a real action, every `InputConfig->Field` read against the fields that exist, no key bound to two verbs on one device outside the declared shared groups, every binding modifier one the controller builds, and no prompt that names a key instead of `{Interact}` | **no** | yes |
| `check_doc_numbers.py` | every count quoted in this document's build report and in the README against the count the tooling produces, and the Docs/13 archetype table against `build_characters.py` field by field | **no** | yes |
| `check_test_names.py` | every automation test `Docs/18` lists against the tests that exist in `Source/OneBraham/Tests`, every test that exists against the table, and every `OneBraham.*` name in `Scripts/` against a test or a category | **no** | yes |
| `check_doc_files.py` | every `Scripts/` file and `Tools/Python/` path the documentation names against the files on disk, every script on disk against the script index, and every tooling module against this document's pipeline table | **no** | yes |
| `check_voice_lines.py` | every `V_` line id the code asks for against the script table, every scripted line against the recording brief in `Docs/14`, and the sixty-six archetype variant rows against the six voices they claim | **no** | yes |
| `check_doc_types.py` | every `[AUIFSE]OneBraham` type name in the README and in the docs against the types this module declares; the names that are deliberately not code (the example class in Docs/18) are listed in the checker with a reason | **no** | yes |
| `check_weapon_ids.py` | the ten weapons as four places describe them: the armoury table in `build_weapons.py` that generates the definition assets, the `WeaponSpawnTable` default in `OneBrahamMatchSettingsAsset.cpp` that the floor loot rolls, every weapon id literal anywhere in `Source/`, and the two tables in `Docs/05` - ids, order, damage, pellets, RPM, magazine, reserve, loot weights and floor gates | **no** | yes |
| `check_audio_ids.py` | the cue list: the bank's rows, what `OneBrahamProceduralAudio` can synthesise (recipes, branches, beds, UI substrings), the audio builder's `AMBIENCE`/`MUSIC` beds, the zone table against the building layout's area ids, the music cue table against `EOneBrahamMusicState`, and the briefs in `Docs/12` - every cue either audible or written down | **no** | yes |
| `check_hud_strings.py` | the HUD's copy: the `HUD_STRINGS` list `build_ui.py` generates `DT_HUDStrings` from, the rows compiled into `OneBrahamHUDStrings`, the keys the code asks the table for, and the ownership table in `Docs/08` that says which layer shows each one | **no** | yes |
| `check_cosmetic_ids.py` | the wardrobe: `COSMETIC_SLOTS` / `COSMETIC_OPTIONS` in `build_characters.py`, the compiled-in `CosmeticSlots` default (names, parent bones, offsets), the starter outfit in `UOneBrahamGameInstance`, and the slot table in `Docs/13` - names and option counts ; the earned-cosmetic catalogue: every locked id is a wardrobe option that no starter outfit or archetype default issues, every tag is registered, and every tag an achievement grants has a row | **no** | yes |
| `check_interaction.py` | the interaction surface: every class that implements `IOneBrahamInteractionInterface` against the table in `Docs/13` section 9, every number in that table against the constant or property default it names, every `OB.Interaction.*` tag either claimed by an emitter or written down as declared-and-waiting, and every verb against the montage `PlayInteractionMontage` would load for it | **no** | yes |
| `report_uncalled.py` | **a report, not a gate**: every member function declared, defined and never called anywhere in the module, tagged `[BP]` when it exists for a Blueprint to call and `[net]` when it is an RPC. Run by hand before and after a wiring batch; `HANDOVER.md` section 4.2 is the triage of the current list. It exits 0 whatever it finds, because "this has no caller" is a question rather than a failure | **no** | by hand |
| `validate_project.py` | the health check and a markdown report | either | yes |
| `run_all.py` | all of the above, in dependency order, with a report | either | yes |

Every script is runnable on its own. Every script imports cleanly **outside** the
editor (the `unreal` import is guarded), which is what lets `--text-only` steps
and the config checks run in CI with nothing but Python 3.

`common.py` holds the parts that must not exist twice: the class-body parser, the
naming contract every builder writes against, and the comment stripper the
checkers share. The stripper replaced six copies of a four-regex version that
removed block comments before line comments - so a `/*` inside a `//` comment or
inside a string literal (the repository's own scripts are described as
`Scripts/*.bat`) opened a block comment that deleted real code from the analysis,
and the first check to notice was the one counting braces. It is one pass, left to
right, in two flavours: `strip_comments` for the checkers that read string
literals, `strip_comments_and_strings` for the checkers that read code structure.
Giving the second flavour to a checker in the first group makes it inspect
nothing, so both are named for what they do and neither is the default.

---

## 2. Running it

```
Scripts\BuildContent.bat                          everything
Scripts\BuildContent.bat --only weapons           one step
Scripts\BuildContent.bat --skip ui,vfx            everything except two
Scripts\BuildContent.bat --text-only              the steps that need no editor
Scripts\BuildContent.bat --only environment,props --dress    rebuild and dress the map
```

`run_all.py` accepts the same switches and writes
`Saved/Build/ob_build_<timestamp>.md`:

```
# ONE BRAHAM - content build report
**29 steps succeeded, 0 failed.**

| Step | Result | Seconds | Detail |
| --- | --- | --- | --- |
| input_ini | OK | 0.0 | wrote 42 actions (41 with a default key) |
| cpp_symbols | OK | 0.7 | 817 declarations, all defined; compile hygiene clean |
| uht_contracts | OK | 1.5 | 953 UE reflection contracts, all satisfied |
| soft_paths | OK | 0.0 | 41 soft paths, all conventional |
| calls | OK | 0.3 | 1341 call sites, all arities plausible |
| tags | OK | 0.4 | 0 resolvable tag requests and 147 documented tags, all declared |
| layout | OK | 0.0 | 16 floors, map and layout agree |
| voice_lines | OK | 0.0 | 118 line references, all declared and documented |
| input_actions | OK | 0.0 | 42 actions, 42 fields, 96 reads |
| doc_numbers | OK | 2.2 | 84 documented numbers, all current |
| test_names | OK | 0.0 | 28 automation tests, every one documented and resolvable |
| doc_files | OK | 0.1 | 72 documented paths, all on disk |
| doc_types | OK | 0.1 | 61 documented type names, all declared |
| weapon_ids | OK | 0.1 | 122 weapon-id checks, the armoury and the spawn table agree |
| cosmetic_ids | OK | 0.1 | 149 cosmetic checks, one wardrobe |
| hud_strings | OK | 0.1 | 98 HUD-string checks, 31 rows and 7 widgets, every one shown |
| audio_ids | OK | 0.2 | 308 audio-id checks, every cue audible or documented |
| interaction_ids | OK | 0.3 | 281 interaction checks, 10 interactables, every verb, tag, tuning number and override accounted for |
| materials | OK | 3.1 | 6 masters, 15 physicals, 28 instances |
...
```

Exit code 0 only if no step failed. CI reads the exit code, not the log.

A step that cannot run because there is no Unreal Editor on the machine is
reported as **SKIPPED** and counted separately, not as a failure: the content
builders raise `"... must run inside the Unreal Editor."`, which is a fact about
the machine rather than about the project, and a CI box with no engine install
should not report a broken build - it should report that it did not run the
content steps, with their names. On a fresh clone the full run is therefore
"19 steps succeeded, 0 failed, 10 skipped" and the same run with an editor
installed is "29 steps succeeded, 0 failed".

---

## 3. The order, and why

```
generate_input_ini        text only; can fail before the editor opens
cpp_symbols               text only; a link error found in 0.6 s, not 4 minutes, and the two faults that are not errors at all: a scoped enum compared with an operator it does
                          not have (refuses to compile), and a member that shadows an engine property (compiles, and the value you set is not the one the engine uses)
uht_contracts             text only; a property that silently never replicates, a tick a widget never has
soft_paths                text only; an asset path that silently loads nothing
calls                     text only; a call that cannot compile: wrong argument count
tags                      text only; a tag that silently matches nothing
layout                    text only; a floor plan that names the wrong floor
voice_lines               text only; a character who says nothing, silently
input_actions             text only; a key that does nothing, and a field that is not there
doc_numbers               text only; a count that reads as verification and is wrong
test_names                text only; a suite the documentation lists and nobody wrote
doc_files                 text only; a script the documentation tells you to run that does not exist
doc_types                 text only; a type name in the manual that the code never declares
weapon_ids                text only; a loot table rolling a weapon that does not exist
cosmetic_ids              text only; a wardrobe id that matches nothing, silently
hud_strings               text only; a HUD table with no rows and nothing reading it
audio_ids                 text only; a cue that ships silent, or a brief for a cue that does not exist
      ↓
materials                 everything references MI_* and PM_OB_*
      ↓
environment               the map: floors, rooms, doors, shutters, the pad
      ↓
props                     blueprints + the dressing manifest
      ↓
weapons                   data assets → blueprints → registry
      ↓
characters                the cast payload
      ↓
apply_cast_payload        payload → DA_CharacterRegistry
      ↓
audio                     classes, mixes, attenuations, the bank
      ↓
input                     IA_* + DA_InputConfig
      ↓
ui                        widgets, strings, font slot
      ↓
vfx                       systems, shakes, decals
      ↓
validate                  the health check, on everything above
```

Two orderings that are not arbitrary and that will break if changed:

* **Materials before the environment.** The map builder looks up `MI_*` per
  surface. Running it first produces a blockout in the engine default material,
  which is a valid build and a useless one.
* **Characters before the cast payload.** The payload is a file the character
  builder writes and the payload applier reads. Applying before building applies
  last run's cast.

---

## 4. What happens when content is missing

Every script follows the same three-state contract:

| State | Example | What the script does | What the game does |
| --- | --- | --- | --- |
| `created` / `updated` | normal path | logs the asset path | uses it |
| `skipped_asset` | a Niagara system with no stack yet, a font with no `.ttf` | records the path, the reason and the *exact* fix, and writes a human-readable brief | uses the documented fallback (sprite actor, procedural audio, code-drawn HUD) |
| `failed` | a reflected property that no longer exists, a factory that returned null | records it and continues | may be missing a feature; `validate_project.py` will catch it |

A `failed` entry in a build report means the C++ and the script disagree — the
most valuable thing the pipeline produces, because it is the only way a rename in
a header gets noticed before it becomes a runtime null reference.

---

## 5. The CI pipeline

```yaml
# .github/workflows/build.yml equivalents, described rather than templated,
# because the project's pipeline runs on a self-hosted UE5 runner.
stages:
  - lint      : python -m py_compile on every tool
  - cpp       : PYTHONPATH=Tools/Python python3 run_all.py --only cpp_symbols,uht_contracts,soft_paths,calls,tags,layout,voice_lines,input_actions,doc_numbers,test_names,doc_files,doc_types,weapon_ids,cosmetic_ids,hud_strings,audio_ids
                (declared-but-undefined members, UE reflection contracts, the
                 naming contract for name-loaded assets, call arity, gameplay
                 tags, the building layout, the voice script, the input pipeline,
                 the documented numbers, the documented files, the documented
                 types, the armoury, the wardrobe, the HUD's copy table and the
                 cue list - every check that needs no engine install at all, so they run before a runner is ever
                 allocated)
  - config    : PYTHONPATH=Tools/Python python3 run_all.py --text-only
  - build     : UBT build of the Editor, Game, Server and Client targets
  - content   : UnrealEditor-Cmd -run=pythonscript run_all.py
  - validate  : UnrealEditor-Cmd -run=pythonscript validate_project.py
  - test      : UnrealEditor-Cmd -ExecCmds="Automation RunTests OneBraham"
  - package   : RunUAT BuildCookRun (client + Linux server)
  - smoke     : the 12-player dedicated smoke test against the packaged server
  - archive   : upload the artefacts, write the build report into the release notes
```

The pipeline fails on: a compile error, any `--text-only` failure, any validator
FAIL, any automation test failure, or a smoke test that does not end in a
`MatchResult`.

---

## 6. Reproducibility

* Every script is deterministic: fixed iteration order, no time-dependent output
  except the report timestamps, no random placement without a seeded
  `FRandomStream`.
* Running the pipeline twice produces the same assets. Running it on a clean
  clone produces the same assets as running it on a developer's machine.
* The one intentional exception: `--dress` places props from a manifest whose
  ordering is stable but whose *outcome* depends on which prop meshes have been
  imported — and it says so in its own output.

---

## 7. Adding a new generated asset

1. Add the data to the relevant table (`SURFACES`, `PROPS`, `WEAPONS`,
   `ARCHETYPES`, `NIAGARA_SYSTEMS`, `ACTIONS`, `WIDGETS`, `HUD_STRINGS`…).
2. If it is a new *kind* of asset, add a `build_*` step and a row in `run_all.py`.
3. Add an assertion to `validate_project.py` if the game would break silently
   without it. Silent is the enemy: a missing map is obvious, a missing footstep
   cue is not.
4. Add the licence entry to `Docs/LICENCES.md` if it came from anywhere but this
   repository.
5. Run `Scripts\BuildContent.bat` and paste the report into the pull request.

**Never hand-place something that a script could place.** Hand-edits are the
reason map bugs take two weeks to find: the level, the manifest and the validator
silently disagree, and the only way to be sure is to make the script the only
writer.
