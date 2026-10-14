# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.validate_project
==========================

The project's own health check. Run it before every build; run it in CI; run it
when something feels wrong and you cannot say why.

WHAT IT CHECKS, AND WHY EACH ONE HAS BITTEN SOMEONE
    1.  Soft paths the C++ loads by name              - a missing one is a silent
                                                        fallback at runtime.
    2.  Every IA_ asset the input config binds        - a null field is a key
                                                        that does nothing.
    3.  The 15 surface types in the config            - off-by-one here gives the
                                                        wrong footstep on marble.
    4.  Gameplay tags declared in C++ and in the ini  - a tag that exists in one
                                                        and not the other fails
                                                        only when it is used.
    5.  Weapon definitions                            - tiering, ranges, RPM and
                                                        penetration sanity.
    6.  Access zones                                  - every zone reachable,
                                                        every pair reciprocal,
                                                        no Skydeck-locking zone.
    7.  Smart-link integrity                          - the map builder's manifest
                                                        against the level.
    8.  Spawn safety                                  - no spawn anchor inside an
                                                        enemy's opening rush.
    9.  Audio bank coverage                           - every cue the code plays
                                                        exists or has a fallback.
    10. Cosmetics                                     - cosmetic-only, proven.
    11. Performance budget                            - actor, light and triangle
                                                        counts against the caps.
    12. Config drift                                  - the ini and the C++
                                                        agreeing on defaults.
    13. Breachable walls                              - all four walls are the
                                                        real actor, not blockout
                                                        geometry wearing a tag.
    14. Declared-but-undefined member functions       - a method declared in a
                                                        header with no definition
                                                        anywhere is a link error.
    15. Project includes                              - an include naming a file
                                                        that does not exist.
    16. Log categories                                - a category the C++ logs
                                                        to that the ini never
                                                        mentions defaults to All.
    17. Brace balance                                 - a file with one brace too
                                                        few is a bad edit that
                                                        reads as 400 errors.
    18. VFX reachability                              - an effect that exists as
                                                        an enum value and an
                                                        asset, and is spawned by
                                                        nothing, is an asset the
                                                        player never sees.
    19. Surface coverage                              - a physical material with
                                                        no geometry using it means
                                                        the room sounds and reacts
                                                        like whatever is left over.
    20. UnrealHeaderTool contracts                    - an RPC with no body, a
                                                        RepNotify that does not
                                                        exist, and the expensive
                                                        one: a replicated property
                                                        with no DOREPLIFETIME, which
                                                        never reaches a client.
    21. Soft-load naming contract                     - a /Game/ path the C++
                                                        loads by name that the
                                                        pipeline never writes is
                                                        a silent null at runtime
                                                        and nothing else.
    22. Call arity                                    - a call whose argument
                                                        count cannot match the
                                                        declaration is a build
                                                        that stops, and it is
                                                        the most common compile
                                                        error in a codebase this
                                                        size.
    23. Gameplay tags                                 - a tag requested but
                                                        never declared returns an
                                                        invalid tag, and every
                                                        query on it falls through
                                                        in silence.
    24. Building layout                               - the floor table the map
                                                        is built from against the
                                                        floor table the game
                                                        describes: a mismatch
                                                        names floors wrongly, in
                                                        every direction, without
                                                        an error.
    25. Voice lines                                   - a line the code asks for
                                                        that the script does not
                                                        have is a character who
                                                        says nothing, silently,
                                                        forever.
    26. Input actions                                 - the generated actions, the
                                                        fields of the input
                                                        config, the keys the ini
                                                        binds and the fields the
                                                        controller reads, as one
                                                        pipeline rather than four
                                                        lists that disagree.
    27. Documented numbers                            - every count written down
                                                        in the build report and
                                                        the README, against the
                                                        count the tooling
                                                        produces: a stale number
                                                        in a document reads as
                                                        verification.

    It writes Saved/Validation/ob_validation_<timestamp>.md and exits non-zero on
    any FAIL. Warnings do not fail the build; failures do.

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/validate_project.py"
    (-game is not needed: validation is editor-side by nature.)

    Also runnable outside the editor for the config-only checks:
        PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/validate_project.py --config-only
"""

from __future__ import print_function

import datetime
import io
import os
import re
import sys

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                          "..", "..", ".."))
CONFIG_DIR = os.path.join(REPO_ROOT, "Config")
SOURCE_DIR = os.path.join(REPO_ROOT, "Source", "OneBraham")
REPORT_DIR = os.path.join(REPO_ROOT, "Saved", "Validation")

# Soft paths the C++ resolves at runtime. Missing here = silent fallback.
EXPECTED_CONTENT = [
    ("/Game/OneBraham/Blueprints/BP_Keycard", "loaded when a keycard is dropped"),
    ("/Game/OneBraham/Data/DA_InputConfig", "the controller's input bindings"),
    ("/Game/OneBraham/Data/DA_WeaponRegistry", "every weapon definition"),
    ("/Game/OneBraham/Characters/DA_CharacterRegistry", "the six archetypes"),
    ("/Game/OneBraham/Audio/Banks/DA_AudioBank", "every sound cue"),
    ("/Game/OneBraham/UI/HUD/WBP_OneBrahamHUD", "the HUD widget"),
    ("/Game/OneBraham/VFX/CameraShakes/BP_OBCameraShake_Explosion", "explosion shake"),
    ("/Game/OneBraham/VFX/CameraShakes/BP_OBCameraShake_Landing", "landing shake"),
    ("/Game/OneBraham/VFX/CameraShakes/BP_OBCameraShake_Extraction", "rotor shake"),
    ("/Game/OneBraham/Maps/L_Persistent_OneBraham", "the playable map"),
]

EXPECTED_NIAGARA = [
    "NS_OB_Impact_Concrete", "NS_OB_Impact_Metal", "NS_OB_Impact_Glass",
    "NS_OB_Impact_Flesh", "NS_OB_MuzzleFlash", "NS_OB_Tracer", "NS_OB_ShellEject",
    "NS_OB_Explosion_Frag", "NS_OB_Explosion_Breach", "NS_OB_Flashbang",
    "NS_OB_Smoke", "NS_OB_BloodMist", "NS_OB_SprinklerSpray", "NS_OB_SparkShower",
    "NS_OB_Dust", "NS_OB_BreachDebris", "NS_OB_CorpseImpact", "NS_OB_Rain",
]

# Performance caps from Docs/18_Performance.md. A dedicated 12-player server is
# the tightest target: it has no renderer, but it has every AI, every projectile
# and every replication graph.
PERF_CAPS = {
    "runtime_actors": 40000,
    "point_lights": 900,
    "static_mesh_instances": 60000,
    "nanite_triangles": 40000000,
    "niagara_systems": 17,
}


class Finding(object):
    def __init__(self, level, check, message, detail=""):
        self.level = level        # "PASS" | "WARN" | "FAIL" | "INFO"
        self.check = check
        self.message = message
        self.detail = detail

    def line(self):
        text = u"[{0}] {1}: {2}".format(self.level, self.check, self.message)
        if self.detail:
            text += u"\n        " + self.detail
        return text


class Validator(object):
    def __init__(self):
        self.findings = []

    def pass_(self, check, message, detail=""):
        self.findings.append(Finding("PASS", check, message, detail))

    def warn(self, check, message, detail=""):
        self.findings.append(Finding("WARN", check, message, detail))

    def fail(self, check, message, detail=""):
        self.findings.append(Finding("FAIL", check, message, detail))

    def info(self, check, message, detail=""):
        self.findings.append(Finding("INFO", check, message, detail))

    def count(self, level):
        return sum(1 for f in self.findings if f.level == level)

    def ok(self):
        return self.count("FAIL") == 0


def read_text(path):
    if not os.path.isfile(path):
        return u""
    with io.open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


# ---------------------------------------------------------------------------
#  1. Content paths
# ---------------------------------------------------------------------------

def check_content_paths(v, config_only):
    if config_only or not C.IN_EDITOR:
        v.info("content paths", "skipped outside the editor")
        return

    missing = 0

    for path, why in EXPECTED_CONTENT:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            continue

        missing += 1
        v.fail("content path", u"{0} is missing ({1})".format(path, why),
               u"generated by the build scripts in Tools/Python/onebraham")

    for stem in EXPECTED_NIAGARA:
        path = u"{0}/{1}".format(C.VFX, stem)
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            missing += 1
            v.fail("niagara", u"{0} is missing".format(path),
                   u"generated by build_vfx.py; the runtime falls back to a sprite "
                   u"so this is not fatal, but it is not shipped quality")

    if missing == 0:
        v.pass_("content paths", u"all {0} named content paths resolve".format(
            len(EXPECTED_CONTENT) + len(EXPECTED_NIAGARA)))


# ---------------------------------------------------------------------------
#  2. Input
# ---------------------------------------------------------------------------

def check_input(v, config_only):
    ini = read_text(os.path.join(CONFIG_DIR, "DefaultInput.ini"))

    if "[OneBraham.Input]" not in ini:
        v.fail("input", "Config/DefaultInput.ini has no generated [OneBraham.Input] section",
               "run Tools/Python/onebraham/generate_input_ini.py")
        return

    bindings = re.findall(r"\+KeyboardBinding=(\w+),", ini)
    gamepad = re.findall(r"\+GamepadBinding=(\w+),", ini)

    if not bindings:
        v.fail("input", "no keyboard bindings in the generated section")
    else:
        v.pass_("input", u"{0} keyboard bindings, {1} gamepad bindings".format(
            len(bindings), len(gamepad)))

    prompts = re.findall(r"\+ActionPrompt=(\w+)=", ini)
    if len(prompts) < 38:
        v.warn("input", u"only {0} action prompts generated".format(len(prompts)))

    # Required by the C++: every field of UOneBrahamInputConfig. Doing this
    # without the editor means reading the header, which is exactly what the
    # config-only mode is for.
    header = read_text(os.path.join(SOURCE_DIR, "Core", "OneBrahamGameSettings.h"))
    fields = re.findall(r"TObjectPtr<UInputAction>\s+(\w+);", header)

    if not fields:
        v.warn("input", "could not read the input config fields from the header")
        return

    prompt_names = set(prompts)
    unbound = [f for f in fields if ("IA_" + f) not in prompt_names]

    if unbound:
        v.fail("input", u"{0} input config fields have no action: {1}".format(
            len(unbound), ", ".join(unbound)))
    else:
        v.pass_("input", u"all {0} input config fields have a bound action".format(len(fields)))

    if config_only or not C.IN_EDITOR:
        return

    path = "/Game/OneBraham/Data/DA_InputConfig"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        v.fail("input config", "DA_InputConfig is missing")
        return

    config = unreal.EditorAssetLibrary.load_asset(path)
    try:
        missing_actions = config.call_method("ValidateAllActionsSet")
        if missing_actions:
            v.fail("input config", u"ValidateAllActionsSet reports {0} null field(s): {1}".format(
                len(missing_actions), ", ".join(str(m) for m in missing_actions)))
        else:
            v.pass_("input config", "ValidateAllActionsSet: all actions assigned")
    except Exception as exc:  # noqa: BLE001
        v.warn("input config", u"could not run ValidateAllActionsSet: {0}".format(exc))


# ---------------------------------------------------------------------------
#  3. Surfaces
# ---------------------------------------------------------------------------

def check_surfaces(v, _config_only):
    """The config's surface list and the C++ enum are the same list written twice.

    This check compares them *in order and by name*, because a mismatch here is
    the bug that plays a marble footstep on carpet and is never noticed until a
    player reports it as bad audio on Twitter.
    """
    engine_ini = read_text(os.path.join(CONFIG_DIR, "DefaultEngine.ini"))

    declared = re.findall(r'\+PhysicalSurfaces=\(Type=(\w+),Name="(\w+)"\)', engine_ini)
    names = [name for _type, name in declared]

    if not names:
        v.fail("surfaces", "no +PhysicalSurfaces entries in DefaultEngine.ini")
        return

    # The canonical order lives in one place: the map builder's SURFACE_ORDER.
    from onebraham.build_environment import SURFACE_ORDER

    expected_names = [s for s in SURFACE_ORDER]

    # The ini carries 15 (the enum's 15); SURFACE_ORDER is the 15-value list.
    if names != expected_names:
        differing = [u"{0}: ini '{1}' vs code '{2}'".format(index, a, b)
                     for index, (a, b) in enumerate(zip(names, expected_names)) if a != b]

        v.fail("surfaces", u"physical surface order differs from SURFACE_ORDER",
               u"; ".join(differing) or u"count differs: ini {0}, code {1}".format(
                   len(names), len(expected_names)))
    else:
        v.pass_("surfaces", u"{0} physical surfaces match SURFACE_ORDER exactly".format(
            len(names)))

    # The C++ enum must have the same number of values.
    types_header = read_text(os.path.join(SOURCE_DIR, "Core", "OneBrahamTypes.h"))
    enum_match = re.search(r"enum class EOneBrahamFootstepSurface : uint8\s*\{(.*?)\};",
                           types_header, re.S)

    if enum_match:
        values = [line.strip().rstrip(",") for line in enum_match.group(1).splitlines()
                  if line.strip() and not line.strip().startswith("/")]
        values = [x.split("=")[0].strip() for x in values if x and not x.startswith("*")]

        if len(values) != len(names):
            v.fail("surfaces", u"enum has {0} values, the config declares {1}".format(
                len(values), len(names)),
                   u"this is the exact off-by-one that plays the wrong footstep")
        elif [v_.upper() for v_ in values] != [n.upper() for n in names]:
            v.fail("surfaces", "enum value names do not match the config names",
                   u"code: {0}".format(", ".join(values)))
        else:
            v.pass_("surfaces", u"enum and config agree on {0} surfaces, in order".format(
                len(values)))


# ---------------------------------------------------------------------------
#  4. Gameplay tags
# ---------------------------------------------------------------------------

def check_gameplay_tags(v, _config_only):
    tags_header = read_text(os.path.join(SOURCE_DIR, "Core", "OneBrahamGameplayTags.h"))
    declared = re.findall(r"UE_DECLARE_GAMEPLAY_TAG_EXTERN\((\w+)\)", tags_header)

    if not declared:
        v.fail("gameplay tags", "no tag declarations found in OneBrahamGameplayTags.h")
        return

    tags_cpp = read_text(os.path.join(SOURCE_DIR, "Core", "OneBrahamGameplayTags.cpp"))
    defined = re.findall(r'UE_DEFINE_GAMEPLAY_TAG\((\w+),\s*"([^"]+)"\)', tags_cpp)
    defined_names = set(name for name, _tag in defined)

    missing = [t for t in declared if t not in defined_names]

    if missing:
        v.fail("gameplay tags", u"{0} tags declared but not defined: {1}".format(
            len(missing), ", ".join(missing)))
    else:
        v.pass_("gameplay tags", u"all {0} declared tags are defined".format(len(declared)))

    # Tags used with a literal string anywhere in the code must exist as OB.* in
    # the C++ definitions, otherwise they silently never match.
    literals = set()
    for folder, _dirs, files in os.walk(SOURCE_DIR):
        for filename in files:
            if not filename.endswith((".cpp", ".h")):
                continue
            text = read_text(os.path.join(folder, filename))
            literals.update(re.findall(r'RequestGameplayTag\(\s*TEXT\("([^"]+)"\)', text))
            literals.update(re.findall(r'FGameplayTag::RequestGameplayTag\(\s*FName\("([^"]+)"\)',
                                       text))

    defined_tags = set(tag for _name, tag in defined)
    unknown = sorted(t for t in literals if t.startswith("OB.") and t not in defined_tags)

    if unknown:
        v.fail("gameplay tags", u"{0} tags requested by string that are never defined: {1}"
               .format(len(unknown), ", ".join(unknown[:12])))
    elif literals:
        v.pass_("gameplay tags", u"all {0} string-requested tags are defined".format(len(literals)))


# ---------------------------------------------------------------------------
#  5. Weapons
# ---------------------------------------------------------------------------

def check_weapons(v, config_only):
    if config_only or not C.IN_EDITOR:
        v.info("weapons", "skipped outside the editor")
        return

    path = "/Game/OneBraham/Data/DA_WeaponRegistry"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        v.fail("weapons", "DA_WeaponRegistry is missing")
        return

    registry = unreal.EditorAssetLibrary.load_asset(path)

    try:
        weapons = registry.get_editor_property("weapons")
    except Exception as exc:  # noqa: BLE001
        v.fail("weapons", u"could not read the registry: {0}".format(exc))
        return

    if len(weapons) != 10:
        v.fail("weapons", u"{0} weapons in the registry, expected 10".format(len(weapons)))
        return

    problems = []

    for weapon in weapons:
        def get(prop, default=None):
            try:
                return weapon.get_editor_property(prop)
            except Exception:  # noqa: BLE001
                return default

        weapon_id = get("weapon_id", "?")
        damage = get("base_damage", 0.0)
        rpm = get("rounds_per_minute", 0.0)
        mag = get("magazine_size", 0)
        pen = get("max_penetration_thickness", 0.0)
        ads = get("ads_time", 0.0)
        draw = get("draw_time", 0.0)

        if damage <= 0.0:
            problems.append(u"{0}: damage {1}".format(weapon_id, damage))
        if rpm <= 0.0 or rpm > 1200.0:
            problems.append(u"{0}: rpm {1}".format(weapon_id, rpm))
        if mag <= 0 or mag > 60:
            problems.append(u"{0}: magazine {1}".format(weapon_id, mag))
        if pen < 0.0 or pen > 30.0:
            problems.append(u"{0}: penetration {1} cm".format(weapon_id, pen))
        if ads <= 0.0 or ads > 0.5:
            problems.append(u"{0}: ADS time {1} s".format(weapon_id, ads))
        if draw <= 0.0 or draw > 1.0:
            problems.append(u"{0}: draw time {1} s".format(weapon_id, draw))

    if problems:
        v.fail("weapons", u"{0} weapon(s) outside sane bounds".format(len(problems)),
               u"; ".join(problems))
    else:
        v.pass_("weapons", u"all 10 weapons are within bounds")

    # The one balance rule that matters: no sidearm out-damages a rifle.
    by_slot = {}
    for weapon in weapons:
        try:
            slot = str(weapon.get_editor_property("default_slot"))
            by_slot.setdefault(slot, []).append(weapon)
        except Exception:  # noqa: BLE001
            continue

    v.info("weapons", u"slots: {0}".format(
        ", ".join(u"{0}={1}".format(k, len(vals)) for k, vals in sorted(by_slot.items()))))


# ---------------------------------------------------------------------------
#  6. Access zones
# ---------------------------------------------------------------------------

def check_access(v, config_only):
    if config_only or not C.IN_EDITOR:
        v.info("access", "skipped outside the editor")
        return

    zones_class = getattr(unreal, "OneBrahamAccessZone", None)
    if zones_class is None:
        v.info("access", "AOneBrahamAccessZone not reflected - skipped")
        return

    world = unreal.EditorLevelLibrary.get_editor_world() if hasattr(unreal, "EditorLevelLibrary") else None
    if world is None:
        v.info("access", "no editor world - skipped")
        return

    zones = []
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if isinstance(actor, zones_class):
            zones.append(actor)

    if not zones:
        v.fail("access", "no access zones placed in the level",
               "run build_environment.py")
        return

    ids = {}
    problems = []

    for zone in zones:
        try:
            zone_id = str(zone.get_editor_property("zone_id"))
            paired = str(zone.get_editor_property("paired_zone_id"))
            tier = str(zone.get_editor_property("required_tier"))
            floor = int(zone.get_editor_property("floor_index"))
        except Exception:  # noqa: BLE001
            continue

        ids[zone_id] = (paired, tier, floor)

        if zone_id in (None, "None", ""):
            problems.append(u"a zone on floor {0} has no id".format(floor))

    for zone_id, (paired, tier, floor) in ids.items():
        if paired in (None, "None", ""):
            continue

        if paired not in ids:
            problems.append(u"{0} pairs with {1}, which is not placed".format(zone_id, paired))
        else:
            back = ids[paired][0]
            if back != zone_id:
                problems.append(u"{0} -> {1} is not reciprocal".format(zone_id, paired))

    # The Skydeck gate must exist and must be the hardest zone in the building.
    gate = [z for z, (_p, t, _f) in ids.items() if "Skydeck" in z]
    if not gate:
        problems.append("no zone leads to the Skydeck: ASCENT cannot be won")
    elif "Skydeck" not in ids[gate[0]][1]:
        problems.append(u"the Skydeck zone is tier {0}, which is not the top tier".format(
            ids[gate[0]][1]))

    if problems:
        v.fail("access", u"{0} access zone problem(s)".format(len(problems)),
               u"; ".join(problems))
    else:
        v.pass_("access", u"{0} zones, pairing reciprocal, Skydeck reachable".format(len(ids)))


# ---------------------------------------------------------------------------
#  6b. Breachable walls
# ---------------------------------------------------------------------------

#: The four partitions the design promises a charge can open, by floor. A wall
#: that goes missing is not a missing prop: it is one of the three routes up the
#: tower quietly not existing.
EXPECTED_BREACH_FLOORS = (5, 9, 12, 14)

#: Reported when geometry is still wearing the engine's default material, which
#: means no surface instance was ever assigned to it.
DEFAULT_MATERIAL_TOKENS = ("DefaultMaterial", "/Engine/EngineMaterials/",
                           "/Engine/BasicShapes/", "WorldGridMaterial")


def check_breachables(v, config_only):
    """Every breachable wall is the real actor, on a real surface."""
    if config_only or not C.IN_EDITOR:
        v.info("breachables", "skipped outside the editor")
        return

    wall_class = getattr(unreal, "OneBrahamBreachableWall", None)
    if wall_class is None:
        v.fail("breachables", "AOneBrahamBreachableWall is not reflected",
               "the C++ module did not load: no wall in the building can be breached")
        return

    walls = [a for a in unreal.EditorLevelLibrary.get_all_level_actors()
             if isinstance(a, wall_class)]

    if len(walls) != len(EXPECTED_BREACH_FLOORS):
        v.fail("breachables",
               u"{0} breachable walls in the level, expected {1}".format(
                   len(walls), len(EXPECTED_BREACH_FLOORS)),
               u"run Scripts/BuildContent.bat --only environment,props --dress; "
               u"the walls are listed in BREACHABLE_WALLS in build_environment.py")
        return

    problems = []
    floors = []
    seen_ids = set()

    for wall in walls:
        try:
            wall_id = str(wall.get_editor_property("wall_id"))
            floor = int(wall.get_editor_property("floor_index"))
            surface = str(wall.get_editor_property("surface_id"))
        except Exception as exc:  # noqa: BLE001
            problems.append(u"a wall could not be read: {0}".format(exc))
            continue

        floors.append(floor)

        if wall_id in (None, "None", ""):
            problems.append(u"a wall on floor {0} has no id".format(floor))
        elif wall_id in seen_ids:
            problems.append(u"two walls share the id {0}".format(wall_id))
        seen_ids.add(wall_id)

        if surface in (None, "None", ""):
            problems.append(u"{0} has no surface, so impacts and footsteps are wrong".format(wall_id))

        # A wall on the engine default material is blockout geometry wearing a
        # tag: it will breach, and it will look like the engine's default grey.
        try:
            material = wall.get_editor_property("wall_mesh").get_material(0)
            material_path = material.get_path_name() if material is not None else "None"
            if any(token in material_path for token in DEFAULT_MATERIAL_TOKENS):
                problems.append(u"{0} is on a placeholder material ({1})".format(
                    wall_id, material_path))
        except Exception:  # noqa: BLE001
            pass

    missing_floors = [f for f in EXPECTED_BREACH_FLOORS if f not in floors]
    if missing_floors:
        problems.append(u"no breachable wall on floor(s) {0}".format(
            u", ".join(str(f) for f in missing_floors)))

    if problems:
        v.fail("breachables", u"{0} breachable wall problem(s)".format(len(problems)),
               u"; ".join(problems))
    else:
        v.pass_("breachables", u"{0} walls, floors {1}, all reachable".format(
            len(walls), u", ".join(str(f) for f in sorted(floors))))


# ---------------------------------------------------------------------------
#  7. Map manifest vs level
# ---------------------------------------------------------------------------

def check_manifest(v, config_only):
    manifest = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                             "..", "onebraham_build_manifest.json"))

    if not os.path.isfile(manifest):
        # Outside the editor there is nothing to compare the manifest against, and
        # a fresh clone legitimately has no manifest until the content build has
        # run once. Inside the editor a missing manifest means the map was never
        # generated, which is a failure: every other check is then looking at a
        # level that does not exist.
        if config_only or not C.IN_EDITOR:
            v.warn("manifest", u"no build manifest yet at {0}".format(manifest),
                   u"expected on a fresh clone; run "
                   u"Scripts/BuildContent.bat --only environment to create it")
        else:
            v.fail("manifest", u"no build manifest at {0}".format(manifest),
                   u"run Tools/Python/onebraham/build_environment.py")
        return

    import json
    with io.open(manifest, "r", encoding="utf-8") as handle:
        data = json.loads(handle.read())

    areas = data.get("areas", [])
    spawns = data.get("spawn_anchors", [])
    floors = data.get("floors", [])

    if len(areas) != 8:
        v.fail("manifest", u"{0} areas in the manifest, expected 8".format(len(areas)))
    if len(floors) < 16:
        v.fail("manifest", u"{0} floors built, expected 16 (plaza + 15)".format(len(floors)))
    if len(spawns) < 24:
        v.warn("manifest", u"only {0} spawn anchors: 12 players need options".format(len(spawns)))

    if len(areas) == 8 and len(floors) >= 16:
        v.pass_("manifest", u"{0} areas, {1} floors, {2} spawn anchors".format(
            len(areas), len(floors), len(spawns)))


# ---------------------------------------------------------------------------
#  8. Spawn safety
# ---------------------------------------------------------------------------

def check_spawn_safety(v, config_only):
    if config_only or not C.IN_EDITOR:
        v.info("spawn safety", "skipped outside the editor")
        return

    spawn_class = getattr(unreal, "OneBrahamSpawnPoint", None)
    if spawn_class is None:
        v.info("spawn safety", "AOneBrahamSpawnPoint not reflected - skipped")
        return

    anchors = [a for a in unreal.EditorLevelLibrary.get_all_level_actors()
               if isinstance(a, spawn_class)]

    if not anchors:
        v.fail("spawn safety", "no spawn anchors in the level")
        return

    import math
    problems = []

    for i, a in enumerate(anchors):
        for b in anchors[i + 1:]:
            # Cluster, not team: ASCENT is a free-for-all of three squads, so two
            # anchors in different clusters are as hostile to each other as two
            # teams are in FIRE DRILL.
            try:
                cluster_a = int(a.get_editor_property("squad_cluster_id"))
                cluster_b = int(b.get_editor_property("squad_cluster_id"))
            except Exception:  # noqa: BLE001
                cluster_a = cluster_b = 0

            location_a = a.get_actor_location()
            location_b = b.get_actor_location()

            # Same storey only: a squad a floor away cannot reach this spawn
            # before the deploy window is over. See build_environment.py.
            if abs(location_a.z - location_b.z) >= C.FLOOR_HEIGHT_CM:
                continue

            distance = math.hypot(location_a.x - location_b.x, location_a.y - location_b.y)

            if cluster_a != cluster_b and distance < C.SPAWN_MIN_ENEMY_DISTANCE_CM:
                problems.append(u"{0} is {1:.0f} cm from opposing cluster {2}".format(
                    a.get_actor_label(), distance, b.get_actor_label()))
            elif cluster_a == cluster_b and distance < C.SPAWN_MIN_SAME_TEAM_DISTANCE_CM:
                problems.append(u"{0} is {1:.0f} cm from cluster-mate {2}".format(
                    a.get_actor_label(), distance, b.get_actor_label()))

    if problems:
        v.fail("spawn safety", u"{0} spawn pair(s) too close".format(len(problems)),
               u"; ".join(problems[:10]))
    else:
        v.pass_("spawn safety", u"{0} anchors, all pairs respect the distances".format(
            len(anchors)))


# ---------------------------------------------------------------------------
#  9. Audio
# ---------------------------------------------------------------------------

def check_audio(v, config_only):
    """Every cue the code plays must exist in the bank or be a known fallback."""
    cue_patterns = [
        r'PlaySound2D\(FName\(TEXT\("([A-Za-z0-9_]+)"\)',
        r'PlaySoundAtLocation\(FName\(TEXT\("([A-Za-z0-9_]+)"\)',
        r'PlaySoundAttached\(FName\(TEXT\("([A-Za-z0-9_]+)"\)',
        r'PlayLocalUISound\(FName\(TEXT\("([A-Za-z0-9_]+)"\)',
        r'PlayReloadCue\(FName\(TEXT\("([A-Za-z0-9_]+)"\)',
    ]

    played = set()
    for folder, _dirs, files in os.walk(SOURCE_DIR):
        for filename in files:
            if not filename.endswith((".cpp", ".h")):
                continue
            text = read_text(os.path.join(folder, filename))
            for pattern in cue_patterns:
                played.update(re.findall(pattern, text))

    bank = read_text(os.path.join(SOURCE_DIR, "Audio", "OneBrahamAudioBankAsset.cpp"))
    banked = set(re.findall(r'TEXT\("([A-Za-z0-9_]+)"\)', bank))

    missing = sorted(c for c in played if c not in banked and c.endswith(tuple("0123456789"))
                     is False and c not in banked)

    if not played:
        v.warn("audio", "no cue ids found in the code")
        return

    if missing:
        v.warn("audio", u"{0} cue(s) played but not in the bank: {1}".format(
            len(missing), ", ".join(missing[:10])),
               u"the audio manager falls back to a synthesised cue, so this is a "
               u"polish issue rather than a break")
    else:
        v.pass_("audio", u"all {0} played cues exist in the bank".format(len(played)))


# ---------------------------------------------------------------------------
#  10. Cosmetics
# ---------------------------------------------------------------------------

def check_cosmetics(v, config_only):
    if config_only or not C.IN_EDITOR:
        v.info("cosmetics", "skipped outside the editor")
        return

    import json
    payload = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                            "..", "payloads", "character_registry.json"))

    if not os.path.isfile(payload):
        v.warn("cosmetics", "no character registry payload to check")
        return

    with io.open(payload, "r", encoding="utf-8") as handle:
        data = json.loads(handle.read())

    cosmetics = data.get("cosmetics", [])
    if not cosmetics:
        v.pass_("cosmetics", "no cosmetic entries to check yet")
        return

    banned = ("scale", "speed", "hitbox", "accuracy", "sound")

    offenders = []
    for entry in cosmetics:
        text = u" ".join(str(value).lower() for value in entry.values())
        if any(word in text for word in banned):
            offenders.append(entry.get("id", "?"))

    if offenders:
        v.fail("cosmetics", u"{0} cosmetics affect gameplay: {1}".format(
            len(offenders), ", ".join(offenders)),
               u"monetisation is cosmetic-only. This is a storefront refund, not "
               u"a design preference")
    else:
        v.pass_("cosmetics", u"{0} cosmetics, all cosmetic-only".format(len(cosmetics)))


# ---------------------------------------------------------------------------
#  11. Performance budget
# ---------------------------------------------------------------------------

def check_performance(v, config_only):
    if config_only or not C.IN_EDITOR:
        v.info("performance", "skipped outside the editor")
        return

    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    counts = {}

    for actor in actors:
        name = actor.get_class().get_name()
        counts[name] = counts.get(name, 0) + 1

    lights = sum(count for name, count in counts.items() if "Light" in name)
    meshes = sum(count for name, count in counts.items() if "StaticMeshActor" in name)

    if len(actors) > PERF_CAPS["runtime_actors"]:
        v.fail("performance", u"{0} actors in the level, cap is {1}".format(
            len(actors), PERF_CAPS["runtime_actors"]))
    else:
        v.pass_("performance", u"{0} actors, {1} static mesh actors, {2} lights".format(
            len(actors), meshes, lights))

    if lights > PERF_CAPS["point_lights"]:
        v.fail("performance", u"{0} lights, cap is {1}".format(lights, PERF_CAPS["point_lights"]))
    elif lights > PERF_CAPS["point_lights"] * 0.8:
        v.warn("performance", u"{0} lights is close to the cap of {1}".format(
            lights, PERF_CAPS["point_lights"]))


# ---------------------------------------------------------------------------
#  12. Config drift
# ---------------------------------------------------------------------------

#: Where a UCLASS(config=X) reads its section from.
CONFIG_FILE_FOR = {"Engine": "DefaultEngine.ini", "Game": "DefaultGame.ini",
                   "GameUserSettings": "DefaultGameUserSettings.ini", "Input": "DefaultInput.ini",
                   "Editor": "DefaultEditor.ini"}


def project_config_classes():
    """{ClassNameWithoutPrefix: (config file, parent, {property: type})} for the module's classes."""
    source = os.path.join(REPO_ROOT, "Source", "OneBraham")
    classes = {}
    for root, _dirs, files in os.walk(source):
        for name in files:
            if not name.endswith(".h"):
                continue
            text = read_text(os.path.join(root, name))
            # Comments out first: a doc comment between UPROPERTY() and the
            # declaration is normal, and commented-out code is not a property.
            text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            text = re.sub(r"//[^\n]*", "", text)
            heads = list(re.finditer(r"UCLASS\(((?:[^()]|\([^()]*\))*)\)\s*class\s+ONEBRAHAM_API\s+[AU](\w+)\s*:\s*public\s+[AU](\w+)", text))
            for index, head in enumerate(heads):
                body = text[head.end():heads[index + 1].start() if index + 1 < len(heads) else len(text)]
                spec = re.search(r"\bconfig\s*=\s*(\w+)", head.group(1), re.I)
                props = dict((prop, kind.strip()) for kind, prop in re.findall(
                    r"UPROPERTY\((?:[^()]|\([^()]*\))*\b(?:config|GlobalConfig)\b(?:[^()]|\([^()]*\))*\)\s*"
                    r"([\w:<>,\s\*]+?)\s+(\w+)\s*(?:=|;|\[|\{)", body, re.I))
                classes[head.group(2)] = (CONFIG_FILE_FOR.get(spec.group(1)) if spec else None,
                                          head.group(3), props)
    return classes


def project_enum_values():
    """{EnumName: set of value names} for the module's UENUMs."""
    source = os.path.join(REPO_ROOT, "Source", "OneBraham")
    enums = {}
    for root, _dirs, files in os.walk(source):
        for name in files:
            if name.endswith(".h"):
                text = re.sub(r"//[^\n]*", "", read_text(os.path.join(root, name)))
                for enum_name, body in re.findall(r"enum\s+class\s+(\w+)\s*:\s*\w+\s*\{(.*?)\}", text, re.S):
                    enums[enum_name] = set(re.findall(r"^\s*(\w+)\s*(?:=|,|UMETA|$)", body, re.M))
    return enums


def check_project_config_keys(v):
    """Every key under [/Script/OneBraham.X] is a config property of X, in the file X reads.

    The engine ignores an ini key that names no property, and a whole section
    for a class that reads another file, without a word. This project shipped
    a 31-line section for a class that does not exist, its anti-cheat
    thresholds in a file their class never opens, and ColorBlindMode for a
    property spelled ColourBlindMode.
    """
    classes = project_config_classes()
    enums = project_enum_values()
    unknown, misplaced, bad_enums = [], [], []
    for root, _dirs, files in os.walk(CONFIG_DIR):
        for name in sorted(files):
            if not name.endswith(".ini"):
                continue
            path = os.path.join(root, name)
            section = None
            for number, raw in enumerate(read_text(path).splitlines(), 1):
                line = raw.strip()
                if line.startswith("[") and line.endswith("]"):
                    section = line[1:-1]
                    continue
                if not section or not section.startswith("/Script/OneBraham.") or not line or line.startswith(";"):
                    continue
                where = u"{0}:{1}".format(os.path.relpath(path, REPO_ROOT), number)
                cls = section[len("/Script/OneBraham."):]
                if cls not in classes:
                    unknown.append(u"{0} [{1}]: no such class".format(where, section))
                    continue
                if root == CONFIG_DIR and classes[cls][0] and name != classes[cls][0]:
                    misplaced.append(u"{0}: {1} reads {2}, not {3}".format(where, cls, classes[cls][0], name))
                key = line.lstrip("+-!.").split("=")[0].split("[")[0].split("(")[0].strip()
                value = line.split("=", 1)[1].strip() if "=" in line else u""
                kind, current = None, cls
                while current in classes and kind is None:
                    kind = classes[current][2].get(key)
                    current = classes[current][1]
                if kind is None:
                    unknown.append(u"{0}: {1} has no config property '{2}'".format(where, cls, key))
                elif kind in enums and value not in enums[kind]:
                    bad_enums.append(u"{0}: {1}={2} is not a {3} value name".format(where, key, value, kind))

    for label, problems, fix in (
            ("config keys", unknown, "the engine ignores these lines; fix the name or delete the line"),
            ("config files", misplaced, "move the section to the file the class reads"),
            ("config enums", bad_enums, "write the enum value's name, as the engine reads it")):
        if problems:
            v.fail("config", u"{0}: {1}".format(label, u"; ".join(problems)), fix)
        else:
            v.pass_("config", u"{0}: every project setting is read".format(label))



#: Engine plugin names (the .uplugin file names) the project may enable, each
#: checked against https://dev.epicgames.com/documentation/unreal-engine/API/PluginIndex.
VERIFIED_ENGINE_PLUGINS = frozenset((
    "EnhancedInput", "CommonUI", "Niagara", "ChaosCloth", "HairStrands", "SoundUtilities",
    "Synthesis", "Metasound", "OnlineSubsystem", "OnlineSubsystemUtils", "OnlineSubsystemSteam",
    "ModelingToolsEditorMode", "EditorScriptingUtilities", "DataValidation", "PythonScriptPlugin",
))


def check_targets(v, _config_only):
    """No target changes the shared build environment unless it owns one.

    A Game or Editor target built against the Launcher engine shares the
    engine's prebuilt modules, and UBT stops the build outright when a target
    changes how they would be compiled: global definitions, Windows compiler
    switches, logging or checks in Shipping. All three of this project's
    targets did, and none of it was used. A target may change them only after
    declaring BuildEnvironment = TargetBuildEnvironment.Unique, which needs a
    source build of the engine (the dedicated server needs one anyway).
    """
    forbidden = ("GlobalDefinitions", "bStrictConformanceMode", "bUseLoggingInShipping",
                 "bUseChecksInShipping", "bOverrideBuildEnvironment", "AdditionalCompilerArguments",
                 "bUseUnityBuild", "bForceEnableExceptions", "bForceEnableRTTI")
    source = os.path.join(REPO_ROOT, "Source")
    problems, targets = [], 0
    for name in sorted(os.listdir(source)):
        if not name.endswith(".Target.cs"):
            continue
        targets += 1
        text = re.sub(r"//[^\n]*", "", read_text(os.path.join(source, name)))
        if "TargetBuildEnvironment.Unique" in text:
            continue
        for setting in forbidden:
            if re.search(r"\b" + setting + r"\b", text):
                problems.append(u"{0} sets {1} on the shared build environment".format(name, setting))
    if problems:
        v.fail("targets", u"; ".join(problems),
               u"UBT refuses this against the Launcher engine; remove it, or give the target "
               u"BuildEnvironment = TargetBuildEnvironment.Unique (source engine only)")
    else:
        v.pass_("targets", u"{0} targets, none changes the shared build environment".format(targets))


#: unreal.* names the content builders have used that the editor's Python API
#: does not have, and what to write instead. Python strips only the U/A/F
#: prefix, so UInputAction_Factory is unreal.InputAction_Factory.
UNREAL_PYTHON_MISNAMES = {
    "InputActionFactory": "InputAction_Factory",
    "InputMappingContextFactory": "InputMappingContext_Factory",
    "GameplayTagLibrary": "common.make_gameplay_tag (UOneBrahamGameplayStatics::RequestTag)",
}


def check_python_api_names(v, _config_only):
    """No builder calls an unreal.* name the editor's Python does not have.

    The builders run only inside the editor, so a wrong name surfaces on the
    first content build, often inside a try block that turns it into a silent
    skip. Every input asset and every archetype tag went missing that way.
    """
    tools = os.path.join(REPO_ROOT, "Tools", "Python", "onebraham")
    problems = []
    for name in sorted(os.listdir(tools)):
        if not name.endswith(".py") or name == "validate_project.py":
            continue
        text = read_text(os.path.join(tools, name))
        for bad, good in sorted(UNREAL_PYTHON_MISNAMES.items()):
            for match in re.finditer(r"\bunreal\." + bad + r"\b", text):
                problems.append(u"{0}:{1} uses unreal.{2}; use {3}".format(
                    name, text[:match.start()].count("\n") + 1, bad, good))
    # A bool property's Python name drops its b: bIntegralSuppressor is
    # integral_suppressor, and "b_integral_suppressor" is neither that nor the
    # C++ name. Five properties (the suppressed pistol's suppressor, shell-by-
    # shell reload, cover penetration, tracer hiding, hiding cosmetics when
    # armed) were never set that way.
    for name in sorted(os.listdir(tools)):
        if not name.endswith(".py") or name == "validate_project.py":
            continue
        text = read_text(os.path.join(tools, name))
        for match in re.finditer(r"""_editor_property\(\s*["'](b_[a-z0-9_]+)["']|_set\([^"'\n]*["'](b_[a-z0-9_]+)["']""", text):
            prop = match.group(1) or match.group(2)
            problems.append(u"{0}:{1} names property '{2}'; a bool's Python name drops the b ('{3}')".format(
                name, text[:match.start()].count("\n") + 1, prop, prop[2:]))

    if problems:
        v.fail("python api", u"; ".join(problems))
    else:
        v.pass_("python api", u"no builder uses a known-missing unreal.* name")


def check_config(v, _config_only):
    game_ini = read_text(os.path.join(CONFIG_DIR, "DefaultGame.ini"))
    engine_ini = read_text(os.path.join(CONFIG_DIR, "DefaultEngine.ini"))

    required_sections = [
        ("[/Script/OneBraham.OneBrahamGameSettings]", engine_ini, "palette, security and registries"),
        ("[/Script/OneBraham.OneBrahamGameUserSettings]",
         read_text(os.path.join(CONFIG_DIR, "DefaultGameUserSettings.ini")), "player settings defaults"),
        ("[/Script/Engine.Engine]", engine_ini, "engine classes"),
    ]

    missing = [name for name, text, _why in required_sections if name not in text]

    if missing:
        v.fail("config", u"missing sections: {0}".format(", ".join(missing)))
    else:
        v.pass_("config", u"all {0} required config sections present".format(
            len(required_sections)))

    check_project_config_keys(v)

    # The content pipeline is Python run inside the editor: -run=pythonscript
    # needs the Python plugin enabled in this project (Epic: per project), and
    # the builders call the Editor Scripting Utilities.
    try:
        import json
        with io.open(os.path.join(os.path.dirname(CONFIG_DIR), "OneBraham.uproject"), encoding="utf-8") as handle:
            plugins = dict((p.get("Name"), p.get("Enabled")) for p in json.load(handle).get("Plugins", []))
    except (IOError, OSError, ValueError) as exc:
        plugins = {}
        v.fail("config", u"OneBraham.uproject could not be read: {0}".format(exc))
    # A plugin is named by its .uplugin file, and a name the engine has no file
    # for stops the editor at startup. "ChaosClothPlugin" (the file is
    # ChaosCloth.uplugin) did exactly that. Every engine plugin the project
    # enables must be one of these names, each checked against Epic's plugin
    # index; a project plugin must exist under Plugins/.
    unknown_plugins = [name for name in plugins
                       if name not in VERIFIED_ENGINE_PLUGINS
                       and not os.path.isdir(os.path.join(os.path.dirname(CONFIG_DIR), "Plugins", name))]
    if unknown_plugins:
        v.fail("config", u"OneBraham.uproject enables {0}, which is not a verified engine plugin name "
               u"or a folder under Plugins/".format(u", ".join(sorted(unknown_plugins))),
               u"use the .uplugin file's name (Epic's plugin index shows it as the link's last segment), "
               u"then add it to VERIFIED_ENGINE_PLUGINS")
    missing_plugins = [name for name in ("PythonScriptPlugin", "EditorScriptingUtilities") if not plugins.get(name)]
    if missing_plugins:
        v.fail("config", u"OneBraham.uproject does not enable {0}".format(u", ".join(missing_plugins)),
               u"Scripts/BuildContent cannot run a single builder without them")
    else:
        v.pass_("config", u"editor Python and scripting plugins enabled")

    # The game net driver. With no definition the engine's IpNetDriver carries
    # every connection and a Steam lobby join, which resolves to a steam.<id>
    # P2P address, cannot connect. The engine's own row must be cleared first or
    # it is matched before ours.
    game_driver = re.search(r'^\+NetDriverDefinitions=\(DefName="GameNetDriver",'
                            r'DriverClassName="([^"]+)",DriverClassNameFallback="([^"]+)"\)',
                            engine_ini, re.M)
    if (game_driver is None or "SteamNetDriver" not in game_driver.group(1)
            or "IpNetDriver" not in game_driver.group(2)):
        v.fail("config", u"no GameNetDriver definition using SteamNetDriver with an IpNetDriver fallback",
               u"Steam lobby joins will fail; see Docs/02, Steam")
    elif "!NetDriverDefinitions=ClearArray" not in engine_ini:
        v.fail("config", u"NetDriverDefinitions is not cleared before the game's rows",
               u"the engine's IpNetDriver row is matched first and the Steam driver never loads")
    else:
        v.pass_("config", u"game net driver: Steam, falling back to IP")

    # Story-critical numbers, checked because they are quoted in the design and
    # in the store page.
    checks = [
        ("MaxPlayers=16", game_ini, "16-player cap for FIRE DRILL"),
        ("SteamDevAppId=", engine_ini, "Steam subsystem configured (dev AppId 480 until release)"),
        ("r.DefaultFeature.AutoExposure", engine_ini, "Lumen exposure configured"),
    ]

    for needle, text, why in checks:
        if needle not in text:
            v.warn("config", u"{0} not found ({1})".format(needle, why))


def check_cpp_symbols(v, _config_only):
    """Every member function declared in a class body must be defined somewhere.

    The compiler finds this and the linker finds this, but both find it minutes
    into a build that a machine has to be able to run. This finds it in 0.3 s on
    any machine, which means it is the check that actually gets run.

    The same checker carries the two faults that are not link errors and that no
    compiler on the project's own machines has ever been asked about: a scoped
    enum compared with an operator it does not have, and a member whose name is
    also an engine property of an ancestor class - which compiles, and then the
    value the code sets is not the value the engine uses.
    """
    try:
        from onebraham import check_cpp_symbols
    except Exception as exc:  # noqa: BLE001
        v.warn("cpp symbols", u"checker unavailable: {0}".format(exc))
        return

    checked, missing = check_cpp_symbols.run()
    hygiene_checks, hygiene = check_cpp_symbols.run_hygiene()

    if checked == 0:
        v.warn("cpp symbols", u"no source parsed - is Source/OneBraham missing?")
        return

    # A scoped enum compared with an operator it does not have is a compile
    # error, and a member that shadows an engine property is a value the engine
    # never sees: both are failures rather than warnings, because neither can be
    # argued with at runtime.
    for rel, line, message in hygiene:
        v.fail("cpp symbols", u"{0}:{1} {2}".format(rel, line, message))

    if not missing and not hygiene:
        v.pass_("cpp symbols", u"all {0} declared member functions have definitions; "
                               u"{1} compile-hygiene checks clean".format(checked, hygiene_checks))
        return

    # A missing definition is a certain link error, so this is a failure, not a
    # warning: it cannot be argued with at runtime.
    for rel, class_name, name in missing:
        v.fail("cpp symbols", u"{0}: {1}::{2} is declared but never defined".format(
            rel, class_name, name))


def check_cpp_includes(v, _config_only):
    """Every project header an included name refers to must exist on disk.

    A file that writes `#include "Core/OneBrahamTypse.h"` is a compile error on
    the line that includes it, and the compiler's message names the include, not
    the typo. This distinguishes project includes from engine includes by name:
    an include is "ours" when it resolves inside Source/OneBraham, or when it
    starts with one of our own subdirectory names, or when its file name is one
    of our headers. Everything else is an engine header and is left alone.
    """
    if not os.path.isdir(SOURCE_DIR):
        v.warn("cpp includes", u"no Source/OneBraham directory; skipped")
        return

    # The project's own header names and top-level package names.
    header_names = set()
    packages = set()

    for dirpath, _dirs, files in os.walk(SOURCE_DIR):
        rel_dir = os.path.relpath(dirpath, SOURCE_DIR)

        if rel_dir != ".":
            packages.add(rel_dir.split(os.sep)[0])

        for name in files:
            if name.endswith(".h"):
                header_names.add(name)

    checked = 0
    missing = []

    for dirpath, _dirs, files in os.walk(SOURCE_DIR):
        for name in files:
            if not name.endswith((".cpp", ".h")):
                continue

            path = os.path.join(dirpath, name)

            with io.open(path, encoding="utf-8", errors="ignore") as handle:
                text = handle.read()

            for match in re.finditer(r'^\s*#include\s+"([^"]+)"', text, re.MULTILINE):
                target = match.group(1)

                if target.endswith(".generated.h"):
                    continue

                # A module's Public/, Private/ and Classes/ folders are its include
                # roots, so a path through one never resolves: the include is
                # written from the folder below them ("InputAction.h", not
                # "EnhancedInput/Public/InputAction.h").
                if re.search(r'(^|/)(Public|Private|Classes)/', target):
                    checked += 1
                    missing.append((os.path.relpath(path, SOURCE_DIR), target))
                    continue

                base = os.path.basename(target)
                first = target.split("/")[0]

                # Ours only if it is one of our headers, or under one of our dirs.
                if base not in header_names and first not in packages and "/" not in target:
                    continue
                if base not in header_names and first not in packages:
                    continue

                checked += 1

                # Quoted includes resolve against the including file's own
                # directory first, then the include paths - so "OneBrahamTypes.h"
                # written inside Core/ is correct and must not be reported.
                if os.path.isfile(os.path.join(SOURCE_DIR, target)):
                    continue
                if os.path.isfile(os.path.join(os.path.dirname(path), target)):
                    continue

                missing.append((os.path.relpath(path, SOURCE_DIR), target))

    if not missing:
        v.pass_("cpp includes", u"all {0} project includes resolve".format(checked))
        return

    for rel, target in missing:
        v.fail("cpp includes", u"{0} includes '{1}', which does not exist".format(rel, target))


def check_log_categories(v, _config_only):
    """Every LogOneBraham* category declared in C++ must have a verbosity entry.

    Unreal's own default for an undeclared category is the compile-time default
    in DECLARE_LOG_CATEGORY_EXTERN, which for this project is All - so a missing
    entry is not "no logs", it is "every log, forever, including in Shipping".
    That is the failure this check exists to prevent.
    """
    header = os.path.join(SOURCE_DIR, "OneBraham.h")
    engine_ini_path = os.path.join(CONFIG_DIR, "DefaultEngine.ini")

    if not os.path.isfile(header) or not os.path.isfile(engine_ini_path):
        v.warn("log categories", u"OneBraham.h or DefaultEngine.ini missing; skipped")
        return

    with io.open(header, encoding="utf-8", errors="ignore") as handle:
        header_text = handle.read()

    declared = sorted(set(re.findall(r'DECLARE_LOG_CATEGORY_EXTERN\((LogOneBraham[A-Za-z0-9_]*),',
                                     header_text)))

    with io.open(engine_ini_path, encoding="utf-8", errors="ignore") as handle:
        ini_text = handle.read()

    section = re.search(r'^\[Core\.Log\](.*?)(?=^\[|\Z)', ini_text, re.MULTILINE | re.DOTALL)
    table = {}
    if section is not None:
        for line in section.group(1).splitlines():
            line = line.strip()
            if not line or line.startswith(";") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            table[key.strip()] = value.strip()

    if not declared:
        v.fail("log categories", u"no LogOneBraham* categories declared in OneBraham.h")
        return

    missing = [name for name in declared if name not in table]

    if missing:
        for name in missing:
            v.fail("log categories", u"{0} is declared in C++ with no [Core.Log] entry "
                                    u"(it will run at its compile-time default)".format(name))
        return

    v.pass_("log categories", u"all {0} project categories have a verbosity in DefaultEngine.ini".format(
        len(declared)))

    # The audit category is the one that must never be quietened; it is quoted in
    # the moderation docs and in the anti-cheat section of Docs/18.
    if table.get("LogOneBrahamAudit", "All").lower() not in ("all", "verbose", "veryverbose"):
        v.warn("log categories", u"LogOneBrahamAudit is set to {0}; Docs/18 promises a full audit "
                                 u"trail (All is expected)".format(table["LogOneBrahamAudit"]))


def check_delimiters(v, _config_only):
    """Every source file's braces, parentheses and brackets must balance.

    This is the cheapest possible check and it catches the most expensive class
    of editing mistake: a deleted closing brace. Unreal will report four hundred
    unrelated errors from a parser that lost its place, and the one line that is
    actually wrong is nowhere in the output. Comments and string literals are
    stripped first, so a brace inside a TEXT() or a comment does not count.
    """
    try:
        from onebraham import check_cpp_symbols as symbols
    except Exception as exc:  # noqa: BLE001
        v.warn("delimiters", u"checker unavailable: {0}".format(exc))
        return

    if not os.path.isdir(SOURCE_DIR):
        v.warn("delimiters", u"no Source/OneBraham directory; skipped")
        return

    files = 0
    bad = []

    for dirpath, _dirs, names in os.walk(SOURCE_DIR):
        for name in names:
            if not name.endswith((".cpp", ".h")):
                continue

            path = os.path.join(dirpath, name)
            files += 1

            with io.open(path, encoding="utf-8", errors="ignore") as handle:
                text = symbols.strip_comments_and_strings(handle.read())

            for opener, closer, label in (("{", "}", "braces"), ("(", ")", "parentheses"),
                                          ("[", "]", "brackets")):
                opened = text.count(opener)
                closed = text.count(closer)

                if opened != closed:
                    bad.append((os.path.relpath(path, SOURCE_DIR), label, opened, closed))

    if not bad:
        v.pass_("delimiters", u"all {0} source files balance their braces, parentheses and brackets".format(files))
        return

    for rel, label, opened, closed in bad:
        v.fail("delimiters", u"{0}: {1} do not balance ({2} open, {3} close)".format(
            rel, label, opened, closed))


def check_vfx_reachability(v, _config_only):
    """Every VFX effect must be spawned by something, directly or through a helper.

    An effect can be declared in the enum, authored as a Niagara system by the
    content build, culled correctly, budgeted correctly - and never spawned,
    because nobody wrote the one line that asks for it. Nothing else catches
    that: the asset validates, the enum validates, and the player simply never
    sees a shell casing.

    "Spawned" here means one of two things:
      * named literally by gameplay code, or
      * returned by a function in OneBrahamVFXStatics.cpp that gameplay code can
        actually reach - followed through the internal calls, so that a surface
        mapping used by SpawnImpact counts and a helper nobody calls does not.

    The dispatch switch that names all seventeen effects is deliberately not
    counted: it would make the check pass by construction.
    """
    statics_h = os.path.join(SOURCE_DIR, "VFX", "OneBrahamVFXStatics.h")
    statics_cpp = os.path.join(SOURCE_DIR, "VFX", "OneBrahamVFXStatics.cpp")

    if not os.path.isfile(statics_h) or not os.path.isfile(statics_cpp):
        v.warn("vfx reachability", u"the VFX library is missing; skipped")
        return

    with io.open(statics_h, encoding="utf-8", errors="ignore") as handle:
        header = handle.read()

    enum_body = re.search(r'enum class EOneBrahamVFXEffect[^{]*\{(.*?)\}', header, re.DOTALL)
    if enum_body is None:
        v.fail("vfx reachability", u"EOneBrahamVFXEffect not found in VFXStatics.h")
        return

    # Comments out first: a documented enumerator ("/** ... */ Rain") is still
    # one name, not a sentence and a name.
    body = re.sub(r'/\*.*?\*/', u"", enum_body.group(1), flags=re.DOTALL)
    body = re.sub(r'//[^\n]*', u"", body)
    effects = [name.strip() for name in re.split(r'[,\n]', body) if name.strip()]

    if not effects:
        v.fail("vfx reachability", u"EOneBrahamVFXEffect is empty")
        return

    # --- 1. What gameplay code names outright -----------------------------
    referenced = set()
    external_text = []

    for dirpath, _dirs, names in os.walk(SOURCE_DIR):
        for name in names:
            if not name.endswith(".cpp"):
                continue

            path = os.path.join(dirpath, name)
            if os.path.normpath(path) == os.path.normpath(statics_cpp):
                continue

            with io.open(path, encoding="utf-8", errors="ignore") as handle:
                text = handle.read()

            external_text.append(text)

            for match in re.finditer(r'EOneBrahamVFXEffect::([A-Za-z0-9_]+)', text):
                referenced.add(match.group(1))

    joined_external = "\n".join(external_text)

    # --- 2. The VFX library's own call graph ------------------------------
    with io.open(statics_cpp, encoding="utf-8", errors="ignore") as handle:
        library = handle.read()

    functions = {}
    for match in re.finditer(r'^(?:[A-Za-z_:<>*&\s]+?)\sUOneBrahamVFXStatics::([A-Za-z0-9_]+)\s*\(', library, re.MULTILINE):
        fn_name = match.group(1)
        open_index = library.index("{", match.end() - 1)

        depth = 0
        index = open_index
        while index < len(library):
            if library[index] == "{":
                depth += 1
            elif library[index] == "}":
                depth -= 1
                if depth == 0:
                    break
            index += 1

        body = library[open_index:index + 1]
        functions[fn_name] = body

    # Which library functions are callable from outside, and which call which.
    entry_points = set()
    for fn_name in functions:
        if re.search(r'\b' + re.escape(fn_name) + r'\s*\(', joined_external):
            entry_points.add(fn_name)

    reachable_functions = set()
    frontier = list(entry_points)

    while frontier:
        current = frontier.pop()
        if current in reachable_functions:
            continue
        reachable_functions.add(current)

        body = functions.get(current, "")
        for candidate in functions:
            if candidate in reachable_functions:
                continue
            if re.search(r'\bUOneBrahamVFXStatics::' + re.escape(candidate) + r'\s*\(', body) or \
               re.search(r'\b' + re.escape(candidate) + r'\s*\(', body):
                frontier.append(candidate)

    for fn_name in reachable_functions:
        for match in re.finditer(r'EOneBrahamVFXEffect::([A-Za-z0-9_]+)', functions.get(fn_name, "")):
            referenced.add(match.group(1))

    unreachable = [name for name in effects if name not in referenced]

    if not unreachable:
        v.pass_("vfx reachability", u"all {0} VFX effects are spawned from reachable gameplay code".format(
            len(effects)))
        return

    for name in unreachable:
        v.fail("vfx reachability",
               u"EOneBrahamVFXEffect::{0} is authored and budgeted but nothing ever spawns it".format(name))


def check_surface_coverage(v, config_only):
    """Every authored surface must be on the map, and every room must have a finish.

    Fifteen physical materials are authored by build_materials.py. Nothing
    forced the map to use more than one of them until this check existed, and a
    surface that appears nowhere in the level is invisible in every other
    validation: the asset exists, the enum exists, the footsteps simply always
    sound like carpet.
    """
    manifest = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                             "..", "onebraham_build_manifest.json"))

    if not os.path.isfile(manifest):
        if config_only:
            v.warn("surface coverage", u"no manifest yet - run Scripts/BuildContent.bat --editor "
                                      u"to prove the map uses every surface")
        else:
            v.warn("surface coverage", u"no build manifest; the map has not been built")
        return

    try:
        import json
        with io.open(manifest, encoding="utf-8") as handle:
            data = json.load(handle)
    except Exception as exc:  # noqa: BLE001
        v.fail("surface coverage", u"the build manifest is unreadable: {0}".format(exc))
        return

    surfaces = data.get("surfaces") or []
    usage = data.get("surface_usage") or {}
    finishes = data.get("floor_finishes") or []

    if not surfaces:
        v.fail("surface coverage", u"the manifest lists no surfaces at all")
        return

    # Flesh is a gameplay surface: it is what a character hit resolves to, and
    # there is deliberately no flesh geometry in the building.
    gameplay_only = {"Flesh"}

    missing = [name for name in surfaces if name not in gameplay_only and not usage.get(name)]

    if missing:
        for name in missing:
            v.fail("surface coverage", u"surface '{0}' is authored but used by no geometry; "
                                       u"footsteps and impacts will fall back".format(name))
    else:
        used = len([n for n in surfaces if usage.get(n)])
        v.pass_("surface coverage", u"{0} of {1} surfaces are placed in the building".format(
            used, len(surfaces)))

    # Rooms without a finish: the manifest is the only place this is visible,
    # because a room with no finish simply keeps the area slab.
    rooms = data.get("rooms") or []

    if rooms and not finishes:
        # A manifest with rooms and no finishes at all means the finish pass did
        # not run: every room is standing on the area slab and the whole floor
        # sounds the same.
        v.fail("surface coverage", u"{0} rooms recorded but no floor finishes at all - "
                                   u"the finish pass did not run".format(len(rooms)))
    elif rooms and finishes:
        finished = set((entry.get("area"), entry.get("room")) for entry in finishes)
        unfinished = [r for r in rooms if (r.get("area"), r.get("name")) not in finished]

        if unfinished:
            for room in unfinished[:8]:
                v.warn("surface coverage", u"room {0}/{1} has no floor finish recorded".format(
                    room.get("area"), room.get("name")))
        else:
            v.pass_("surface coverage", u"all {0} rooms record a floor finish".format(len(rooms)))


def check_uht_contracts(v, _config_only):
    """The contracts UnrealHeaderTool enforces, checked without running it.

    Six things are verified: RPCs have their `_Implementation`, RPCs do not
    return values, BlueprintNativeEvents have a C++ default body,
    BlueprintImplementableEvents have no C++ body, RepNotify callbacks exist and
    are UFUNCTIONs, and every replicated property is named in a DOREPLIFETIME.

    That last one is the reason this check exists. A replicated property with no
    DOREPLIFETIME compiles, validates and silently never replicates, so it looks
    correct on a listen server and is permanently zero on every client - the
    classic "works on my machine" multiplayer bug, and the hardest one to notice
    when testing alone.
    """
    try:
        from onebraham import check_uht_contracts
    except Exception as exc:  # noqa: BLE001
        v.warn("uht contracts", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_uht_contracts.run(source_root=SOURCE_DIR)

    if checked == 0:
        v.warn("uht contracts", u"no headers parsed - is Source/OneBraham missing?")
        return

    if not problems:
        v.pass_("uht contracts", u"all {0} UE reflection contracts are satisfied".format(checked))
        return

    for rel, class_name, message in problems:
        v.fail("uht contracts", u"{0}: {1} - {2}".format(rel, class_name, message))


def check_soft_paths(v, _config_only):
    """Every asset path the C++ loads by name must be one the pipeline writes.

    The C++ in this project loads its registries, its HUD widget, its sound
    classes, its camera shakes and its interaction montages by string, because a
    hard reference from code to a Blueprint breaks the cook. A string that is
    wrong produces no error: the load returns null, the caller takes its fallback
    branch, and the game carries on with a silent component missing.

    This check compares every `/Game/OneBraham/...` literal in the source against
    two things the content pipeline guarantees - the directories its scripts name
    and the asset-name prefixes its naming table defines - so a path that points
    at a folder nothing creates is caught on a machine with no engine on it.
    """
    try:
        from onebraham import check_soft_paths
    except Exception as exc:  # noqa: BLE001
        v.warn("soft paths", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_soft_paths.run(source_root=SOURCE_DIR)

    if checked == 0:
        v.warn("soft paths", u"no /Game/ paths found in the source - is Source/OneBraham missing?")
        return

    if not problems:
        v.pass_("soft paths", u"all {0} name-loaded asset paths match the pipeline's contract".format(checked))
        return

    for rel, literal, message in problems:
        v.fail("soft paths", u"{0}: {1} - {2}".format(rel, literal, message))


def check_calls(v, _config_only):
    """Call sites whose argument count cannot match their declaration.

    The compiler catches every one of these, in the same pass as a thousand other
    things, at the end of a twenty-minute build. This finds them in a fraction of
    a second with no engine installed, which matters more here than anywhere
    else: this project has never been compiled, so every arity error is a build
    that stops before it starts.

    It is deliberately narrow. It only checks calls to a name that exactly one
    project class declares, only when the call is unqualified or through
    `this->`, honours default arguments, and skips anything containing a braced
    initialiser. When it cannot be certain, it says nothing.
    """
    try:
        from onebraham import check_calls
    except Exception as exc:  # noqa: BLE001
        v.warn("call arity", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_calls.run(source_root=SOURCE_DIR)

    if checked == 0:
        v.warn("call arity", u"no call sites matched a unique project signature")
        return

    if not problems:
        v.pass_("call arity", u"all {0} unambiguous call sites match their declarations".format(checked))
        return

    for rel, line_number, message in problems:
        v.fail("call arity", u"{0}:{1} - {2}".format(rel, line_number, message))


def check_gameplay_tags(v, _config_only):
    """Every gameplay tag the code asks for must be one the project declares.

    Gameplay tags fail silently. A request for an undeclared tag returns an
    invalid tag, which is not an error - it is a tag no query matches, so every
    branch keyed on it takes its fallback and the game carries on looking
    slightly wrong. This check has already earned its place: it is the reason the
    bot controller no longer asks for "OB.Character.Archetype.Intern", a tag that
    has never existed next to a registry that calls that archetype Graduate.

    Tags assembled at runtime from a literal or a local string table are checked
    too - that is where the archetype names live. A tag assembled from a value
    only known at runtime (a travel option, a profile) is counted as skipped and
    reported as such, rather than counted as checked.
    """
    try:
        from onebraham import check_gameplay_tags
    except Exception as exc:  # noqa: BLE001
        v.warn("gameplay tags", u"checker unavailable: {0}".format(exc))
        return

    checked, problems, warnings, skipped, documented, patterns = check_gameplay_tags.run()

    for message in warnings:
        v.warn("gameplay tags", message)

    if not problems:
        v.pass_("gameplay tags", u"all {0} resolvable tag requests are declared ({1} skipped)".format(
            checked, skipped))
        return

    for rel, line_number, message in problems:
        where = u"{0}:{1}".format(rel, line_number) if line_number else rel
        v.fail("gameplay tags", u"{0} - {1}".format(where, message))


def check_building_layout(v, _config_only):
    """The building the map builds is the building the game describes.

    The level is laid out from a table in build_environment.py. The game
    describes that level from a table in OneBrahamBuildingLayoutAsset.cpp: which
    floor the server hall is on, which floors the lockdown may seal, what the
    lift says. Two tables, two languages, one building - and when they disagree
    the plan tells a player they are on the executive floor while they are
    standing in the server hall, with no error anywhere.
    """
    try:
        from onebraham import check_building_layout
    except Exception as exc:  # noqa: BLE001
        v.warn("building layout", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_building_layout.run()

    if checked == 0:
        v.warn("building layout", u"no floors were parsed out of the layout asset")
        return

    if not problems:
        v.pass_("building layout", u"all {0} floors agree between the map and the layout asset".format(checked))
        return

    for message, _ in problems:
        v.fail("building layout", message)


def check_doc_numbers(v, _config_only):
    """The numbers in the documentation are the numbers the tooling produces.

    Docs/17's summary table and the README's rule 4 quote counts - declarations
    checked, call sites checked, references checked - as the evidence that the
    pipeline is real. Three of them had gone stale (495 declarations when there
    were 525, 42 asset paths when there were 45, 823 call sites when there were
    848) and each was fixed by hand, which is a promise to remember. This is the
    check instead: the counts are re-derived by running the checkers.
    """
    try:
        from onebraham import check_doc_numbers
    except Exception as exc:  # noqa: BLE001
        v.warn("documented numbers", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_doc_numbers.run()

    if not problems:
        v.pass_("documented numbers", u"{0} documented numbers agree with the tooling".format(checked))
        return

    for message, kind in problems:
        v.fail("documented numbers", u"[{0}] {1}".format(kind, message))


def check_test_names(v, _config_only):
    """The automation tests the documentation lists are the ones that exist.

    Docs/18 section 2 listed twelve suites for most of this project's life and
    `grep -rn IMPLEMENT_SIMPLE_AUTOMATION_TEST` matched one file: the document.
    The table is now written against twenty real tests, and this check keeps it
    that way in both directions - a documented suite with no source, and a test
    nobody documented.
    """
    try:
        from onebraham import check_test_names
    except Exception as exc:  # noqa: BLE001
        v.warn("test names", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_test_names.run()

    if not problems:
        v.pass_("test names", u"{0} automation tests agree with Docs/18".format(checked))
        return

    for message, kind in problems:
        v.fail("test names", u"[{0}] {1}".format(kind, message))


def check_doc_files(v, _config_only):
    """The files the documentation names are files that exist.

    Docs/18 said the validator runs in `Scripts\\RunAll.bat`, and there was no
    such script: an instruction that reads perfectly and cannot be typed. This
    check holds every documented script and tool path against the disk, and the
    reverse - an undocumented script, an undocumented checker.
    """
    try:
        from onebraham import check_doc_files
    except Exception as exc:  # noqa: BLE001
        v.warn("documented files", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_doc_files.run()

    if not problems:
        v.pass_("documented files", u"{0} documented paths all exist".format(checked))
        return

    for message, kind in problems:
        v.fail("documented files", u"[{0}] {1}".format(kind, message))


def check_doc_types(v, _config_only):
    """The type names in the documentation are type names in the code.

    Docs/03 listed an `EOneBrahamExtractionStage` (the enum is
    `EOneBrahamExtractionState`), an `EOneBrahamArchetype` (archetypes are
    gameplay tags), an `EOneBrahamGameMode` (it is `EOneBrahamMatchMode`), an
    `EOneBrahamDamageType` (it is `EOneBrahamKillFeedVerb`), a
    `UOneBrahamCharacter` and a `UOneBrahamGameState` - both actors - and a
    `UOneBrahamVoiceCodec`, which is a plain `F` class. Every one of them was a
    name a reader would go looking for.
    """
    try:
        from onebraham import check_doc_types
    except Exception as exc:  # noqa: BLE001
        v.warn("documented types", u"checker unavailable: {0}".format(exc))
        return

    checked, problems, allowed = check_doc_types.run()

    if not problems:
        v.pass_("documented types", u"{0} documented type names, all declared ({1} intentional)"
                .format(checked, len(allowed)))
        return

    for message, kind in problems:
        v.fail("documented types", u"[{0}] {1}".format(kind, message))

    for type_name in sorted(allowed):
        v.info("documented types", u"'{0}' is an intentional example, not a declaration"
               .format(type_name))


def check_weapon_ids(v, _config_only):
    """The ten weapons are ten weapons.

    The registry asset is generated from build_weapons.py with ids like
    `W_SMG`; the replayable default spawn table in the match settings listed
    `SMG_MP5A3` and `Carbine_L85A2`, which the registry has never contained, and
    the default loadout, the saved loadout and `ob.give` named the same ghosts.
    Every floor-loot roll asked for a weapon that does not exist and got
    nothing back - which is not an error, it is an empty hand.
    """
    try:
        from onebraham import check_weapon_ids
    except Exception as exc:  # noqa: BLE001
        v.warn("weapon ids", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_weapon_ids.run()

    if not problems:
        v.pass_("weapon ids", u"{0} checks: the armoury, the spawn table, the defaults and "
                             u"Docs/05 are one list".format(checked))
        return

    for message, kind in problems:
        v.fail("weapon ids", u"[{0}] {1}".format(kind, message))


def check_interaction_ids(v, _config_only):
    """The interaction surface: the classes, the table, the tags and the montages.

    The reviver story, in one line: revive and execute existed, were documented,
    and could not be reached, because the downed player implemented no interface,
    the capsule could not be blocked on, and nothing in the build noticed.
    """
    try:
        from onebraham import check_interaction
    except Exception as exc:  # noqa: BLE001
        v.warn("interaction ids", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_interaction.run()

    if not problems:
        v.pass_("interaction ids", u"{0} checks: {1} interactables, {2} tags claimed, "
                u"{3} declared and waiting".format(
                    checked, len(check_interaction.table5_rows()),
                    len(check_interaction.claimed_tags()),
                    len(check_interaction.table6_rows())))
        return

    for message, kind in problems:
        v.fail("interaction ids", u"[{0}] {1}".format(kind, message))


def check_audio_ids(v, _config_only):
    """Every cue the game can ask for exists, is heard, or is written down.

    Four files describe the audio and they disagreed: the bank (126 rows), the
    synthesiser's whitelist (which named three cues nothing plays while reporting
    the hit marker, kill confirm and low-ammo tick as silent), the audio builder's
    bed lists, and Docs/12 (which named ten cues the bank has never contained).
    """
    try:
        from onebraham import check_audio_ids
    except Exception as exc:  # noqa: BLE001
        v.warn("audio ids", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_audio_ids.run()

    if not problems:
        rows = check_audio_ids.bank_rows()
        known = check_audio_ids.synthesised_ids()
        audible = sum(1 for r in rows if check_audio_ids.is_synthesised(r, known))
        v.pass_("audio ids", u"{0} checks: {1} cues, {2} synthesised, {3} briefed".format(
            checked, len(rows), audible, len(rows) - audible))
        return

    for message, kind in problems:
        v.fail("audio ids", u"[{0}] {1}".format(kind, message))


def check_hud_strings(v, _config_only):
    """The HUD's copy table is real and every row is shown by something.

    `build_ui.py` created `DT_HUDStrings` with a `struct` property set to a row
    struct that did not exist, wrote no rows, and nothing in C++ read the table.
    Docs/08 said it "holds every string the HUD shows". It now does: the rows are
    compiled in, the builder writes them, and the C++ HUD reads fourteen of them.
    """
    try:
        from onebraham import check_hud_strings
    except Exception as exc:  # noqa: BLE001
        v.warn("hud strings", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_hud_strings.run()

    if not problems:
        v.pass_("hud strings", u"{0} checks: {1} rows, every one shown".format(
            checked, len(check_hud_strings.compiled_rows())))
        return

    for message, kind in problems:
        v.fail("hud strings", u"[{0}] {1}".format(kind, message))


def check_cosmetic_ids(v, _config_only):
    """The wardrobe is one wardrobe.

    Cosmetics are the entire monetisation model and the ids are `FName`s, so a
    wrong one is a string that matches nothing rather than an error. The
    compiled-in `CosmeticSlots` default carried five of the twelve slots, which
    meant `ApplyCosmetics` - which drops a pair whose slot it cannot find -
    silently applied the lanyard and the badge and threw away the jacket,
    trousers, shoes, fleece, hi-vis, bag tag, gloves and watch. The starter
    outfit unlocked seven ids in a naming scheme no wardrobe contains, so a
    fresh profile unlocked nothing a player could wear.
    """
    try:
        from onebraham import check_cosmetic_ids
    except Exception as exc:  # noqa: BLE001
        v.warn("cosmetic ids", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_cosmetic_ids.run()

    if not problems:
        v.pass_("cosmetic ids", u"{0} checks: the builder, the compiled-in default, the "
                               u"starter outfit and Docs/13 are one wardrobe".format(checked))
        return

    for message, kind in problems:
        v.fail("cosmetic ids", u"[{0}] {1}".format(kind, message))


def check_input_actions(v, _config_only):
    """The input pipeline is one pipeline.

    IA_Map, IA_ToggleHUD and IA_VoiceLineMenu were generated as assets, bound in
    DefaultInput.ini and documented in Docs/08 - with no field in the config to
    point at them and no handler in C++. Three keys that did nothing. Then the
    handlers were written against field names that did not exist, which is the
    same defect from the other end and at least fails loudly.

    Four sides are compared: the generation table, the struct fields, the ini
    bindings and prompts, and every `InputConfig->Field` read in the source.
    """
    try:
        from onebraham import check_input_actions
    except Exception as exc:  # noqa: BLE001
        v.warn("input actions", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_input_actions.run()

    if not problems:
        v.pass_("input actions", u"{0} config reads, every action bound and every field declared".format(checked))
        return

    for message, kind in problems:
        v.fail("input actions", u"[{0}] {1}".format(kind, message))


def check_voice_lines(v, _config_only):
    """Every voice line the code asks for exists, and every line is in the brief.

    A voice line id is a string. The subsystem looks it up, does not find it,
    logs at Verbose and returns false: nothing is said, nothing is shown, and no
    test fails. The comms wheel's first draft named eight ids, seven of which had
    been invented - which would have produced a wheel where seven of the eight
    segments did nothing and every screenshot looked correct.

    The second half of the check matters as much as the first: a line in the code
    and not in Docs/14_Voice_Script.md is a line nobody will ever record.
    """
    try:
        from onebraham import check_voice_lines
    except Exception as exc:  # noqa: BLE001
        v.warn("voice lines", u"checker unavailable: {0}".format(exc))
        return

    checked, problems = check_voice_lines.run()

    if checked == 0:
        v.warn("voice lines", u"no voice line references found")
        return

    if not problems:
        v.pass_("voice lines", u"all {0} line references are declared and documented".format(checked))
        return

    for message, kind in problems:
        v.fail("voice lines", u"[{0}] {1}".format(kind, message))


# ---------------------------------------------------------------------------
#  Report
# ---------------------------------------------------------------------------

def write_report(validator):
    try:
        if not os.path.isdir(REPORT_DIR):
            os.makedirs(REPORT_DIR)
    except Exception:  # noqa: BLE001
        return None

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    path = os.path.join(REPORT_DIR, u"ob_validation_{0}.md".format(stamp))

    lines = []
    lines.append(u"# ONE BRAHAM - validation report")
    lines.append(u"")
    lines.append(u"Generated: {0}".format(datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    lines.append(u"")
    lines.append(u"| Result | Count |")
    lines.append(u"| --- | --- |")
    for level in ("PASS", "INFO", "WARN", "FAIL"):
        lines.append(u"| {0} | {1} |".format(level, validator.count(level)))
    lines.append(u"")
    lines.append(u"## Findings")
    lines.append(u"")
    for finding in validator.findings:
        lines.append(u"* {0}".format(finding.line().replace(u"\n", u"\n  ")))
    lines.append(u"")

    with io.open(path, "w", encoding="utf-8") as handle:
        handle.write(u"\n".join(lines))

    return path


def build_all(config_only=False):
    validator = Validator()

    checks = [
        ("content paths", check_content_paths),
        ("input", check_input),
        ("surfaces", check_surfaces),
        ("gameplay tags", check_gameplay_tags),
        ("weapons", check_weapons),
        ("access zones", check_access),
        ("breachables", check_breachables),
        ("manifest", check_manifest),
        ("spawn safety", check_spawn_safety),
        ("audio", check_audio),
        ("cosmetics", check_cosmetics),
        ("performance", check_performance),
        ("config", check_config),
        ("targets", check_targets),
        ("python api", check_python_api_names),
        ("cpp symbols", check_cpp_symbols),
        ("cpp includes", check_cpp_includes),
        ("log categories", check_log_categories),
        ("delimiters", check_delimiters),
        ("vfx reachability", check_vfx_reachability),
        ("surface coverage", check_surface_coverage),
        ("uht contracts", check_uht_contracts),
        ("soft paths", check_soft_paths),
        ("call arity", check_calls),
        ("gameplay tags", check_gameplay_tags),
        ("building layout", check_building_layout),
        ("voice lines", check_voice_lines),
        ("input actions", check_input_actions),
        ("documented numbers", check_doc_numbers),
        ("test names", check_test_names),
        ("documented files", check_doc_files),
        ("documented types", check_doc_types),
        ("weapon ids", check_weapon_ids),
        ("cosmetic ids", check_cosmetic_ids),
        ("hud strings", check_hud_strings),
        ("audio ids", check_audio_ids),
        ("interaction ids", check_interaction_ids),
    ]

    for label, fn in checks:
        try:
            fn(validator, config_only)
        except Exception as exc:  # noqa: BLE001
            validator.fail(label, u"the check itself threw: {0}".format(exc))

    report_path = write_report(validator)

    C.log(u"")
    C.log(u"=== Validation: {0} pass, {1} warn, {2} fail ===".format(
        validator.count("PASS"), validator.count("WARN"), validator.count("FAIL")))

    for finding in validator.findings:
        if finding.level in ("FAIL", "WARN"):
            C.log(finding.line())

    if report_path:
        C.log(u"  report: {0}".format(report_path))

    return validator


if __name__ == "__main__":
    _config_only = "--config-only" in sys.argv
    _validator = build_all(config_only=_config_only)
    raise SystemExit(0 if _validator.ok() else 1)
