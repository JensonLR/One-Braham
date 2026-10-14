# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_environment
===========================

Builds One Braham: fifteen floors, eight named areas, circulation, spawn points,
gameplay anchors, lighting and audio zoning - procedurally, from the numbers in
common.py.

WHY PROCEDURAL
    The alternative is a hand-modelled 15-floor tower. That is six months of
    level art before anybody can play the mode, and every design change ("the
    server floor wants a second stairwell") is a rebuild. This script produces a
    complete, playable, lit, navigable building in one command, with every
    gameplay anchor named and logged. The art pass then replaces *meshes* per
    room, room by room, without touching the layout, the collision, the nav mesh
    or the gameplay.

WHAT IT CREATES
    L_Persistent_OneBraham         the shell: exterior, ground, sky, persistent
                                   gameplay actors, and the streaming references
    L_OneBraham_F01 .. F15         one streaming level per floor
    L_OneBraham_Core               lifts, stair cores, risers (shared across floors)
    L_OneBraham_Skydeck            the deck itself: pad, winch, plant, edge rails
    MI_* material instances        one per surface type, driven by the palette
    AOneBrahamSpawnPoint actors    three per area, validated for distance safety
    AOneBrahamPlayerStart-equivalent anchors are the spawn points above; the
                                   game mode's own selector consumes them.

GEOMETRY IS REAL
    Every piece is a mesh actor with collision and a physical material matching
    its surface type, so footsteps, bullet impacts, occlusion and the nav mesh are
    all correct from the first run. The blockout uses engine primitives, which is
    how the building is playable on day one and how the art pass can be dropped in
    piecemeal.

USAGE
    Headless:
        UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
            -script="Tools/Python/onebraham/build_environment.py"
    In the editor console:
        import onebraham.build_environment as be; be.build_all()
"""

from __future__ import print_function

import json
import math
import os

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

# ---------------------------------------------------------------------------
#  Engine primitives
# ---------------------------------------------------------------------------
CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"        # 100 x 100 x 100 cm
PLANE_MESH = "/Engine/BasicShapes/Plane.Plane"     # 100 x 100 cm, +Z
CYLINDER_MESH = "/Engine/BasicShapes/Cylinder.Cylinder"   # 100 dia x 100 tall
SPHERE_MESH = "/Engine/BasicShapes/Sphere.Sphere"

# One material per surface the game distinguishes. The names are the *gameplay*
# names: EOneBrahamFootstepSurface resolves from the physical material, and the
# impact/footstep cue is chosen from that. Getting this list right is what makes
# a marble lobby sound different from a carpeted office.
SURFACES = [
    # name,            colour key,    roughness, physical material
    ("Marble",         "LightGrey",   0.15),
    ("Carpet",         "Navy",        0.90),
    ("Concrete",       "LightGrey",   0.75),
    ("Metal",          "Primary",     0.35),
    ("Glass",          "SignalBlue",  0.05),
    ("Wood",           "Navy",        0.55),
    ("Puddle",         "SignalBlue",  0.05),
    ("Grating",        "Metal",       0.40),
    ("Fabric",         "Primary",     0.95),
    ("ServerRack",     "Navy",        0.45),
    ("Plasterboard",   "White",       0.85),
    ("Lino",           "LightGrey",   0.30),
    ("Decking",        "Metal",       0.60),
    ("BallisticGlass", "SignalBlue",  0.08),
]

# Every physical material maps to a surface-type enum index in the engine's
# project settings (Config/DefaultEngine.ini), so this table has to stay in the
# same order as EOneBrahamFootstepSurface in Core/OneBrahamTypes.h.
SURFACE_ORDER = [
    "Marble", "Carpet", "Concrete", "Metal", "Glass", "Wood", "Puddle",
    "Grating", "Fabric", "ServerRack", "Plasterboard", "Lino", "Decking",
    "Flesh", "BallisticGlass",
]

# ---------------------------------------------------------------------------
#  Room floor finishes
#
#  WHY THIS TABLE EXISTS
#      Fifteen physical materials are authored, and the map has to actually use
#      them. A surface that exists as an asset and appears nowhere in the level
#      is fifteen materials and one gameplay surface (carpet), which shows up as
#      "every room sounds the same" and "shooting a server rack does nothing".
#      The finish is what the player's footsteps, bullet impacts and the impact
#      VFX family resolve from, so each room gets the finish a real building
#      would have.
#
#  Keys are room names as they appear in ROOM_PROGRAMMES. Anything not listed
#  falls back to its area's default finish below, so adding a room never means
#  adding a material by hand.
# ---------------------------------------------------------------------------
ROOM_FLOOR_FINISH = {
    # Ground Plaza: a reception floor, a wet bike store, a loading bay.
    ("Ground_Plaza", "Reception_Island"): "Marble",
    ("Ground_Plaza", "Security_Desk"):    "Lino",
    ("Ground_Plaza", "Bike_Store"):       "Puddle",
    ("Ground_Plaza", "Loading_Bay"):      "Concrete",
    ("Ground_Plaza", "Turnstile_Bank_1"): "Marble",
    ("Ground_Plaza", "Turnstile_Bank_2"): "Marble",

    # Lobby: the polished arrival floor, then carpet in the lounge.
    ("Lobby", "Reception_Desk"):  "Marble",
    ("Lobby", "Lift_Lobby"):      "Marble",
    ("Lobby", "Waiting_Seating"): "Carpet",
    ("Lobby", "Coffee_Point"):    "Lino",
    ("Lobby", "Under_Stair_Store"): "Concrete",

    # Mezzanine: the bridge over the lobby is stone, the breakout carpet.
    ("Mezzanine", "Breakout_Area"):   "Carpet",
    ("Mezzanine", "Bridge_To_Core"):  "Lino",
    ("Mezzanine", "Vending_Alcove"):  "Lino",

    # Open plan: desk farms on carpet tile, acoustic pods, plant on grating.
    ("OpenPlan", "Desk_Farm_A"):      "Carpet",
    ("OpenPlan", "Desk_Farm_B"):      "Carpet",
    ("OpenPlan", "Meeting_Pod_1"):    "Fabric",
    ("OpenPlan", "Meeting_Pod_2"):    "Fabric",
    ("OpenPlan", "Break_Out_Kitchen"): "Lino",
    ("OpenPlan", "Print_Alcove"):     "Lino",

    # Server and comms: a raised perforated rack floor, not carpet. This is the
    # finish that makes a firefight on floor 9 throw sparks.
    ("ServerComms", "Server_Row_1"):   "ServerRack",
    ("ServerComms", "Server_Row_2"):   "ServerRack",
    ("ServerComms", "Cold_Aisle"):     "ServerRack",
    ("ServerComms", "NOC"):            "ServerRack",
    ("ServerComms", "Power_Room"):     "Grating",
    ("ServerComms", "Comms_Cupboard"): "Grating",

    # Executive: boardroom and bar in dark wood, offices on carpet.
    ("Executive", "Boardroom"):     "Wood",
    ("Executive", "Corner_Office"): "Carpet",
    ("Executive", "Exec_PA_Desk"):  "Carpet",
    ("Executive", "West_Lounge"):   "Carpet",

    # Canteen: lino throughout, and a kitchen floor that is always wet.
    ("Canteen", "Canteen_Hall"): "Lino",
    ("Canteen", "Servery"):      "Lino",
    ("Canteen", "Kitchen"):      "Puddle",
    ("Canteen", "Terrace"):      "Decking",

    # Skydeck: plant deck, the pad, the rails, and a lino control room.
    ("Skydeck", "Helipad"):         "Decking",
    ("Skydeck", "Edge_Rail_North"): "Decking",
    ("Skydeck", "Edge_Rail_South"): "Decking",
    ("Skydeck", "Winch_Control"):   "Lino",
    ("Skydeck", "Plant_Room"):      "Grating",
    ("Skydeck", "Bar_Pavilion"):    "Wood",
}

# The finish a room gets when it is not named above: the area's own floor.
AREA_DEFAULT_FINISH = {
    "Ground_Plaza":  "Concrete",
    "Lobby":         "Marble",
    "Mezzanine":     "Carpet",
    "OpenPlan":      "Carpet",
    "ServerComms":   "ServerRack",
    "Executive":     "Carpet",
    "Canteen":       "Lino",
    "Skydeck":       "Decking",
}

# ---------------------------------------------------------------------------
#  Room programmes per area
# ---------------------------------------------------------------------------
# Each room is (name, x, y, size_x, size_y) in cm, in the area's local frame.
# These are the rooms that give each area its shape and its fights: a lobby with
# a reception desk island and four sightline-breaking columns, an open-plan floor
# with a break-out area and two meeting pods, a server floor with four rows and a
# cold aisle, and an executive floor with a boardroom and a corner office.
ROOM_PROGRAMMES = {
    "Ground_Plaza": [
        ("Reception_Island", 0, 0, 700, 400),
        ("Turnstile_Bank_1", -900, 600, 500, 160),
        ("Turnstile_Bank_2", 900, 600, 500, 160),
        ("Security_Desk", 0, 900, 600, 300),
        ("Bike_Store", -1700, -1200, 900, 600),
        ("Loading_Bay", 1500, -1300, 1400, 700),
    ],
    "Lobby": [
        ("Reception_Desk", 0, 300, 900, 350),
        ("Waiting_Seating", 0, -700, 1200, 400),
        ("Coffee_Point", 1100, 0, 500, 350),
        ("Lift_Lobby", 0, 0, 900, 500),
        ("Under_Stair_Store", -1300, -1000, 500, 400),
    ],
    "Mezzanine": [
        ("Breakout_Area", 0, 0, 1400, 800),
        ("Bridge_To_Core", 0, 900, 500, 500),
        ("Vending_Alcove", 1200, -900, 400, 350),
    ],
    "OpenPlan": [
        ("Desk_Farm_A", -900, -700, 1400, 1100),
        ("Desk_Farm_B", 900, -700, 1400, 1100),
        ("Meeting_Pod_1", -900, 900, 600, 500),
        ("Meeting_Pod_2", 900, 900, 600, 500),
        ("Break_Out_Kitchen", 0, 1300, 900, 500),
        ("Print_Alcove", -1700, 0, 450, 350),
    ],
    "ServerComms": [
        ("Cold_Aisle", 0, -600, 900, 1200),
        ("Server_Row_1", -700, -600, 200, 1200),
        ("Server_Row_2", 700, -600, 200, 1200),
        ("NOC", 0, 700, 1200, 600),
        ("Power_Room", -1000, 900, 500, 450),
        ("Comms_Cupboard", 1000, 900, 350, 320),
    ],
    "Executive": [
        ("Boardroom", 0, 0, 1600, 1000),
        ("Corner_Office", -1000, 900, 700, 600),
        ("Exec_PA_Desk", 900, 900, 500, 400),
        ("West_Lounge", 1200, -700, 700, 700),
    ],
    "Canteen": [
        ("Canteen_Hall", 0, 0, 1800, 1400),
        ("Servery", 0, 1000, 1400, 400),
        ("Kitchen", -1200, 900, 700, 500),
        ("Terrace", 1300, 300, 600, 900),
    ],
    "Skydeck": [
        ("Helipad", 0, 0, 1800, 1800),
        ("Plant_Room", 1800, 0, 700, 900),
        ("Winch_Control", -1500, 400, 600, 500),
        ("Bar_Pavilion", 0, 1500, 1200, 600),
        ("Edge_Rail_North", 0, 1900, 4200, 120),
        ("Edge_Rail_South", 0, -1900, 4200, 120),
    ],
}

# Rooms that are areas of open deck rather than enclosed rooms: no partitions,
# no door, and the edge rails are where the balustrade runs.
OPEN_AIR_ROOMS = {"Helipad", "Edge_Rail_North", "Edge_Rail_South"}

# Anchors: named, gameplay-relevant positions per area. The manifest this script
# writes is the authoritative list the C++ and Docs reference.
ANCHORS = {
    "Ground_Plaza": [
        ("Plaza_Spawn_West", -2100, 0),
        ("Plaza_Spawn_East", 2100, 0),
        ("Plaza_Spawn_North", 0, 1900),
    ],
    "Lobby": [
        ("Lobby_Spawn_A", -1800, -1200),
        ("Lobby_Spawn_B", 1800, -1200),
        ("Lobby_Spawn_C", 0, 1000),
    ],
    "Mezzanine": [
        ("Mezz_Spawn_A", -1500, 0),
        ("Mezz_Spawn_B", 1500, 0),
        ("Mezz_Spawn_C", 0, -1500),
    ],
    "OpenPlan": [
        ("Office_Spawn_A", -1800, -1400),
        ("Office_Spawn_B", 1800, -1400),
        ("Office_Spawn_C", -1800, 1400),
        ("Office_Spawn_D", 1800, 1400),
   ],
    "ServerComms": [
        ("Server_Spawn_A", -1300, -1000),
        ("Server_Spawn_B", 1300, -1000),
        ("Server_Spawn_C", 0, 1100),
    ],
    "Executive": [
        ("Exec_Spawn_A", -1500, -1100),
        ("Exec_Spawn_B", 1500, -1100),
        ("Exec_Spawn_C", 0, 1000),
    ],
    "Canteen": [
        ("Canteen_Spawn_A", -1600, -1200),
        ("Canteen_Spawn_B", 1600, -1200),
        ("Canteen_Spawn_C", 0, 1100),
    ],
    "Skydeck": [
        ("Deck_Spawn_West", -2000, 0),
        ("Deck_Spawn_East", 2000, 0),
        ("Deck_Spawn_North", 0, -1600),
        ("Deck_Spawn_South", 0, 1600),
    ],
}

# Access zones: the locked doors that define ASCENT's progression. Each entry is
# (zone_id, floor, tier, x, y, paired_zone_id, breachable, escalation_opens).
ACCESS_ZONES = [
    ("Core_Lift_Lobby",         1,  "Public",    0,    0,    "Core_Lift_Mezzanine", False, True),
    ("Core_Lift_Mezzanine",     2,  "Staff",     0,    0,    "Core_Lift_Lobby",     False, True),
    ("Stair_A_F03",             3,  "Staff",   -1300,  0,    None,                  True,  True),
    ("Stair_B_F03",             3,  "Staff",    1300,  0,    None,                  True,  True),
    ("Office_Floor09",          9,  "Secure",    0,   -900,  None,                  True,  True),
    ("Server_Floor09",          9,  "Secure",   -900,   0,    None,                  True,  True),
    ("Executive_Floor12",      12,  "Executive", 0,    0,    "Executive_Wing_East", True,  True),
    ("Executive_Wing_East",    12,  "Executive", 900,  900,  "Executive_Floor12",   True,  True),
    ("Canteen_Floor15",        15,  "Staff",     0,    0,    "Skydeck_Gate",        True,  False),
    ("Skydeck_Gate",           15,  "Skydeck", 900, -700,  "Canteen_Floor15",     True,  False),
]

# Override terminals: hold-to-open. Two-panel pairs are what make the co-op push
# into the Skydeck a real coordination problem.
OVERRIDE_TERMINALS = [
    ("Terminal_Office09_PanelA", 9,  -500, -1100, "Terminal_Office09_PanelB"),
    ("Terminal_Office09_PanelB", 9,   500, -1100, "Terminal_Office09_PanelA"),
    ("Terminal_Executive12",     12,   0,    700,  None),
    ("Terminal_Canteen15",       15,  -700,   0,   "Terminal_Skydeck15"),
    ("Terminal_Skydeck15",       15,   700,   0,   "Terminal_Canteen15"),
]

# Breachable walls: plasterboard and service partitions that a charge opens. Each
# is (id, floor, x, y, yoke_axis, width, height, surface).
BREACHABLE_WALLS = [
    ("Breach_Office05_Partition", 5,  0,  700, "X", 600, 400, "Plasterboard"),
    ("Breach_Server09_Riser",     9,  900, -600, "Y", 200, 400, "Plasterboard"),
    ("Breach_Exec12_StairLock",  12, -900,  0,   "X", 250, 400, "Metal"),
    ("Breach_Canteen14_Wall",    14,  0,   -900, "X", 700, 400, "Plasterboard"),
]

# Fire shutters, driven by the lockdown director. One per stairwell per floor.
SHUTTER_FLOORS = list(range(0, 14))


# ===========================================================================
#  Low-level helpers
# ===========================================================================

def _require_editor():
    if not C.IN_EDITOR:
        raise RuntimeError(
            "build_environment must run inside the Unreal Editor "
            "(UnrealEditor-Cmd.exe -run=pythonscript). It cannot run in a plain "
            "Python interpreter.")


def _actor_subsystem():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _level_subsystem():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _asset_subsystem():
    return unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def _ensure_dir(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def _load(mesh_path):
    """Loads an engine primitive, cached. Returns None and warns once if missing."""
    if mesh_path in _load.misses:
        return None
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        _load.misses.add(mesh_path)
        C.warn(u"Mesh '{0}' is not available; skipping geometry that needs it.".format(mesh_path))
    return mesh
_load.misses = set()


#: Every actor this script spawns is tagged with this prefix plus the level it
#: was built into, so a rebuild can remove exactly what an earlier run made and
#: nothing a designer placed by hand. Without it a second run stacked a second
#: building on the first.
GENERATED_TAG_PREFIX = "OB_Generated:"
_current_build_level = [""]


def _spawn(actor_class, location, rotation=None):
    # unreal.Rotator's positional order is (roll, pitch, yaw), not the
    # (pitch, yaw, roll) of FRotator in C++. Four calls here had it the C++
    # way round: the ceiling lights shone sideways, the winch console faced the
    # floor and the sun was rolled. Pass rotations by keyword.
    rot = rotation if rotation is not None else unreal.Rotator(0.0, 0.0, 0.0)
    actor = _actor_subsystem().spawn_actor_from_class(actor_class, location, rot)
    if actor is not None and _current_build_level[0]:
        try:
            tags = list(actor.tags)
            tags.append(unreal.Name(GENERATED_TAG_PREFIX + _current_build_level[0]))
            actor.tags = tags
        except Exception:  # noqa: BLE001 - a missing tag only costs a clean rebuild
            pass
    return actor


def _editor_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def _open_level_for_build(report, level_path):
    """Opens level_path for building, creating it if it does not exist, and
    removes the actors an earlier run of this script generated in it.

    Levels are created unpartitioned. World Partition streams by a 2D grid in
    XY; sixteen storeys stacked on one 52 x 44 m footprint all fall in the same
    cells, so a partitioned tower can only load all of itself or none of it,
    and a partitioned world cannot hold the per-floor sublevels at all.
    """
    level_subsystem = _level_subsystem()
    existed = unreal.EditorAssetLibrary.does_asset_exist(level_path)

    try:
        if existed:
            ok = level_subsystem.load_level(level_path)
        else:
            try:
                ok = level_subsystem.new_level(level_path, False)
            except TypeError:
                ok = level_subsystem.new_level(level_path)
    except Exception as exc:  # noqa: BLE001
        report.failed(level_path, u"could not open: {0}".format(exc))
        return False

    if ok is False:
        report.failed(level_path, u"could not {0}".format("load" if existed else "create"))
        return False

    _current_build_level[0] = level_path.rsplit("/", 1)[-1]
    tag = unreal.Name(GENERATED_TAG_PREFIX + _current_build_level[0])

    removed = 0
    for actor in list(_actor_subsystem().get_all_level_actors()):
        try:
            if tag in list(actor.tags):
                _actor_subsystem().destroy_actor(actor)
                removed += 1
        except Exception:  # noqa: BLE001
            continue

    if existed:
        report.updated_asset(level_path)
        if removed:
            C.log(u"  {0}: removed {1} actor(s) from the previous build".format(level_path, removed))
    else:
        report.created_asset(level_path)

    return True


def _save_build_level(report, level_path):
    try:
        _level_subsystem().save_current_level()
        return True
    except Exception as exc:  # noqa: BLE001
        report.failed(level_path, u"could not save: {0}".format(exc))
        return False
    finally:
        _current_build_level[0] = ""


def _attach_floor_levels(report, persistent_path, floor_paths):
    """Adds every floor as an always-loaded sublevel of the persistent level.

    Until this the floors were created and never referenced: the persistent
    map that ships held the shell and the winch, and no building. Always
    loaded, not distance-streamed: nothing in the game streams floors at
    runtime, and a floor that is not loaded has no collision to stand on.
    """
    if not _open_level_for_build(report, persistent_path):
        return 0

    world = _editor_world()
    attached = 0
    for level_path in floor_paths:
        package = level_path
        try:
            if unreal.GameplayStatics.get_streaming_level(world, unreal.Name(package)) is None:
                unreal.EditorLevelUtils.add_level_to_world(world, package, unreal.LevelStreamingAlwaysLoaded)
            attached += 1
        except Exception as exc:  # noqa: BLE001
            report.failed(level_path, u"could not attach to the persistent level: {0}".format(exc))

    _save_build_level(report, persistent_path)
    return attached


def _label(actor, label):
    if actor is None:
        return
    try:
        actor.set_actor_label(label)
    except Exception:  # noqa: BLE001 - labels are a nicety, never fatal
        pass


def _box(location, size_cm, material=None, label=None, static=True):
    """Places a cube scaled to `size_cm` as a single piece of geometry.

    A cube is 100 cm, so scale = cm / 100. Using one primitive for the blockout
    means the collision, the nav mesh, the physical material and the lightmaps
    are all correct from the first run: this is real, walkable geometry.
    """
    mesh = _load(CUBE_MESH)
    if mesh is None:
        return None

    actor = _spawn(unreal.StaticMeshActor, location)
    if actor is None:
        return None

    component = actor.static_mesh_component
    component.set_static_mesh(mesh)

    sx = size_cm[0] / 100.0
    sy = size_cm[1] / 100.0
    sz = size_cm[2] / 100.0
    actor.set_actor_scale3d(unreal.Vector(sx, sy, sz))

    if material is not None:
        component.set_material(0, material)

    if static:
        try:
            actor.set_actor_hidden_in_game(False)
        except Exception:  # noqa: BLE001
            pass

    if label:
        _label(actor, label)

    return actor


def _floor_z(floor_index):
    """World Z of a floor's finished floor level. Lobby (floor 1) is Z = 0,
    the plaza is -400, and the Skydeck is +5600, exactly as documented."""
    return (floor_index - 1) * C.FLOOR_HEIGHT_CM


def _area_for_floor(floor_index):
    """The area on a floor, as (name, centre_x, centre_y, size_x, size_y)."""
    for area in C.AREAS:
        name, (first, last), centre_x, centre_y, size_x, size_y = C.area_centre(area)
        if first <= floor_index <= last:
            return name, centre_x, centre_y, size_x, size_y
    return None, 0.0, 0.0, 0.0, 0.0


# ===========================================================================
#  Materials
# ===========================================================================

def build_materials(report):
    """Collects the material instances authored by build_materials.py.

    ONE OWNER, ONE SCRIPT. Tools/Python/onebraham/build_materials.py authors the
    master material node graphs, the fifteen physical materials and the surface
    instances; this function only RESOLVES them, because the map geometry needs a
    material per surface to hand to each actor it spawns. Two scripts writing the
    same MI_<Surface> assets is two scripts that can disagree about roughness on
    a Tuesday, and the disagreement shows up as a floor that looks wrong on one
    machine and not another.

    If the instances are missing this returns what it has and reports what is
    missing, so `--only environment` still produces a walkable map.
    """
    materials = {}

    for name, colour_key, roughness in SURFACES:
        asset_path = "{0}/MI_{1}".format(C.MATERIALS_INSTANCES, name)
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)

        if asset is None:
            report.skipped_asset(
                asset_path,
                "material instance missing - this surface will use the engine "
                "default. Run Scripts/BuildContent.bat --only materials first "
                "(or --only materials,environment).")
            continue

        materials[name] = asset

    if not materials:
        report.skipped_asset(
            C.MATERIALS + "/M_OB_Surface",
            "no surface instances at all; the blockout is walkable but grey. "
            "See Docs/10_Environment_Art.md for the master material graph.")

    C.log(u"  surface materials resolved: {0} of {1}".format(len(materials), len(SURFACES)))
    return materials


# ===========================================================================
#  Floor shell
# ===========================================================================

# Where each surface ended up, keyed by surface name. Filled while the building
# is generated and written into the manifest, so validate_project can prove that
# every authored physical material is actually on the map rather than sitting
# unused in Content/OneBraham/Materials.
_SURFACE_USAGE = {}


def _note_surface(surface_name, place):
    """Records one place a surface is used. Called by the builders."""
    if not surface_name:
        return
    _SURFACE_USAGE.setdefault(surface_name, [])
    if place not in _SURFACE_USAGE[surface_name]:
        _SURFACE_USAGE[surface_name].append(place)


# ---------------------------------------------------------------------------
#  Per-area grade
#
#  One unbound post-process volume per floor. This is the difference between
#  fourteen floors of identical exposure and a building that reads as a place:
#  the plaza is lit by a grey London sky and the server room is lit by racks, so
#  they cannot share an exposure range without one of them looking wrong.
#
#  Reflections are deliberately absent. This project runs Lumen, which builds
#  its own reflection scene; a legacy reflection capture per floor would be a
#  capture that is never read, and a second source of truth for the same thing.
#
#  Exposure is quoted in Unreal's brightness units (EV100-relative). The numbers
#  are wide on purpose: clamping exposure is what stops a player walking from the
#  plant room into the glazing and being flash-blinded for a second and a half.
# ---------------------------------------------------------------------------
AREA_GRADE = {
    # area,            min brightness, max brightness, bloom, grain, vignette
    "Ground_Plaza":  (0.35, 2.4, 0.85, 0.030, 0.42),
    "Lobby":         (0.40, 2.2, 0.75, 0.028, 0.38),
    "Mezzanine":     (0.45, 2.1, 0.70, 0.026, 0.36),
    "OpenPlan":      (0.60, 1.9, 0.55, 0.022, 0.30),
    "ServerComms":   (0.22, 1.6, 0.40, 0.034, 0.46),
    "Executive":     (0.65, 2.0, 0.60, 0.020, 0.32),
    "Canteen":       (0.70, 2.3, 0.65, 0.022, 0.30),
    "Skydeck":       (0.55, 3.6, 1.15, 0.024, 0.44),
}


def build_floor_shell(report, floor_index, materials):
    """Builds one floor: slab, ceiling, perimeter glazing, core, rooms, circulation.

    This is where the map's eight areas get their character. The programmes in
    ROOM_PROGRAMMES are the shape of every fight on this floor: a reception desk
    island is cover in the lobby, four server rows create a corridor of sightlines,
    a boardroom table is a long, brave push.
    """
    area_name, origin_x, origin_y, size_x, size_y = _area_for_floor(floor_index)
    if area_name is None:
        return []

    z = _floor_z(floor_index)
    half_x = size_x / 2.0
    half_y = size_y / 2.0

    carpet = materials.get("Carpet")
    concrete = materials.get("Concrete")
    marble = materials.get("Marble")
    glass = materials.get("BallisticGlass") or materials.get("Glass")
    plaster = materials.get("Plasterboard")
    metal = materials.get("Metal")
    decking = materials.get("Decking")

    created = []
    prefix = "F{0:02d}".format(floor_index)

    # --- Floor slab -------------------------------------------------------
    # The plaza and the deck are outdoor (concrete / decking); everything between
    # them is carpeted office space except the lobby, which is marble.
    slab_material = marble if area_name == "Lobby" else (concrete if area_name == "Ground_Plaza" else carpet)
    if area_name == "Skydeck":
        slab_material = decking or concrete

    # 20 cm of slab so floors read as floors from the stairwell, and so a round
    # that misses a stairwell rail has something to stop in.
    _box((origin_x, origin_y, z - 10.0), (size_x, size_y, 20.0), slab_material, prefix + "_Slab")
    created.append((prefix + "_Slab", (origin_x, origin_y, z - 10.0)))
    _note_surface("Marble" if area_name == "Lobby" else
                  ("Concrete" if area_name == "Ground_Plaza" else
                   ("Decking" if area_name == "Skydeck" else "Carpet")),
                  prefix + "_Slab")

    # --- Ceiling ----------------------------------------------------------
    # Not on the Skydeck: it is the roof, open to the sky, and the one floor
    # where the weather is real. It used to get the office ceiling and the
    # office glazing like every other floor, which made the ending of the game
    # a sixteenth open-plan office with a helicopter pad painted on the carpet.
    if area_name != "Skydeck":
        _box((origin_x, origin_y, z + C.CEILING_HEIGHT_CM + 10.0),
             (size_x, size_y, 20.0), plaster, prefix + "_Ceiling")
        _note_surface("Plasterboard", prefix + "_Ceiling")

    # --- Perimeter glazing (the thing that makes the tower feel like a tower) --
    # Windows are placed as a bank rather than individually: the banded look of a
    # curtain-wall office is what sells the building from inside *and* it is what
    # makes a floor edge dangerous to walk near.
    if area_name not in ("ServerComms", "Ground_Plaza", "Skydeck"):
        window_h = C.CEILING_HEIGHT_CM - 120.0
        _box((origin_x, origin_y - half_y, z + window_h / 2.0 + 60.0),
             (size_x - 200.0, 12.0, window_h), glass, prefix + "_Glazing_South")
        _box((origin_x, origin_y + half_y, z + window_h / 2.0 + 60.0),
             (size_x - 200.0, 12.0, window_h), glass, prefix + "_Glazing_North")
        _note_surface("BallisticGlass" if materials.get("BallisticGlass") is not None else "Glass",
                      prefix + "_Glazing")

    # --- Core walls (the lift core is a hole in the middle of every floor) ---
    core = C.LIFT_CORE_SIZE_CM
    core_wall = 20.0
    _box((origin_x - core / 2.0, origin_y, z + C.CEILING_HEIGHT_CM / 2.0),
         (core_wall, core, C.CEILING_HEIGHT_CM), concrete, prefix + "_Core_West")
    _box((origin_x + core / 2.0, origin_y, z + C.CEILING_HEIGHT_CM / 2.0),
         (core_wall, core, C.CEILING_HEIGHT_CM), concrete, prefix + "_Core_East")
    _box((origin_x, origin_y - core / 2.0, z + C.CEILING_HEIGHT_CM / 2.0),
         (core, core_wall, C.CEILING_HEIGHT_CM), concrete, prefix + "_Core_South")
    # The north face is left open on most floors: that is the door into the core.

    # --- Room partitions --------------------------------------------------
    rooms = ROOM_PROGRAMMES.get(area_name, [])
    for room_name, rx, ry, rsx, rsy in rooms:
        cx = origin_x + rx
        cy = origin_y + ry

        # Open-air rooms have no partitions: a helipad with plaster walls round
        # it is a box, and an "edge rail" walled in plasterboard is not an edge.
        if room_name in OPEN_AIR_ROOMS:
            continue

        # Partitions around every room except its entrance side. Entrances are
        # placed facing the core, which is what makes every room approachable from
        # two directions and never a dead end.
        wall_h = C.CEILING_HEIGHT_CM if "Server" in room_name or "Power" in room_name else 250.0
        wall_t = 15.0

        _box((cx - rsx / 2.0, cy, z + wall_h / 2.0), (wall_t, rsy, wall_h), plaster, prefix + "_" + room_name + "_W")
        _box((cx + rsx / 2.0, cy, z + wall_h / 2.0), (wall_t, rsy, wall_h), plaster, prefix + "_" + room_name + "_E")
        _box((cx, cy + rsy / 2.0, z + wall_h / 2.0), (rsx, wall_t, wall_h), plaster, prefix + "_" + room_name + "_N")

        # South wall, split around a doorway so the room is walkable.
        door_gap = C.DOOR_WIDTH_CM
        side = max(60.0, (rsx - door_gap) / 2.0)
        _box((cx - (door_gap / 2.0 + side / 2.0), cy - rsy / 2.0, z + wall_h / 2.0),
             (side, wall_t, wall_h), plaster, prefix + "_" + room_name + "_S_W")
        _box((cx + (door_gap / 2.0 + side / 2.0), cy - rsy / 2.0, z + wall_h / 2.0),
             (side, wall_t, wall_h), plaster, prefix + "_" + room_name + "_S_E")
        # Door head over the gap, so the opening reads as a doorway from a
        # distance and, more importantly, blocks line of sight over it.
        _box((cx, cy - rsy / 2.0, z + wall_h - 25.0), (door_gap, wall_t, 50.0), plaster, prefix + "_" + room_name + "_Head")

    # --- Room floor finishes ---------------------------------------------
    # The slab is one material for a whole area. The finish is the layer a room
    # actually has: it is what the footstep system reads, what the bullet impact
    # family reads (a server rack throwing sparks is this line, not a special
    # case in code), and what the impact decals are chosen from. It is placed
    # 4 mm proud of the slab so it never z-fights with it.
    for room_name, rx, ry, rsx, rsy in rooms:
        finish_name = ROOM_FLOOR_FINISH.get((area_name, room_name),
                                            AREA_DEFAULT_FINISH.get(area_name))
        finish_material = materials.get(finish_name) if materials else None

        if finish_material is None:
            # Not an error: the room keeps the area slab, which is walkable and
            # correct-looking. It is worth a note because a missing instance means
            # build_materials did not finish, and the whole map will be grey.
            report.skipped_asset(
                u"{0}/{1} floor finish".format(area_name, room_name),
                u"no MI_{0} instance; room keeps the area slab".format(finish_name))
            continue

        fx = origin_x + rx
        fy = origin_y + ry

        # Inset by the partition thickness so a finish never pokes through a wall.
        finish_size_x = max(60.0, rsx - 30.0)
        finish_size_y = max(60.0, rsy - 30.0)

        _box((fx, fy, z - 0.6), (finish_size_x, finish_size_y, 2.0),
             finish_material, "{0}_{1}_Finish".format(prefix, room_name))
        created.append(("{0}_{1}_Finish".format(prefix, room_name), (fx, fy, z - 0.6)))
        _note_surface(finish_name, "{0}/{1}".format(prefix, room_name))

    # --- Circulation: stairwells and the lift doors -----------------------
    stair_x = -1300.0
    stair_y = 0.0
    _box((origin_x + stair_x, origin_y + stair_y, z + 60.0),
         (C.STAIRWELL_SIZE_CM, C.STAIRWELL_SIZE_CM, 120.0), concrete, prefix + "_Stair_A_Landing")
    _box((origin_x - stair_x, origin_y + stair_y, z + 60.0),
         (C.STAIRWELL_SIZE_CM, C.STAIRWELL_SIZE_CM, 120.0), concrete, prefix + "_Stair_B_Landing")

    # Lift doors on the core's north face: two shafts, 110 cm doors, which is the
    # chokepoint the whole map is balanced around.
    for i, offset in enumerate((-260.0, 260.0)):
        _box((origin_x + offset, origin_y + core / 2.0, z + 110.0),
             (C.DOOR_WIDTH_CM, 20.0, 220.0), metal, "{0}_Lift_Door_{1}".format(prefix, i + 1))
    _note_surface("Metal", prefix + "_Lift_Door")
    _note_surface("Concrete", prefix + "_Stair_Landing")
    _note_surface("Plasterboard", prefix + "_Partitions")

    return created


def build_vertical_circulation(report, materials):
    """Builds the stair flights and lift shafts through all fifteen floors.

    Stairs are the spine of ASCENT: the lockdown seals them, the breach opens
    them, and the whole game is a decision about when to stop climbing. Each
    flight is a run of steps, built as real geometry so the character movement
    component's mantle and step-up logic has something honest to work with.
    """
    concrete = materials.get("Concrete")
    metal = materials.get("Metal")

    stair_x = 1300.0
    step_count = 12
    step_rise = C.FLOOR_HEIGHT_CM / step_count
    step_run = 26.0

    for floor_index in range(0, C.FLOOR_COUNT):
        z0 = _floor_z(floor_index)
        z1 = _floor_z(floor_index + 1)

        for side in (-1.0, 1.0):
            x = stair_x * side
            for i in range(step_count):
                step_z = z0 + step_rise * (i + 1)
                step_y = i * step_run
                _box((x, step_y, step_z - step_rise / 2.0),
                     (120.0, step_run, step_rise), concrete,
                     "Stair_{0}_{1}_Step_{2}".format("A" if side < 0 else "B", floor_index, i))

        # Lift shaft: an open tube with a car floor at every level, so the lifts
        # can be animated without a separate mesh.
        for shaft in (-260.0, 260.0):
            _box((shaft, 0.0, (z0 + z1) / 2.0), (C.LIFT_CAR_SIZE_CM, C.LIFT_CAR_SIZE_CM, C.FLOOR_HEIGHT_CM),
                 metal, "Lift_Shaft_{0}_{1}".format(int(shaft), floor_index))

    return True


# ===========================================================================
#  Gameplay anchors
# ===========================================================================

#: Rooms no loot is placed in: the pad and the edge rails are where a player
#: should be looking at the sky and the other squads, not the floor.
LOOT_EXCLUDED_ROOMS = {"Helipad", "Edge_Rail_North", "Edge_Rail_South"}

#: One loot point per this many square metres of room, three to sixteen a room.
LOOT_SQUARE_METRES_PER_POINT = 5.0
LOOT_POINTS_MIN, LOOT_POINTS_MAX = 3, 16

#: The first points in each room may also hold a keycard: a floor issues its
#: cards from desks people work at, two a room, never from a stairwell.
LOOT_KEYCARD_POINTS_PER_ROOM = 2

#: Bit values of EOneBrahamLootCategory (Loot/OneBrahamLootSpawnPoint.h).
LOOT_MASK_DEFAULT = 0b01111     # weapon, ammo, utility, medkit
LOOT_MASK_WITH_KEYCARD = 0b11111


def loot_points_for_room(room, centre_x, centre_y):
    """(x, y, is_keycard_point) for one room, centred on the area centre.

    A grid over the room, inset 60 cm from its walls, so the points are spread
    across the room rather than stacked in its middle. Pure, so the engine-free
    check can count what a floor will have before an editor ever runs.
    """
    name, rx, ry, size_x, size_y = room
    if name in LOOT_EXCLUDED_ROOMS:
        return []

    area_m2 = (size_x * size_y) / 10000.0
    count = int(max(LOOT_POINTS_MIN, min(LOOT_POINTS_MAX, round(area_m2 / LOOT_SQUARE_METRES_PER_POINT))))

    columns = max(1, int(math.ceil(math.sqrt(count * size_x / max(size_y, 1.0)))))
    rows = max(1, int(math.ceil(count / float(columns))))

    inset_x = min(60.0, size_x / 4.0)
    inset_y = min(60.0, size_y / 4.0)
    span_x = max(0.0, size_x - 2.0 * inset_x)
    span_y = max(0.0, size_y - 2.0 * inset_y)

    points = []
    for index in range(count):
        column = index % columns
        row = index // columns
        px = centre_x + rx - span_x / 2.0 + (column + 0.5) * span_x / columns
        py = centre_y + ry - span_y / 2.0 + (row + 0.5) * span_y / rows
        points.append((px, py, index < LOOT_KEYCARD_POINTS_PER_ROOM))

    return points


def loot_points_per_floor():
    """floor index -> number of loot points the builder will place."""
    counts = {}
    for area in C.AREAS:
        name, (first, last), cx, cy, _sx, _sy = C.area_centre(area)
        per_floor = sum(len(loot_points_for_room(room, cx, cy)) for room in ROOM_PROGRAMMES.get(name, []))
        for floor in range(first, last + 1):
            counts[floor] = per_floor
    return counts


def build_loot_points(report):
    """Places AOneBrahamLootSpawnPoint actors in every room of every floor.

    The match settings say how much loot a floor holds; these say where it may
    be. There are deliberately more points than any floor's profile asks for,
    so the same room holds its rifle on a different desk next match.
    """
    point_class = getattr(unreal, "OneBrahamLootSpawnPoint", None)
    if point_class is None:
        report.failed("OneBrahamLootSpawnPoint", "the C++ class is not available; compile the module first")
        return []

    placed = []
    for area in C.AREAS:
        name, (first, last), cx, cy, _sx, _sy = C.area_centre(area)
        for floor_index in range(first, last + 1):
            z = _floor_z(floor_index) + 2.0
            for room in ROOM_PROGRAMMES.get(name, []):
                for index, (px, py, keycard) in enumerate(loot_points_for_room(room, cx, cy)):
                    actor = _spawn(point_class, unreal.Vector(px, py, z))
                    if actor is None:
                        continue

                    label = "Loot_F{0:02d}_{1}_{2:02d}".format(floor_index, room[0], index)
                    _label(actor, label)
                    try:
                        actor.set_editor_property("FloorIndex", floor_index)
                        actor.set_editor_property("CategoryMask",
                                                  LOOT_MASK_WITH_KEYCARD if keycard else LOOT_MASK_DEFAULT)
                    except Exception as exc:  # noqa: BLE001
                        report.failed(label, str(exc))
                        continue

                    placed.append({"label": label, "floor": floor_index, "room": room[0],
                                   "location": [px, py, z], "keycard": keycard})

    C.log(u"  loot: {0} spawn points".format(len(placed)))
    return placed


def build_spawn_points(report):
    """Places and configures every spawn anchor.

    Spawn safety is a hard requirement of the map spec: no spawn may be within
    SPAWN_MIN_ENEMY_DISTANCE_CM of an enemy's spawn, and no two same-team spawns
    may be closer than SPAWN_MIN_SAME_TEAM_DISTANCE_CM. This functions places
    them, configures them, and hands the list to the manifest where
    validate_project.py checks it - a map where the first thing that happens is
    being shot in the back is a map that does not ship.
    """
    spawn_class = getattr(unreal, "OneBrahamSpawnPoint", None)
    if spawn_class is None:
        report.failed("OneBrahamSpawnPoint", "the C++ class is not available; "
                      "compile the project before running the environment build.")
        return []

    placed = []
    spawn_index = 0

    for area_name, anchors in ANCHORS.items():
        area = next((a for a in C.AREAS if a[0] == area_name), None)
        if area is None:
            continue

        floors = area[1]
        floor_span = max(1, floors[1] - floors[0] + 1)

        # The tag the game mode uses to prefer spawns in a zone. Built by
        # stripping the underscores out of the area name, which is the rule the
        # tag list in DefaultGame.ini follows (Ground_Plaza -> OB.Area.GroundPlaza).
        zone_tag_name = "OB.Area." + area_name.replace("_", "")

        for index, (anchor_name, ax, ay) in enumerate(anchors):
            # Spread the area's anchors across its floors rather than stacking
            # them all on the middle one: two anchors on the same floor 3000 cm
            # apart are one spawn, not two.
            floor_index = floors[0] + (index % floor_span)
            z = _floor_z(floor_index) + 90.0

            actor = _spawn(spawn_class, unreal.Vector(ax, ay, z))
            if actor is None:
                continue

            _label(actor, anchor_name)

            try:
                actor.set_editor_property("FloorIndex", floor_index)

                # ASCENT is three squads of four in a free-for-all: no anchor is
                # tied to a team, and the cluster id is what keeps a squad
                # together. The game mode round-robins players onto clusters 0-2.
                actor.set_editor_property("SquadClusterId", spawn_index % 3)
                actor.set_editor_property("Weight", 1.0)
                actor.set_editor_property("CooldownSeconds", 0.0)
                actor.set_editor_property("bEnabled", True)

                # Best-effort tag: if the tag list in DefaultGame.ini has not been
                # compiled into this editor session, leave it unset rather than
                # failing the build. An untagged spawn point is a spawn point the
                # game mode weights less; it is not a broken map.
                try:
                    tag = unreal.GameplayTagLibrary.request_gameplay_tag(zone_tag_name)
                    if tag is not None and tag.is_valid:
                        actor.set_editor_property("ZoneTag", tag)
                except Exception:  # noqa: BLE001
                    pass

            except Exception as exc:  # noqa: BLE001
                report.failed("spawn point " + anchor_name, str(exc))

            placed.append({
                "name": anchor_name,
                "floor": floor_index,
                "area": area_name,
                "zone_tag": zone_tag_name,
                "location": [ax, ay, z],
                "squad_cluster": spawn_index % 3,
            })
            spawn_index += 1

    # The two rules, checked here as well as in the validator: a failure at build
    # time names the anchor, which a validation report cannot.
    #
    # MEASURED ON THE SAME STOREY. The enemy rule exists to stop an opening rush
    # reaching an opposing squad before it has moved, and a squad two floors away
    # cannot do that: the slab between them is a real barrier, and the stairwells
    # are the only route. Measuring straight-line distance through a floor would
    # force spawn anchors into the corners of a floor plan and make the tower
    # worse to play in order to satisfy a number. Same storey, then, and the
    # vertical case is covered by the deploy timer and the spawn-heat scan in
    # AOneBrahamGameMode.
    import math
    problems = []

    for i, a in enumerate(placed):
        for b in placed[i + 1:]:
            if abs(a["location"][2] - b["location"][2]) >= C.FLOOR_HEIGHT_CM:
                continue

            distance = math.hypot(a["location"][0] - b["location"][0],
                                  a["location"][1] - b["location"][1])

            if a["squad_cluster"] != b["squad_cluster"] and distance < C.SPAWN_MIN_ENEMY_DISTANCE_CM:
                problems.append(u"{0} (F{1:02d}) is {2:.0f} cm from {3} (F{4:02d}), enemy rule is {5:.0f} cm".format(
                    a["name"], a["floor"], distance, b["name"], b["floor"],
                    C.SPAWN_MIN_ENEMY_DISTANCE_CM))
            elif a["squad_cluster"] == b["squad_cluster"] and distance < C.SPAWN_MIN_SAME_TEAM_DISTANCE_CM:
                problems.append(u"{0} (F{1:02d}) is {2:.0f} cm from squadmate {3} (F{4:02d})".format(
                    a["name"], a["floor"], distance, b["name"], b["floor"]))

    if problems:
        # A spawn-safety failure is a design failure, not a script failure, so it
        # is reported with every offending pair and the build continues: the map
        # is still worth looking at, and the fix is to move an anchor in ANCHORS.
        report.failed("spawn safety", u"{0} offending pair(s)".format(len(problems)))
        for problem in problems[:12]:
            C.warn(u"    " + problem)
    else:
        C.log(u"  spawn safety: all {0} anchors respect the {1:.0f} cm enemy rule".format(
            len(placed), C.SPAWN_MIN_ENEMY_DISTANCE_CM))

    C.log(u"  spawn anchors placed: {0} across {1} areas".format(len(placed), len(ANCHORS)))
    return placed


def build_access_zones(report):
    """Places one access zone component per locked door, plus its reader.

    The actor is the door: AOneBrahamAccessZone carries its own two leaves, its
    frame, its badge reader and its light, so this only has to say WHERE the door
    is and what it needs. Placing blockout geometry beside it would leave a
    solid box standing in a doorway that is supposed to open.
    """
    zone_class = getattr(unreal, "OneBrahamAccessZone", None)
    placed = []

    for zone_id, floor, tier, x, y, paired, breachable, escalation in ACCESS_ZONES:
        z = _floor_z(floor) + 110.0

        if zone_class is None:
            # Fallback blockout: no C++ class in this build, so the room still
            # needs a door-shaped object in it. Reported, because a release build
            # with this path taken is a release build without doors.
            report.skipped_asset(
                "AOneBrahamAccessZone",
                "access zone actor class is not loaded; door '" + zone_id +
                "' is blockout only. Rebuild the C++ module, then re-run this script.")
            _box((x, y, z), (C.DOOR_WIDTH_CM, 20.0, C.DOOR_HEIGHT_CM),
                 None, "Door_" + zone_id, static=False)
        else:
            zone = _spawn(zone_class, unreal.Vector(x, y, z))
            if zone is not None:
                _label(zone, zone_id)
                try:
                    zone.set_editor_property("ZoneId", unreal.Name(zone_id))
                    zone.set_editor_property("RequiredTier",
                                             getattr(unreal.OneBrahamAccessTier, tier.upper()))
                    zone.set_editor_property("FloorIndex", floor)
                    zone.set_editor_property("bBreachable", breachable)
                    zone.set_editor_property("bEscalationOpens", escalation)
                    zone.set_editor_property("PairedZoneId", unreal.Name(paired or ""))
                except Exception as exc:  # noqa: BLE001
                    report.failed("access zone " + zone_id, str(exc))

        placed.append({
            "id": zone_id,
            "floor": floor,
            "tier": tier,
            "location": [x, y, z],
            "paired": paired,
            "breachable": breachable,
            "escalation_opens": escalation,
        })

    C.log(u"  access zones placed: {0}".format(len(ACCESS_ZONES)))
    return placed


def build_override_terminals(report):
    """Places the hold-to-open terminals, including the two-panel pairs.

    AOneBrahamOverrideTerminal builds its own panel, screen, badge reader and
    alarm beacon, and it links itself to its target zone by ZoneLinkRadius, so
    the builder only sets the identity of each panel. The paired pairs are the
    design's one co-operative set piece: twelve seconds alone, three with a
    colleague on the other panel.
    """
    placed = []
    terminal_class = getattr(unreal, "OneBrahamOverrideTerminal", None)

    for terminal_id, floor, x, y, paired in OVERRIDE_TERMINALS:
        z = _floor_z(floor) + 120.0

        if terminal_class is None:
            report.skipped_asset(
                "AOneBrahamOverrideTerminal",
                "override terminal class is not loaded; panel '" + terminal_id +
                "' is blockout only. Rebuild the C++ module, then re-run this script.")
            _box((x, y, z), (60.0, 30.0, 120.0), None, "TerminalPanel_" + terminal_id, static=False)
        else:
            actor = _spawn(terminal_class, unreal.Vector(x, y, z + 20.0))
            if actor is not None:
                _label(actor, terminal_id)
                try:
                    actor.set_editor_property("TerminalId", unreal.Name(terminal_id))
                    actor.set_editor_property("FloorIndex", floor)
                    actor.set_editor_property("PairedTerminalId", unreal.Name(paired or ""))
                    actor.set_editor_property("ZoneLinkRadius", 900.0)
                except Exception as exc:  # noqa: BLE001
                    report.failed("terminal " + terminal_id, str(exc))

        placed.append({
            "id": terminal_id,
            "floor": floor,
            "location": [x, y, z],
            "paired": paired,
        })

    C.log(u"  terminals placed: {0} (paired pairs: {1})".format(
        len(OVERRIDE_TERMINALS),
        sum(1 for t in OVERRIDE_TERMINALS if t[4] is not None) // 2))
    return placed


def build_breachable_walls(report, materials):
    """Places the walls a breaching charge can open.

    A breach is the loudest thing a squad can do, so each of these is a decision:
    it costs a utility slot, it costs three seconds of standing still, and it
    tells the whole floor where you are.

    The actor is AOneBrahamBreachableWall, which computes its own opening from
    the dimensions it is given: two jambs and a lintel appear when the wall goes,
    so the hole is real geometry rather than a deleted wall.
    """
    wall_class = getattr(unreal, "OneBrahamBreachableWall", None)
    placed = []

    for wall_id, floor, x, y, axis, width, height, surface in BREACHABLE_WALLS:
        z = _floor_z(floor) + height / 2.0
        thickness = 25.0
        size = (width, thickness, height) if axis == "X" else (thickness, width, height)

        # Yaw only: the wall's opening maths is expressed in the actor's local
        # frame, where X is width and Y is thickness.
        yaw = 0.0 if axis == "X" else 90.0
        rotation = unreal.Rotator(0.0, 0.0, yaw)

        material = materials.get(surface) if materials else None

        if wall_class is None:
            report.skipped_asset(
                "AOneBrahamBreachableWall",
                "breachable wall class is not loaded; wall '" + wall_id +
                "' is blockout only and cannot be breached. Rebuild the C++ module.")
            _box((x, y, z), size, material, wall_id, static=False)
        else:
            actor = _spawn(wall_class, unreal.Vector(x, y, z), rotation)
            if actor is not None:
                _label(actor, wall_id)
                try:
                    actor.set_editor_property("WallId", unreal.Name(wall_id))
                    actor.set_editor_property("FloorIndex", floor)
                    actor.set_editor_property("SurfaceId", unreal.Name(surface))
                    actor.set_editor_property("WallMaterial", material)
                    actor.set_editor_property(
                        "WallDimensionsCm", unreal.Vector(size[0], size[1], size[2]))
                    # Pushes the sizes onto the meshes now, so the level is
                    # correct before it is ever played.
                    actor.call_method("SetWallDimensions",
                                      (unreal.Vector(size[0], size[1], size[2]), True))
                except Exception as exc:  # noqa: BLE001
                    report.failed("breachable wall " + wall_id, str(exc))

                try:
                    actor.tags = [unreal.Name("OB.Breachable"), unreal.Name(surface)]
                except Exception:  # noqa: BLE001
                    pass

        placed.append({
            "id": wall_id,
            "floor": floor,
            "location": [x, y, z],
            "size": list(size),
            "surface": surface,
            "axis": axis,
        })

    C.log(u"  breachable walls placed: {0}".format(len(placed)))
    return placed


def build_shutters(report):
    """Places the fire shutters the lockdown director closes.

    AOneBrahamFireShutter owns its own shutter leaf, its frame and its seal
    volume, and it registers itself with the lockdown director on BeginPlay. The
    builder therefore places ONE actor per stair opening and nothing else: an
    extra box here would be a permanent wall across the stair core, which is the
    one thing the tower cannot afford.
    """
    shutter_class = getattr(unreal, "OneBrahamFireShutter", None)
    placed = []

    for floor in SHUTTER_FLOORS:
        z = _floor_z(floor) + C.DOOR_HEIGHT_CM / 2.0

        for side in (-1.0, 1.0):
            x = 1300.0 * side
            side_name = "A" if side < 0 else "B"

            if shutter_class is None:
                report.skipped_asset(
                    "AOneBrahamFireShutter",
                    "fire shutter class is not loaded; stair opening " + side_name +
                    " on floor " + str(floor) + " cannot be sealed by the lockdown.")
                _box((x, 0.0, z), (C.DOOR_WIDTH_CM + 40.0, 30.0, C.DOOR_HEIGHT_CM),
                     None, "Shutter_{0}_F{1:02d}".format(side_name, floor), static=False)
            else:
                shutter = _spawn(shutter_class, unreal.Vector(x, 0.0, z))
                if shutter is not None:
                    _label(shutter, "Shutter_{0}_F{1:02d}".format(side_name, floor))
                    try:
                        shutter.set_editor_property("FloorIndex", floor)
                        shutter.set_editor_property("ShutterId", unreal.Name(
                            "Shutter_{0}_F{1:02d}".format(side_name, floor)))
                    except Exception as exc:  # noqa: BLE001
                        report.failed("shutter " + side_name + " F" + str(floor), str(exc))

            placed.append({"floor": floor, "side": side_name})

    C.log(u"  fire shutters placed: {0}".format(len(placed)))
    return placed


def build_extraction(report):
    """Builds the Skydeck extraction: pad, winch, beacon and control room.

    The pad is the only place in the game where a player stands still and waits,
    so it is deliberately exposed on three sides with cover on one. Everything
    about the geometry here is about making the arrival audible and visible from
    the stairwell before the helicopter lands.
    """
    deck_z = _floor_z(15)
    px, py, _ = C.EXTRACTION_PAD_LOCATION
    materials = {}
    concrete = None
    metal = None

    # Pad markings: a painted circle (a flattened cylinder) plus a centre letter H
    # built from three boxes. Cheap, readable from the air, and it reads on the
    # ground as the obvious place to be.
    _box((px, py, deck_z + 1.0), (C.EXTRACTION_HOLD_RADIUS_CM * 1.6, C.EXTRACTION_HOLD_RADIUS_CM * 1.6, 2.0),
         None, "Helipad_Marking")
    _box((px - 130.0, py, deck_z + 2.0), (40.0, 300.0, 4.0), None, "Helipad_H_Left")
    _box((px + 130.0, py, deck_z + 2.0), (40.0, 300.0, 4.0), None, "Helipad_H_Right")
    _box((px, py, deck_z + 2.0), (300.0, 40.0, 4.0), None, "Helipad_H_Cross")

    # Winch control room, from the room programme.
    _box((-1500.0, 400.0, deck_z + 125.0), (600.0, 500.0, 250.0), None, "Winch_Control_Shell")

    # Beacon mast: the thing the whole building can see. Four metres of mast with
    # a light on top, hot on the extraction channel.
    mast = _box((px + 600.0, py + 600.0, deck_z + 200.0), (40.0, 40.0, 400.0), None, "Extraction_Beacon_Mast")
    if mast is not None:
        try:
            light = _spawn(unreal.PointLight, unreal.Vector(px + 600.0, py + 600.0, deck_z + 420.0))
            if light is not None:
                _label(light, "Extraction_Beacon_Light")
                light.point_light_component.set_editor_property("Intensity", 6000.0)
                light.point_light_component.set_editor_property("AttenuationRadius", 3000.0)
                light.point_light_component.set_editor_property("LightColor",
                    unreal.Color(255, 160, 40, 255))
        except Exception as exc:  # noqa: BLE001
            report.failed("Extraction beacon light", str(exc))

    # The winch itself: the pad volume that tells the extraction component where
    # the wire is, and the console that calls it. Both are actors because both
    # carry rules the map cannot express as geometry - the pad measures the hold,
    # and the console is the only caller of ServerCallExtraction in the project.
    # (Before they were placed, the winch could not be called at all: the whole
    # broadcast/arrival/hold sequence had no entry point.)
    pad_class = getattr(unreal, "OneBrahamExtractionPad", None)
    console_class = getattr(unreal, "OneBrahamExtractionTerminal", None)

    if pad_class is None:
        report.skipped_asset(
            "AOneBrahamExtractionPad",
            "winch pad actor is not loaded: the map has the painted square and no "
            "hold rule, so 'on the pad' is measured against the component's default "
            "point. Rebuild the C++ module, then re-run this script.")
        _box((px, py, deck_z + 1.0), (C.EXTRACTION_HOLD_RADIUS_CM * 2.0,
             C.EXTRACTION_HOLD_RADIUS_CM * 2.0, 2.0), None, "Winch_Pad_Blockout")
    else:
        pad = _spawn(pad_class, unreal.Vector(px, py, deck_z))
        if pad is not None:
            _label(pad, "Winch_Pad")
            try:
                pad.set_editor_property("PadId", unreal.Name("Skydeck_Winch_Pad"))
                pad.set_editor_property("HoldRadiusCm", C.EXTRACTION_HOLD_RADIUS_CM)
                pad.set_editor_property("ZoneHeightCm", 300.0)
                pad.set_editor_property("SearchlightHeightCm", 600.0)
            except Exception as exc:  # noqa: BLE001
                report.failed("winch pad", str(exc))

    # The console stands against the pad-facing wall of the winch control shell,
    # facing the deck: calling the winch means turning your back on the pad, which
    # is the decision the mode is built around.
    console_x = -1140.0
    console_y = 400.0
    console_z = deck_z + 20.0

    if console_class is None:
        report.skipped_asset(
            "AOneBrahamExtractionTerminal",
            "winch console class is not loaded: nothing in the level can call the "
            "winch. Rebuild the C++ module, then re-run this script.")
        _box((console_x, console_y, deck_z + 55.0), (140.0, 70.0, 110.0), None,
             "Winch_Console_Blockout", static=False)
    else:
        console = _spawn(console_class, unreal.Vector(console_x, console_y, console_z),
                         unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0))
        if console is not None:
            _label(console, "Winch_Control_Console")
            try:
                console.set_editor_property("TerminalId", unreal.Name("Winch_Control_Console"))
                console.set_editor_property("TerminalDisplayName",
                    unreal.Text("WINCH CONTROL"))
                console.set_editor_property("CallHoldSeconds", 1.4)
                console.set_editor_property("InteractionRangeCm", 220.0)
            except Exception as exc:  # noqa: BLE001
                report.failed("winch console", str(exc))

    C.log(u"  extraction pad built at {0} (hold radius {1} cm)".format(
        C.EXTRACTION_PAD_LOCATION, C.EXTRACTION_HOLD_RADIUS_CM))
    C.log(u"  winch pad actor: {0}; console: {1} (hold {2} s)".format(
        u"placed" if pad_class is not None else u"BLOCKOUT ONLY",
        u"placed" if console_class is not None else u"BLOCKOUT ONLY",
        1.4))

    return {
        "pad_location": list(C.EXTRACTION_PAD_LOCATION),
        "hold_radius": C.EXTRACTION_HOLD_RADIUS_CM,
        "pad_actor": pad_class is not None,
        "console_actor": console_class is not None,
        "console_location": [console_x, console_y, console_z],
        "console_hold_seconds": 1.4,
    }


def build_lighting(report, floor_index):
    """Places the lighting for one floor.

    The building is lit by its own ceiling grid - the flat, slightly greenish
    light of a real office - with the Skydeck lit by the sky. That contrast is
    the entire visual argument of the game: fourteen floors of overhead panels
    and one floor of weather.
    """
    area_name, origin_x, origin_y, size_x, size_y = _area_for_floor(floor_index)
    if area_name is None:
        return

    z = _floor_z(floor_index)
    ceiling_z = z + C.CEILING_HEIGHT_CM - 30.0

    # Office lighting: a grid of recessed panels. Wide attenuation, low intensity:
    # a hundred small bright lights would look like a nightclub, not a workplace.
    grid_x = int(max(2, size_x // 600))
    grid_y = int(max(2, size_y // 600))

    spawn_count = 0
    for ix in range(grid_x):
        for iy in range(grid_y):
            lx = origin_x - size_x / 2.0 + (ix + 0.5) * (size_x / grid_x)
            ly = origin_y - size_y / 2.0 + (iy + 0.5) * (size_y / grid_y)

            light = _spawn(unreal.RectLight, unreal.Vector(lx, ly, ceiling_z),
                           unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0))  # facing down
            if light is None:
                continue

            try:
                component = light.rect_light_component
                component.set_editor_property("Intensity", 2600.0)
                component.set_editor_property("AttenuationRadius", 700.0)
                component.set_editor_property("SourceWidth", 60.0)
                component.set_editor_property("SourceHeight", 60.0)
                # 4000 K: the colour of a building's own lighting, everywhere.
                component.set_editor_property("LightColor", unreal.Color(255, 246, 232, 232))
                component.set_editor_property("Mobility", unreal.ComponentMobility.STATIC)
            except Exception:  # noqa: BLE001
                pass

            _label(light, "Ceiling_Light_{0}_F{1:02d}_{2}".format(area_name, floor_index, spawn_count))
            spawn_count += 1

    C.log(u"  floor {0:02d} ({1}): {2} ceiling lights".format(floor_index, area_name, spawn_count))


def build_post_process(report, floor_index):
    """Adds the unbound post-process volume that grades this floor.

    Created per floor rather than once in the persistent level because the
    Skydeck and the server room cannot share an exposure range, and a single
    global volume is how a game ends up with one floor that looks like a
    photograph and another that looks like a screenshot of a spreadsheet.

    Every property is set inside the same try/except the rest of this file uses:
    post-process property names have moved between engine minor versions, and a
    volume that exists with default settings is still better than a build that
    failed because one property was renamed.
    """
    area_name, origin_x, origin_y, size_x, size_y = _area_for_floor(floor_index)
    if area_name is None:
        return

    z = _floor_z(floor_index) + C.CEILING_HEIGHT_CM / 2.0

    volume = _spawn(unreal.PostProcessVolume, unreal.Vector(origin_x, origin_y, z))
    if volume is None:
        return

    _label(volume, "PostProcess_{0}_F{1:02d}".format(area_name, floor_index))

    min_brightness, max_brightness, bloom, grain, vignette = AREA_GRADE.get(
        area_name, (0.5, 2.0, 0.6, 0.025, 0.35))

    try:
        volume.set_editor_property("bUnbound", True)
        volume.set_editor_property("BlendWeight", 1.0)
        volume.set_editor_property("Priority", 0.0)
    except Exception:  # noqa: BLE001
        pass

    try:
        settings = volume.settings

        # Exposure: manual range, so a dark plant room does not auto-brighten the
        # whole floor and a bright one does not auto-dim it.
        settings.set_editor_property("AutoExposureMethod", unreal.AutoExposureMethod.AEM_Manual)
        settings.set_editor_property("AutoExposureMinBrightness", float(min_brightness))
        settings.set_editor_property("AutoExposureMaxBrightness", float(max_brightness))

        # Grade: restrained. One Braham is a corporate building, not a film.
        settings.set_editor_property("BloomIntensity", float(bloom))
        settings.set_editor_property("FilmGrainIntensity", float(grain))
        settings.set_editor_property("VignetteIntensity", float(vignette))
        settings.set_editor_property("MotionBlurAmount", 0.28)
    except Exception:  # noqa: BLE001
        pass

    # The interaction outline. Six interactables in this game set custom-depth
    # stencil 2 when the player looks at them (SetInteractionHighlight), and
    # until now nothing on the receiving end read it: the highlight was written
    # to the depth buffer and consumed by nobody. M_OB_Highlight is the material
    # that reads it, and this is where it is applied - the material is
    # build_materials.py's, the volume is this file's.
    #
    # Loaded by path and skipped with a warning rather than a failure: a floor
    # without an outline is still a floor, and the build order already puts
    # materials first for exactly the case where it is not.
    try:
        highlight = unreal.EditorAssetLibrary.load_asset(C.MATERIALS + "/M_OB_Highlight")

        if highlight is None:
            C.warn(u"  M_OB_Highlight not built yet - no interaction outline on "
                   u"PostProcess_{0}_F{1:02d} (run build_materials.py first)".format(
                       area_name, floor_index))
        else:
            blendables = unreal.WeightedBlendables()
            blendables.set_editor_property("array", [unreal.WeightedBlendable(1.0, highlight)])
            settings.set_editor_property("WeightedBlendables", blendables)
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  outline blendable not attachable on PostProcess_{0}_F{1:02d}: {2}".format(
            area_name, floor_index, exc))


def build_audio_zone(report, floor_index):
    """Adds the audio volume that tells the audio director which room you are in.

    The ambience bed is chosen from the zone id, so this is the map telling the
    sound designer where the room boundaries are - one volume per area, tagged
    with the exact zone id the audio bank uses.
    """
    area_name, origin_x, origin_y, size_x, size_y = _area_for_floor(floor_index)
    if area_name is None:
        return

    z = _floor_z(floor_index) + C.CEILING_HEIGHT_CM / 2.0

    volume = _spawn(unreal.AudioVolume, unreal.Vector(origin_x, origin_y, z))
    if volume is None:
        return

    _label(volume, "AudioZone_{0}_F{1:02d}".format(area_name, floor_index))

    try:
        volume.set_actor_scale3d(unreal.Vector(size_x / 100.0, size_y / 100.0, C.CEILING_HEIGHT_CM / 100.0))
        # The zone id maps to AMB_* in the audio bank (see OneBrahamAudioBankAsset).
        settings = volume.settings
        settings.set_editor_property("Enabled", True)
        settings.set_editor_property("Priority", 1.0)
    except Exception:  # noqa: BLE001
        pass


def build_nav(report, floor_index):
    """Bounds the nav mesh for one floor.

    Without this, bots stand still on the mezzanine - the single most common
    "the AI is broken" report in any project that builds its levels from script.
    """
    area_name, origin_x, origin_y, size_x, size_y = _area_for_floor(floor_index)
    if area_name is None:
        return

    z = _floor_z(floor_index)

    volume = _spawn(unreal.NavMeshBoundsVolume, unreal.Vector(origin_x, origin_y, z + C.CEILING_HEIGHT_CM / 2.0))
    if volume is None:
        report.failed("NavMeshBoundsVolume F{0}".format(floor_index), "could not spawn")
        return

    _label(volume, "NavBounds_F{0:02d}_{1}".format(floor_index, area_name))
    volume.set_actor_scale3d(unreal.Vector((size_x - 200.0) / 100.0, (size_y - 200.0) / 100.0,
                                           (C.CEILING_HEIGHT_CM - 80.0) / 100.0))


def build_skydeck_climate(report):
    """Skydeck weather: this is the only floor that gets a sky, wind and rain.

    The contrast is the point. Fifteen floors of sealed, over-lit corporate space
    and then one floor where the weather is real and the wind takes your voice
    away - which is why proximity voice is quieter up here and why the extraction
    announcement is the loudest cue in the game.
    """
    deck_z = _floor_z(15)

    build_skydeck_edge(report)
    _set_kill_z(report, _floor_z(0) - 1200.0)

    sky = _spawn(unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, deck_z))
    if sky is None:
        report.failed("SkyAtmosphere", "could not spawn")
    else:
        _label(sky, "Sky_OneBraham")

    sun = _spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, deck_z + 2000.0),
                 unreal.Rotator(roll=0.0, pitch=-38.0, yaw=145.0))
    if sun is not None:
        _label(sun, "Sun_London_Overcast")
        try:
            component = sun.directional_light_component
            # An overcast London afternoon: wide, soft, low contrast. Sunlight
            # would make the office floors look like a Mediterranean resort.
            component.set_editor_property("Intensity", 2.2)
            component.set_editor_property("LightColor", unreal.Color(214, 222, 232, 255))
            component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
        except Exception:  # noqa: BLE001
            pass

    fog = _spawn(unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, deck_z))
    if fog is not None:
        _label(fog, "Fog_London")
        try:
            component = fog.component
            component.set_editor_property("FogDensity", 0.012)
            component.set_editor_property("FogHeightFalloff", 0.12)
        except Exception:  # noqa: BLE001
            pass

    skylight = _spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, deck_z + 400.0))
    if skylight is not None:
        _label(skylight, "Skylight_OneBraham")
        try:
            skylight.sky_light_component.set_editor_property("Intensity", 0.8)
            skylight.sky_light_component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
        except Exception:  # noqa: BLE001
            pass

    # Post-process: a corporate colour grade (slightly desaturated, cool shadows,
    # a small amount of grain) so the building reads as a building even in the
    # blockout, and so the art pass has a target to match rather than invent.
    ppv = _spawn(unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, deck_z - 200.0))
    if ppv is not None:
        _label(ppv, "PPS_OneBraham_Global")
        try:
            ppv.set_editor_property("Unbound", True)
            settings = ppv.settings
            settings.set_editor_property("OverrideFilmGrainIntensity", True)
            settings.set_editor_property("FilmGrainIntensity", 0.18)
            settings.set_editor_property("OverrideColorSaturation", True)
            settings.set_editor_property("ColorSaturation", unreal.Vector4(0.94, 0.94, 0.94, 1.0))
            settings.set_editor_property("OverrideAutoExposureMinBrightness", True)
            settings.set_editor_property("AutoExposureMinBrightness", 0.65)
            settings.set_editor_property("OverrideAutoExposureMaxBrightness", True)
            settings.set_editor_property("AutoExposureMaxBrightness", 1.6)
        except Exception as exc:  # noqa: BLE001
            report.failed("Post-process volume", str(exc))


#: The Skydeck balustrade: frameless glass, 110 cm, the height a real roof
#: terrace has. Breakable, because the fall is the deck's one environmental
#: story: a squad that shoots out the glass behind the holding squad has made
#: the pad a cliff edge, and Landed / FellOutOfWorld do the rest.
BALUSTRADE_HEIGHT_CM = 110.0
BALUSTRADE_PANE_WIDTH_CM = 240.0
BALUSTRADE_INSET_CM = 40.0



def skydeck_balustrade_panes():
    """(pane id, centre x, centre y, yaw) for every pane round the deck edge.

    Pure, so the engine-free check can see that the deck has an edge on all four
    sides before an editor ever runs.
    """
    area = next(a for a in C.AREAS if a[0] == "Skydeck")
    _name, _floors, cx, cy, size_x, size_y = C.area_centre(area)
    half_x = size_x / 2.0 - BALUSTRADE_INSET_CM
    half_y = size_y / 2.0 - BALUSTRADE_INSET_CM

    panes = []
    for side, (start, end, fixed, along_x) in (
            ("North", (-half_x, half_x, cy + half_y, True)),
            ("South", (-half_x, half_x, cy - half_y, True)),
            ("East", (-half_y, half_y, cx + half_x, False)),
            ("West", (-half_y, half_y, cx - half_x, False))):
        length = end - start
        count = max(1, int(round(length / BALUSTRADE_PANE_WIDTH_CM)))
        width = length / count
        for index in range(count):
            along = start + (index + 0.5) * width
            if along_x:
                panes.append(("Pane_Skydeck_{0}_{1:02d}".format(side, index), cx + along, fixed, 0.0, width))
            else:
                panes.append(("Pane_Skydeck_{0}_{1:02d}".format(side, index), fixed, cy + along, 90.0, width))
    return panes


def build_skydeck_edge(report):
    """Places the deck's glass balustrade as AOneBrahamBreakableGlass panes."""
    glass_class = getattr(unreal, "OneBrahamBreakableGlass", None)
    if glass_class is None:
        report.failed("Skydeck balustrade", "OneBrahamBreakableGlass is not reflected; compile the module first")
        return []

    deck_z = _floor_z(C.FLOOR_COUNT)
    placed = []
    for pane_id, x, y, yaw, width in skydeck_balustrade_panes():
        actor = _spawn(glass_class, unreal.Vector(x, y, deck_z + BALUSTRADE_HEIGHT_CM / 2.0),
                       unreal.Rotator(0.0, 0.0, yaw))
        if actor is None:
            continue

        _label(actor, pane_id)
        try:
            actor.set_editor_property("PaneId", unreal.Name(pane_id))
            actor.set_editor_property("PaneSizeCm", unreal.Vector2D(width, BALUSTRADE_HEIGHT_CM))
            actor.set_editor_property("FloorIndex", C.FLOOR_COUNT)
        except Exception as exc:  # noqa: BLE001
            report.failed(pane_id, str(exc))
            continue
        placed.append(pane_id)

    C.log(u"  Skydeck: {0} balustrade panes".format(len(placed)))
    return placed


def _set_kill_z(report, kill_z):
    """Below the plaza, so anything that leaves the map dies rather than falling
    for ever; FellOutOfWorld turns it into a death the kill feed can name."""
    try:
        settings = _editor_world().get_world_settings()
        settings.set_editor_property("kill_z", kill_z)
    except Exception as exc:  # noqa: BLE001
        report.failed("KillZ", str(exc))


# ===========================================================================
#  Levels
# ===========================================================================

def build_floor_level(report, floor_index, materials):
    """Creates or rebuilds the sublevel for one floor. Returns its package path."""
    level_path = "{0}/{1}_F{2:02d}".format(C.MAPS, "L_OneBraham", floor_index)

    # An existing floor used to be returned untouched with "updating" in the
    # log, so a change to a room programme never reached a map that had been
    # built once. It is now opened, cleared of what the last run made, and
    # rebuilt.
    if not _open_level_for_build(report, level_path):
        return None

    build_floor_shell(report, floor_index, materials)
    build_lighting(report, floor_index)
    build_post_process(report, floor_index)
    build_audio_zone(report, floor_index)
    build_nav(report, floor_index)

    if not _save_build_level(report, level_path):
        return None

    return level_path


def build_persistent_level(report, materials, manifest):
    """Creates the persistent level: the shell, the town, and the references."""
    persistent_path = "{0}/{1}".format(C.MAPS, C.MAP_PERSISTENT)

    if not _open_level_for_build(report, persistent_path):
        return None

    # --- Gameplay anchors -------------------------------------------------
    # These were placed before this level was opened, into whatever map the
    # editor happened to have open, so the persistent level shipped with no
    # spawn points, access zones, terminals, breach walls or shutters.
    manifest["spawn_anchors"] = C.guard(report, "spawn points", build_spawn_points, report) or []
    manifest["access_zones"] = C.guard(report, "access zones", build_access_zones, report) or []
    manifest["terminals"] = C.guard(report, "terminals", build_override_terminals, report) or []
    manifest["breachable_walls"] = C.guard(report, "breachable walls", build_breachable_walls, report, materials) or []
    manifest["shutters"] = C.guard(report, "shutters", build_shutters, report) or []
    manifest["loot_points"] = C.guard(report, "loot points", build_loot_points, report) or []

    # --- The building shell -----------------------------------------------
    # A single exterior mass so the tower is visible from the Skydeck, the lobby
    # windows look onto something, and the plaza has a building above it.
    shell_material = materials.get("Concrete")
    _box((0.0, 0.0, -220.0), (9000.0, 8000.0, 400.0), shell_material, "Ground_Slab_Exterior")

    # Curtain wall, four sides, from the lobby to the Skydeck. Deliberately one
    # mass rather than fifteen stacked bands: the interior floors have their own
    # glazing, and this is the silhouette the players see in the trailer.
    glass = materials.get("BallisticGlass") or materials.get("Glass")
    # From the plaza to the underside of the Skydeck slab and no higher: the
    # curtain wall used to run a storey past the deck, so the roof was a glass
    # box and nobody could see out of the ending or fall off it.
    facade_bottom = _floor_z(0)
    facade_top = _floor_z(C.FLOOR_COUNT) - 20.0
    total_height = facade_top - facade_bottom
    facade_z = facade_bottom + total_height / 2.0
    _box((0.0, -4200.0, facade_z), (8600.0, 60.0, total_height), glass, "Facade_South")
    _box((0.0, 4200.0, facade_z), (8600.0, 60.0, total_height), glass, "Facade_North")
    _box((-4400.0, 0.0, facade_z), (60.0, 8600.0, total_height), glass, "Facade_West")
    _box((4400.0, 0.0, facade_z), (60.0, 8600.0, total_height), glass, "Facade_East")

    # --- Ground: the plaza and Aldgate ------------------------------------
    # Asphalt, kerbs, a road with markings, and the low buildings opposite:
    # the outside of One Braham has to look like Aldgate, because the first thing
    # the player does is walk in the front door.
    _box((0.0, 0.0, -410.0), (26000.0, 26000.0, 20.0), materials.get("Concrete"), "Aldgate_Asphalt")
    for i in range(4):
        offset = -6000.0 + i * 1200.0
        _box((offset, 7000.0, -395.0), (900.0, 40.0, 4.0), None, "Road_Crossing_{0}".format(i))

    # --- Persistent gameplay actors ---------------------------------------
    # The winch's return value goes into the manifest: whether the pad and the
    # console are actors or blockout, where the console stands, and how long the
    # call takes. A content build that could not place the console has a map in
    # which the winch cannot be called at all, and that has to be readable in the
    # manifest rather than inferred from a log line.
    winch = build_extraction(report)
    if winch:
        manifest["extraction"] = winch

    build_skydeck_climate(report)
    build_vertical_circulation(report, materials)

    manifest["persistent_level"] = persistent_path

    _save_build_level(report, persistent_path)
    return persistent_path


# ===========================================================================
#  Entry point
# ===========================================================================

def build_all(write_manifest=True):
    """Builds the entire building. Returns the build report."""
    _require_editor()

    report = C.BuildReport("build_environment")
    C.log(u"=== Building One Braham ===")

    _ensure_dir(C.MAPS)
    _ensure_dir(C.ENVIRONMENT)
    _ensure_dir(C.PROPS)

    C.step(1, 6, "materials")
    materials = C.guard(report, "materials", build_materials, report) or {}

    C.step(2, 6, "room programmes")
    _SURFACE_USAGE.clear()

    manifest = {
        "generated_by": "Tools/Python/onebraham/build_environment.py",
        "floor_height_cm": C.FLOOR_HEIGHT_CM,
        "floor_count": C.FLOOR_COUNT,
        "areas": [{"name": a[0], "floors": list(a[1]), "min_corner": [a[2], a[3]],
                   "centre": list(C.area_centre(a)[2:4]), "size": [a[4], a[5]]}
                  for a in C.AREAS],
    }

    manifest["rooms"] = [
        {"area": area, "name": room[0], "x": room[1], "y": room[2],
         "size": [room[3], room[4]]}
        for area, rooms in sorted(ROOM_PROGRAMMES.items()) for room in rooms
    ]
    manifest["surfaces"] = [name for name, _colour, _roughness in SURFACES]
    manifest["floor_finishes"] = [
        {"area": area, "room": room[0],
         "surface": ROOM_FLOOR_FINISH.get((area, room[0]), AREA_DEFAULT_FINISH.get(area))}
        for area, rooms in sorted(ROOM_PROGRAMMES.items()) for room in rooms
    ]
    manifest["surface_usage"] = {name: sorted(places) for name, places in _SURFACE_USAGE.items()}
    if len(manifest["rooms"]) < 30:
        report.failed("rooms", u"only {0} rooms programmed; the map needs 40".format(
            len(manifest["rooms"])))

    C.step(3, 6, "persistent level and gameplay anchors")
    persistent_path = C.guard(report, "persistent level", build_persistent_level, report, materials, manifest)

    C.step(4, 6, "per-floor streaming levels")
    floors = []
    for floor_index in range(0, C.FLOOR_COUNT + 1):
        level = C.guard(report, u"floor level F{0:02d}".format(floor_index),
                        build_floor_level, report, floor_index, materials)
        if level:
            area_name = _area_for_floor(floor_index)[0]
            grade = AREA_GRADE.get(area_name)
            floors.append({
                "index": floor_index,
                "level": level,
                "area": area_name,
                "z_cm": _floor_z(floor_index),
                "exposure": [grade[0], grade[1]] if grade else None,
            })
    manifest["floors"] = floors
    if len(floors) != C.FLOOR_COUNT + 1:
        report.failed("floor levels", u"only {0} of {1} floors built".format(
            len(floors), C.FLOOR_COUNT + 1))

    if persistent_path:
        attached = C.guard(report, "attach floors", _attach_floor_levels, report, persistent_path,
                           [floor["level"] for floor in floors]) or 0
        manifest["floors_attached"] = attached
        if attached != len(floors):
            report.failed("floor levels", u"{0} of {1} floors attached to {2}".format(
                attached, len(floors), persistent_path))

    C.step(5, 6, "fire drill arena")
    C.guard(report, "fire drill arena", build_fire_drill_arena, report, materials)

    C.step(6, 6, "manifest")
    if write_manifest:
        C.guard(report, "manifest", write_build_manifest, manifest)

    C.log(report.summary())
    return report


def build_fire_drill_arena(report, materials):
    """Builds the FIRE DRILL arena: a compact, three-point control map.

    FIRE DRILL is 6v6 or 8v8 with three capture points (Lobby / Server Floor /
    Canteen-Skydeck) and faster respawns. It is the mode a new player learns the
    shooting in, so its arena is deliberately tighter than the tower: three
    points in a line, each with two flanks and one hard chokepoint.
    """
    path = "{0}/{1}".format(C.MAPS, C.MAP_FIRE_DRILL)

    if not _open_level_for_build(report, path):
        return None

    concrete = materials.get("Concrete")
    carpet = materials.get("Carpet")
    metal = materials.get("Metal")

    # Arena floor and ceiling.
    _box((0.0, 0.0, -10.0), (5000.0, 7000.0, 20.0), concrete, "Arena_Floor")
    _box((0.0, 0.0, 410.0), (5000.0, 7000.0, 20.0), concrete, "Arena_Ceiling")

    # Three control points down the centre line: A (Lobby), B (Server Floor),
    # C (Canteen-Skydeck). Each gets a raised platform and cover, so a capture is
    # a fight and not a walk.
    points = [("A_Lobby", 0.0, -2600.0), ("B_ServerFloor", 0.0, 0.0), ("C_Canteen", 0.0, 2600.0)]
    for name, cx, cy in points:
        _box((cx, cy, 20.0), (1400.0, 1400.0, 40.0), carpet, "ControlPoint_{0}".format(name))
        # Four cover blocks on the corners, offset so the point is not a bowl.
        for sx in (-1.0, 1.0):
            for sy in (-1.0, 1.0):
                _box((cx + sx * 800.0, cy + sy * 800.0, 60.0), (260.0, 260.0, 120.0), metal,
                     "CP_{0}_Cover_{1}{2}".format(name, int(sx), int(sy)))

    # Two long flanks and one central corridor: the map's whole tactical promise
    # is that every point can be flanked and every flank can be held.
    _box((-2200.0, 0.0, 100.0), (200.0, 7000.0, 200.0), concrete, "Flank_West")
    _box((2200.0, 0.0, 100.0), (200.0, 7000.0, 200.0), concrete, "Flank_East")
    _box((0.0, 0.0, 200.0), (600.0, 7000.0, 20.0), concrete, "Corridor_Roof")

    # Team spawn rooms at each end.
    for team, sy in (("Staff", -3200.0), ("Contractors", 3200.0)):
        _box((0.0, sy, 60.0), (3600.0, 500.0, 120.0), carpet, "SpawnRoom_{0}".format(team))

    # Lighting: same ceiling grid language as the tower, brighter, colder.
    for ix in range(-2, 3):
        for iy in range(-3, 4):
            light = _spawn(unreal.RectLight, unreal.Vector(ix * 1000.0, iy * 1000.0, 380.0),
                           unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0))  # facing down
            if light is None:
                continue
            try:
                component = light.rect_light_component
                component.set_editor_property("Intensity", 3200.0)
                component.set_editor_property("AttenuationRadius", 900.0)
                component.set_editor_property("SourceWidth", 80.0)
                component.set_editor_property("SourceHeight", 80.0)
            except Exception:  # noqa: BLE001
                pass

    _spawn(unreal.NavMeshBoundsVolume, unreal.Vector(0.0, 0.0, 200.0))
    _spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 600.0))

    if not _save_build_level(report, path):
        return None

    C.log(u"FIRE DRILL arena built: three control points, two flanks, one corridor")
    return path


def write_build_manifest(manifest):
    """Writes the manifest next to the scripts.

    The manifest is the contract between the level, the C++ and the documentation:
    the spawn names, the zone ids, the terminal pairs and the extraction pad are
    all listed in one file that both the docs build and the validator read.
    """
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    path = os.path.join(project_root, "Tools", "Python", "onebraham_build_manifest.json")

    with open(path, "w") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)

    C.log(u"manifest written: {0}".format(path))
    return path


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
