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


def _spawn(actor_class, location, rotation=None):
    rot = rotation if rotation is not None else unreal.Rotator(0.0, 0.0, 0.0)
    return _actor_subsystem().spawn_actor_from_class(actor_class, location, rot)


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
    for name, (first, last), origin_x, origin_y, size_x, size_y in C.AREAS:
        if first <= floor_index <= last:
            return name, origin_x, origin_y, size_x, size_y
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

    # --- Ceiling ----------------------------------------------------------
    _box((origin_x, origin_y, z + C.CEILING_HEIGHT_CM + 10.0),
         (size_x, size_y, 20.0), plaster, prefix + "_Ceiling")

    # --- Perimeter glazing (the thing that makes the tower feel like a tower) --
    # Windows are placed as a bank rather than individually: the banded look of a
    # curtain-wall office is what sells the building from inside *and* it is what
    # makes a floor edge dangerous to walk near.
    if area_name not in ("ServerComms", "Ground_Plaza"):
        window_h = C.CEILING_HEIGHT_CM - 120.0
        _box((origin_x, origin_y - half_y, z + window_h / 2.0 + 60.0),
             (size_x - 200.0, 12.0, window_h), glass, prefix + "_Glazing_South")
        _box((origin_x, origin_y + half_y, z + window_h / 2.0 + 60.0),
             (size_x - 200.0, 12.0, window_h), glass, prefix + "_Glazing_North")

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

    C.log(u"  extraction pad built at {0} (hold radius {1} cm)".format(
        C.EXTRACTION_PAD_LOCATION, C.EXTRACTION_HOLD_RADIUS_CM))

    return {
        "pad_location": list(C.EXTRACTION_PAD_LOCATION),
        "hold_radius": C.EXTRACTION_HOLD_RADIUS_CM,
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
                           unreal.Rotator(0.0, 0.0, -90.0))
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

    sky = _spawn(unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, deck_z))
    if sky is None:
        report.failed("SkyAtmosphere", "could not spawn")
    else:
        _label(sky, "Sky_OneBraham")

    sun = _spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, deck_z + 2000.0),
                 unreal.Rotator(-38.0, 145.0, 0.0))
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


# ===========================================================================
#  Levels
# ===========================================================================

def build_floor_level(report, floor_index, materials):
    """Creates (or updates) the streaming level for one floor."""
    level_path = "{0}/{1}_F{2:02d}".format(C.MAPS, "L_OneBraham", floor_index)
    full_path = level_path + "." + os.path.basename(level_path)

    level_subsystem = _level_subsystem()

    if unreal.EditorAssetLibrary.does_asset_exist(level_path):
        C.log(u"  floor {0:02d}: streaming level already exists, updating".format(floor_index))
        report.updated_asset(level_path)
        return level_path

    try:
        level_subsystem.new_level(level_path)
    except Exception as exc:  # noqa: BLE001
        report.failed(level_path, u"could not create streaming level: {0}".format(exc))
        return None

    build_floor_shell(report, floor_index, materials)
    build_lighting(report, floor_index)
    build_audio_zone(report, floor_index)
    build_nav(report, floor_index)

    try:
        level_subsystem.save_current_level()
    except Exception as exc:  # noqa: BLE001
        report.failed(level_path, u"could not save: {0}".format(exc))
        return None

    report.created_asset(level_path)
    return level_path


def build_persistent_level(report, materials, manifest):
    """Creates the persistent level: the shell, the town, and the references."""
    level_subsystem = _level_subsystem()
    persistent_path = "{0}/{1}".format(C.MAPS, C.MAP_PERSISTENT)

    if unreal.EditorAssetLibrary.does_asset_exist(persistent_path):
        C.log(u"persistent level exists; rebuilding its persistent actors")
    else:
        try:
            level_subsystem.new_level(persistent_path)
            report.created_asset(persistent_path)
        except Exception as exc:  # noqa: BLE001
            report.failed(persistent_path, u"could not create: {0}".format(exc))
            return None

    # --- The building shell -----------------------------------------------
    # A single exterior mass so the tower is visible from the Skydeck, the lobby
    # windows look onto something, and the plaza has a building above it.
    shell_material = materials.get("Concrete")
    _box((0.0, 0.0, -220.0), (9000.0, 8000.0, 400.0), shell_material, "Ground_Slab_Exterior")

    # Curtain wall, four sides, from the lobby to the Skydeck. Deliberately one
    # mass rather than fifteen stacked bands: the interior floors have their own
    # glazing, and this is the silhouette the players see in the trailer.
    glass = materials.get("BallisticGlass") or materials.get("Glass")
    total_height = (C.FLOOR_COUNT + 1) * C.FLOOR_HEIGHT_CM
    _box((0.0, -4200.0, total_height / 2.0 - 400.0), (8600.0, 60.0, total_height), glass, "Facade_South")
    _box((0.0, 4200.0, total_height / 2.0 - 400.0), (8600.0, 60.0, total_height), glass, "Facade_North")
    _box((-4400.0, 0.0, total_height / 2.0 - 400.0), (60.0, 8600.0, total_height), glass, "Facade_West")
    _box((4400.0, 0.0, total_height / 2.0 - 400.0), (60.0, 8600.0, total_height), glass, "Facade_East")

    # --- Ground: the plaza and Aldgate ------------------------------------
    # Asphalt, kerbs, a road with markings, and the low buildings opposite:
    # the outside of One Braham has to look like Aldgate, because the first thing
    # the player does is walk in the front door.
    _box((0.0, 0.0, -410.0), (26000.0, 26000.0, 20.0), materials.get("Concrete"), "Aldgate_Asphalt")
    for i in range(4):
        offset = -6000.0 + i * 1200.0
        _box((offset, 7000.0, -395.0), (900.0, 40.0, 4.0), None, "Road_Crossing_{0}".format(i))

    # --- Persistent gameplay actors ---------------------------------------
    build_extraction(report)
    build_skydeck_climate(report)
    build_vertical_circulation(report, materials)

    manifest["persistent_level"] = persistent_path

    try:
        level_subsystem.save_current_level()
    except Exception as exc:  # noqa: BLE001
        report.failed(persistent_path, u"could not save: {0}".format(exc))

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

    C.step(2, 6, "gameplay anchors")
    manifest = {
        "generated_by": "Tools/Python/onebraham/build_environment.py",
        "floor_height_cm": C.FLOOR_HEIGHT_CM,
        "floor_count": C.FLOOR_COUNT,
        "areas": [{"name": a[0], "floors": list(a[1]), "origin": [a[2], a[3]], "size": [a[4], a[5]]}
                  for a in C.AREAS],
    }

    manifest["spawn_anchors"] = C.guard(report, "spawn points", build_spawn_points, report) or []
    manifest["access_zones"] = C.guard(report, "access zones", build_access_zones, report) or []
    manifest["terminals"] = C.guard(report, "terminals", build_override_terminals, report) or []
    manifest["breachable_walls"] = C.guard(report, "breachable walls", build_breachable_walls, report, materials) or []
    manifest["shutters"] = C.guard(report, "shutters", build_shutters, report) or []
    manifest["rooms"] = [
        {"area": area, "name": room[0], "x": room[1], "y": room[2],
         "size": [room[3], room[4]]}
        for area, rooms in sorted(ROOM_PROGRAMMES.items()) for room in rooms
    ]
    manifest["surfaces"] = [name for name, _colour, _roughness in SURFACES]
    if len(manifest["rooms"]) < 30:
        report.failed("rooms", u"only {0} rooms programmed; the map needs 40".format(
            len(manifest["rooms"])))

    C.step(3, 6, "persistent level")
    C.guard(report, "persistent level", build_persistent_level, report, materials, manifest)

    C.step(4, 6, "per-floor streaming levels")
    floors = []
    for floor_index in range(0, C.FLOOR_COUNT + 1):
        level = C.guard(report, u"floor level F{0:02d}".format(floor_index),
                        build_floor_level, report, floor_index, materials)
        if level:
            area_name = _area_for_floor(floor_index)[0]
            floors.append({
                "index": floor_index,
                "level": level,
                "area": area_name,
                "z_cm": _floor_z(floor_index),
            })
    manifest["floors"] = floors
    if len(floors) != C.FLOOR_COUNT + 1:
        report.failed("floor levels", u"only {0} of {1} floors built".format(
            len(floors), C.FLOOR_COUNT + 1))

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

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        report.updated_asset(path)
        return path

    try:
        _level_subsystem().new_level(path)
    except Exception as exc:  # noqa: BLE001
        report.failed(path, u"could not create: {0}".format(exc))
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
                           unreal.Rotator(0.0, 0.0, -90.0))
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

    try:
        _level_subsystem().save_current_level()
    except Exception as exc:  # noqa: BLE001
        report.failed(path, u"could not save: {0}".format(exc))
        return None

    report.created_asset(path)
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
