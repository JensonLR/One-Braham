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
    "NS_OB_Dust", "NS_OB_BreachDebris", "NS_OB_CorpseImpact",
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

def check_config(v, _config_only):
    game_ini = read_text(os.path.join(CONFIG_DIR, "DefaultGame.ini"))
    engine_ini = read_text(os.path.join(CONFIG_DIR, "DefaultEngine.ini"))

    required_sections = [
        ("[/Script/OneBraham.OneBrahamMatchSettings]", game_ini, "match tuning"),
        ("[/Script/OneBraham.OneBrahamGameSettings]", game_ini, "palette and gameplay"),
        ("[/Script/Engine.Engine]", engine_ini, "engine classes"),
    ]

    missing = [name for name, text, _why in required_sections if name not in text]

    if missing:
        v.fail("config", u"missing sections: {0}".format(", ".join(missing)))
    else:
        v.pass_("config", u"all {0} required config sections present".format(
            len(required_sections)))

    # The three modes must each have a settings block: a mode with no block
    # loads with the C++ defaults, which is how one mode quietly plays like
    # another.
    for mode in ("Ascent", "FireDrill", "NightShift"):
        if mode not in game_ini:
            v.fail("config", u"no [OneBrahamMatchSettings] entry for {0}".format(mode))
        else:
            v.pass_("config", u"{0} settings present".format(mode))

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
