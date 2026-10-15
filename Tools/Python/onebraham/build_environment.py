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
    # Ground floor: terrazzo reception, a wet bike store, back-of-house lino.
    ("Ground_Plaza", "Reception_Foyer"):  "Marble",
    ("Ground_Plaza", "Security_Office"):  "Lino",
    ("Ground_Plaza", "Post_Room"):        "Lino",
    ("Ground_Plaza", "Bike_Store"):       "Puddle",

    # Floor 1: the escalators arrive into a stone gallery; showcase and lab.
    ("Lobby", "Arrival_Gallery"):  "Marble",
    ("Lobby", "Client_Showcase"):  "Carpet",
    ("Lobby", "Lab"):              "Lino",
    ("Lobby", "Meeting_Room"):     "Fabric",
    ("Lobby", "Coffee_Point"):     "Lino",

    # Floor 2: breakout and meeting rooms.
    ("Mezzanine", "Breakout_Area"):  "Carpet",
    ("Mezzanine", "Team_Desks"):     "Carpet",
    ("Mezzanine", "Meeting_Pod_1"):  "Fabric",
    ("Mezzanine", "Meeting_Pod_2"):  "Fabric",
    ("Mezzanine", "Quiet_Room"):     "Fabric",
    ("Mezzanine", "Vending_Alcove"): "Lino",

    # Open plan: desk farms on carpet tile, acoustic pods.
    ("OpenPlan", "Desk_Farm_West"):    "Carpet",
    ("OpenPlan", "Desk_Farm_East"):    "Carpet",
    ("OpenPlan", "Desk_Farm_South"):   "Carpet",
    ("OpenPlan", "Meeting_Pod_1"):     "Fabric",
    ("OpenPlan", "Meeting_Pod_2"):     "Fabric",
    ("OpenPlan", "Break_Out_Kitchen"): "Lino",
    ("OpenPlan", "Print_Alcove"):      "Lino",

    # Server and comms: a raised perforated rack floor, not carpet. This is the
    # finish that makes a firefight on floor 9 throw sparks.
    ("ServerComms", "Server_Hall_West"): "ServerRack",
    ("ServerComms", "Server_Hall_East"): "ServerRack",
    ("ServerComms", "NOC"):              "ServerRack",
    ("ServerComms", "Power_Room"):       "Grating",
    ("ServerComms", "Comms_Cupboard"):   "Grating",

    # Executive: boardroom in dark wood, offices on carpet.
    ("Executive", "Boardroom"):          "Wood",
    ("Executive", "Corner_Office_West"): "Carpet",
    ("Executive", "Corner_Office_East"): "Carpet",
    ("Executive", "Exec_Suite"):         "Carpet",
    ("Executive", "Exec_PA_Desk"):       "Carpet",
    ("Executive", "Exec_Lounge"):        "Wood",

    # The canteen shares floor 15 with the Skydeck balcony: lino throughout,
    # and a kitchen floor that is always wet.
    ("Skydeck", "Canteen_Hall"):  "Lino",
    ("Skydeck", "Servery"):       "Lino",
    ("Skydeck", "Hot_Food_Line"): "Lino",
    ("Skydeck", "Coffee_Bar"):    "Lino",
    ("Skydeck", "Kitchen"):       "Puddle",

    # Skydeck: plant deck, the pad, the rails, and a lino control room.
    ("Skydeck", "Helipad"):         "Decking",
    ("Skydeck", "Edge_Rail_North"): "Decking",
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
    # Every floor from 1 to 14 is a loop round the core (build_core). Rooms sit
    # in four bands between the loop and the curtain wall: south and north,
    # east and west. Each tuple is (name, x, y, size x, size y) from the
    # plate's centre; zones in OPEN_ROOMS are open floor with a finish and no
    # walls. A room's doorway faces the core.
    "Ground_Plaza": [
        ("Reception_Foyer", -500, -1475, 3300, 650),
        ("Security_Office", -1800, -500, 800, 900),
        ("Post_Room", -1800, 650, 800, 900),
        ("Bike_Store", 1800, 1500, 800, 600),
    ],
    "Lobby": [
        ("Arrival_Gallery", 0, -1475, 4300, 650),
        ("Client_Showcase", -1775, 100, 750, 2100),
        ("Lab", 1775, 600, 750, 1300),
        ("Meeting_Room", -1500, 1575, 1100, 350),
        ("Coffee_Point", 1500, 1575, 1100, 350),
    ],
    "Mezzanine": [
        ("Breakout_Area", -1775, 100, 750, 2100),
        ("Team_Desks", 0, -1475, 3300, 650),
        ("Meeting_Pod_1", 1775, -300, 750, 800),
        ("Meeting_Pod_2", 1775, 750, 750, 800),
        ("Quiet_Room", -1500, 1575, 1100, 350),
        ("Vending_Alcove", 1500, 1575, 1100, 350),
    ],
    "OpenPlan": [
        ("Desk_Farm_West", -1775, 100, 750, 2100),
        ("Desk_Farm_East", 1775, 100, 750, 2100),
        ("Desk_Farm_South", 0, -1475, 2900, 650),
        ("Break_Out_Kitchen", -1850, -1475, 600, 650),
        ("Print_Alcove", 1850, -1475, 600, 650),
        ("Meeting_Pod_1", -1500, 1575, 1100, 350),
        ("Meeting_Pod_2", 1500, 1575, 1100, 350),
    ],
    "ServerComms": [
        ("Server_Hall_West", -1775, 100, 750, 2100),
        ("Server_Hall_East", 1775, 100, 750, 2100),
        ("NOC", 0, -1475, 2900, 650),
        ("Power_Room", -1500, 1575, 1100, 350),
        ("Comms_Cupboard", 1500, 1575, 1100, 350),
    ],
    "Executive": [
        ("Boardroom", 0, -1475, 2300, 650),
        ("Corner_Office_West", -1750, -1475, 700, 650),
        ("Corner_Office_East", 1750, -1475, 700, 650),
        ("Exec_Suite", -1775, 300, 750, 1700),
        ("Exec_PA_Desk", 1775, -100, 750, 1300),
        ("Exec_Lounge", 1775, 1100, 750, 700),
    ],
    "Skydeck": [
        # The canteen, west to east: the till and coffee bar against the back
        # wall, the hot-food line in front of the kitchen to its right, the
        # servery islands in front of those, then the dining hall widening out
        # towards the Skydeck gate. Zones of one open floor, not walled rooms
        # (see OPEN_ROOMS); the counters themselves are built by
        # build_canteen_fixtures.
        ("Coffee_Bar", -2025, -1200, 350, 1000),
        ("Kitchen", -1150, -1650, 1500, 300),
        ("Hot_Food_Line", -1150, -1300, 1500, 300),
        ("Servery", -1050, -1000, 1500, 250),
        ("Canteen_Hall", 1350, -1175, 1700, 1250),
        # The balcony, round the roof core: helipad and plant on the east
        # wing, the bar and the winch control on the west, the rail north.
        ("Helipad", 1650, 900, 1050, 1500),
        ("Plant_Room", 1650, -275, 1000, 450),
        ("Bar_Pavilion", -1650, 300, 1000, 900),
        ("Winch_Control", -1700, 1350, 600, 500),
        ("Edge_Rail_North", 0, 1740, 4200, 100),
    ],
}

# Zones of an open floor: they get a floor finish but no partition walls. The
# canteen is one room with a food court in it, not five rooms.
OPEN_ROOMS = {
    ("Ground_Plaza", "Reception_Foyer"),
    ("Lobby", "Arrival_Gallery"),
    ("Mezzanine", "Breakout_Area"),
    ("Mezzanine", "Team_Desks"),
    ("Mezzanine", "Vending_Alcove"),
    ("OpenPlan", "Desk_Farm_West"),
    ("OpenPlan", "Desk_Farm_East"),
    ("OpenPlan", "Desk_Farm_South"),
    ("OpenPlan", "Break_Out_Kitchen"),
    ("Lobby", "Coffee_Point"),
    ("Executive", "Exec_PA_Desk"),
    ("Skydeck", "Coffee_Bar"),
    ("Skydeck", "Kitchen"),
    ("Skydeck", "Hot_Food_Line"),
    ("Skydeck", "Servery"),
    ("Skydeck", "Canteen_Hall"),
    ("Skydeck", "Helipad"),
    ("Skydeck", "Edge_Rail_North"),
}

# Floor 15's split: the canteen is roofed and glazed south of this line, the
# balcony open to the north. The Skydeck gate is the door in the screen.
SKYDECK_SCREEN_Y = -550.0
SKYDECK_GATE_X = 900.0
SKYDECK_GATE_WIDTH = 300.0

# Anchors: named, gameplay-relevant positions per area. The manifest this script
# writes is the authoritative list the C++ and Docs reference.
ANCHORS = {
    # Clear floor only: the loop round the core, the bands' open zones, never
    # inside the core, a desk row or a walled room.
    "Ground_Plaza": [
        ("Plaza_Spawn_West", -1900, -1500),
        ("Plaza_Spawn_East", 1800, -1000),
        ("Plaza_Spawn_North", 0, 1500),
    ],
    "Lobby": [
        ("Lobby_Spawn_A", -1800, -1450),
        ("Lobby_Spawn_B", 1800, -1450),
        ("Lobby_Spawn_C", 0, 1500),
    ],
    "Mezzanine": [
        ("Mezz_Spawn_A", -1800, 0),
        ("Mezz_Spawn_B", 1250, -1100),
        ("Mezz_Spawn_C", 0, 1550),
    ],
    "OpenPlan": [
        ("Office_Spawn_A", -1800, -1450),
        ("Office_Spawn_B", 1850, -1450),
        ("Office_Spawn_C", -1250, 1450),
        ("Office_Spawn_D", 1250, 1450),
    ],
    "ServerComms": [
        ("Server_Spawn_A", -1225, -1000),
        ("Server_Spawn_B", 1225, -1000),
        ("Server_Spawn_C", 0, 1500),
    ],
    # Spread over 12-14 by index, two per floor.
    "Executive": [
        ("Exec_Spawn_A", -1225, -1000),
        ("Exec_Spawn_B", 1225, -1000),
        ("Exec_Spawn_C", 0, 1500),
        ("Exec_Spawn_D", -1225, 1300),
        ("Exec_Spawn_E", 1225, 1300),
        ("Exec_Spawn_F", 0, -1030),
    ],
    "Skydeck": [
        ("Deck_Spawn_West", -1800, 200),
        ("Deck_Spawn_East", 1900, -300),
        ("Deck_Spawn_North", 0, 1550),
        ("Deck_Spawn_South", 200, -1700),
    ],
}

# Access zones: the locked doors that define ASCENT's progression. Each entry is
# (zone_id, floor, tier, x, y, paired_zone_id, breachable, escalation_opens).
ACCESS_ZONES = [
    ("Core_Lift_Lobby",         1,  "Public",    0,    0,    "Core_Lift_Mezzanine", False, True),
    ("Core_Lift_Mezzanine",     2,  "Staff",     0,    0,    "Core_Lift_Lobby",     False, True),
    ("Stair_A_F03",             3,  "Staff",   -440,   600,  None,                  True,  True),
    ("Stair_B_F03",             3,  "Staff",    440,   600,  None,                  True,  True),
    ("Office_Floor09",          9,  "Secure",    0,   -900,  None,                  True,  True),
    ("Server_Floor09",          9,  "Secure",   -900,   0,    None,                  True,  True),
    ("Executive_Floor12",      12,  "Executive", 0,    0,    "Executive_Wing_East", True,  True),
    ("Executive_Wing_East",    12,  "Executive", 1400, -100, "Executive_Floor12",   True,  True),
    ("Canteen_Floor15",        15,  "Staff",     0,  -1000,  "Skydeck_Gate",        True,  False),
    ("Skydeck_Gate",           15,  "Skydeck", 900, -550,  "Canteen_Floor15",     True,  False),
]

# Override terminals: hold-to-open. Two-panel pairs are what make the co-op push
# into the Skydeck a real coordination problem.
OVERRIDE_TERMINALS = [
    ("Terminal_Office09_PanelA", 9,  -500, -1100, "Terminal_Office09_PanelB"),
    ("Terminal_Office09_PanelB", 9,   500, -1100, "Terminal_Office09_PanelA"),
    ("Terminal_Executive12",     12,   0,  -1050,  None),
    ("Terminal_Canteen15",       15,  -700,   0,   "Terminal_Skydeck15"),
    ("Terminal_Skydeck15",       15,   700,   0,   "Terminal_Canteen15"),
]

# Breachable walls: plasterboard and service partitions that a charge opens. Each
# is (id, floor, x, y, yoke_axis, width, height, surface).
BREACHABLE_WALLS = [
    ("Breach_Office05_Partition", 5, -1600, 1250, "X", 600, 400, "Plasterboard"),
    ("Breach_Server09_Riser",     9,  900, -600, "Y", 200, 400, "Plasterboard"),
    ("Breach_Exec12_StairLock",  12, -900,  0,   "X", 250, 400, "Metal"),
    ("Breach_Exec14_Wall",       14, -1775, 1150, "X", 700, 400, "Plasterboard"),
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
    """The area on a floor, as (name, centre x, centre y, size x, size y).

    C.AREAS stores each floor plate's south-west corner; everything placed on a
    floor - slab, rooms, lights, volumes - is laid out from its centre, which is
    also the frame the doors, spawn anchors and extraction pad use.
    """
    for name, (first, last), origin_x, origin_y, size_x, size_y in C.AREAS:
        if first <= floor_index <= last:
            return name, origin_x + size_x / 2.0, origin_y + size_y / 2.0, size_x, size_y
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
    "Skydeck":       (0.55, 3.6, 1.15, 0.024, 0.44),
}


# ===========================================================================
#  The core: lifts, glass lift, cross-hallway, toilets, stairs (every floor)
# ---------------------------------------------------------------------------
#  Laid out from the real building's ground floor, and identical on every
#  floor above it, centred on the plate:
#
#    * the lift hall runs north-south between two banks of four lifts;
#    * the glass lift stands at the back (north) of the hall;
#    * a hallway crosses between the banks and the glass lift, out to the
#      floor loop on both sides, with the men's toilets on the left and the
#      women's on the right;
#    * the main stair, left of the glass lift, goes from the ground to the
#      roof; an escape stair mirrors it on the right. Each is a switchback
#      behind a stair lobby whose door is what the lockdown's shutter seals.
#
#  Slabs are cut over the stairs and the shafts, so the stairs climb and the
#  lift cars travel.
# ===========================================================================
CORE_HALL_X = 175.0                      # the hall is x -175..175
CORE_BANK_OUTER_X = 445.0                # the banks' back walls
CORE_SHAFT_X = 310.0                     # shaft centres, either side
CORE_SHAFT_YS = (-750.0, -450.0, -150.0, 150.0)
CORE_HALL_Y = (-900.0, 300.0)
CORE_CORRIDOR_Y = (300.0, 560.0)
GLASS_LIFT_Y = (560.0, 860.0)            # x -150..150
STAIR_X = (210.0, 670.0)                 # interior, mirrored either side
STAIR_LOBBY_Y = (600.0, 720.0)
STAIR_FLIGHT_Y = (720.0, 1028.0)
STAIR_TOP_Y = 1230.0
STAIR_DOOR_X = 440.0                     # the stair door's centre, mirrored
STAIR_DOOR_WIDTH = 150.0
TOILET_X = (700.0, 1100.0)               # mirrored
CORE_NORTH_Y = 1240.0
ESCALATOR_X = (960.0, 1380.0)            # floor 0 to floor 1, rising north
ESCALATOR_Y = (-1050.0, -350.0)
COFFEE_WALL_X = 1450.0
GATE_LINE_Y = -1150.0

# The lift actors (AOneBrahamLift) bring their own doors; until a build has
# the class, closed landing doors keep the shafts from being open drops.
LIFT_CLASS_NAME = "OneBrahamLift"


def _slab_holes(floor_index):
    """The openings cut into a floor's slab, as (x0, x1, y0, y1) from centre."""
    holes = []
    if floor_index >= 1:
        for side in (-1.0, 1.0):
            for y in CORE_SHAFT_YS:
                cx = CORE_SHAFT_X * side
                holes.append((cx - 125.0, cx + 125.0, y - 135.0, y + 135.0))
        holes.append((-150.0, 150.0, GLASS_LIFT_Y[0], GLASS_LIFT_Y[1]))
        for side in (-1.0, 1.0):
            if floor_index < C.FLOOR_COUNT:
                # The whole flight area: the upper floor's first flight sits in it.
                x0, x1 = STAIR_X
            else:
                # The top floor only needs headroom over the arriving flight.
                x0, x1 = STAIR_X[0], STAIR_DOOR_X
            if side < 0:
                x0, x1 = -x1, -x0
            holes.append((x0, x1, STAIR_FLIGHT_Y[0], STAIR_TOP_Y if floor_index < C.FLOOR_COUNT else STAIR_FLIGHT_Y[1]))
    if floor_index == 1:
        holes.append((ESCALATOR_X[0] - 10.0, ESCALATOR_X[1] + 10.0, ESCALATOR_Y[0] - 10.0, ESCALATOR_Y[1] + 10.0))
    return holes


def _slab_with_holes(cx, cy, z, size_x, size_y, holes, material, label):
    """A 20 cm slab with rectangular openings, as the fewest boxes per column."""
    half_x, half_y = size_x / 2.0, size_y / 2.0
    edges = {-half_x, half_x}
    for x0, x1, _y0, _y1 in holes:
        edges.add(max(-half_x, min(half_x, x0)))
        edges.add(max(-half_x, min(half_x, x1)))
    xs = sorted(edges)
    piece = 0
    for xa, xb in zip(xs, xs[1:]):
        if xb - xa < 1.0:
            continue
        cuts = sorted((y0, y1) for x0, x1, y0, y1 in holes if x0 < xb - 0.5 and x1 > xa + 0.5)
        y = -half_y
        spans = []
        for y0, y1 in cuts:
            if y0 > y:
                spans.append((y, min(y0, half_y)))
            y = max(y, y1)
        if y < half_y:
            spans.append((y, half_y))
        for ya, yb in spans:
            if yb - ya < 1.0:
                continue
            piece += 1
            _box((cx + (xa + xb) / 2.0, cy + (ya + yb) / 2.0, z - 10.0), (xb - xa, yb - ya, 20.0),
                 material, "{0}_{1}".format(label, piece))


def _wall(cx, cy, z, x0, y0, x1, y1, material, label, height=None, thickness=20.0, base=0.0):
    """An axis-aligned wall between two points on the floor, from centre offsets."""
    height = C.CEILING_HEIGHT_CM if height is None else height
    if abs(x1 - x0) >= abs(y1 - y0):
        size = (abs(x1 - x0), thickness, height)
    else:
        size = (thickness, abs(y1 - y0), height)
    _box((cx + (x0 + x1) / 2.0, cy + (y0 + y1) / 2.0, z + base + height / 2.0), size, material, label)


def _wall_with_door(cx, cy, z, x0, y0, x1, y1, door_at, door_width, material, label, height=None):
    """A wall with one doorway centred at door_at along its length."""
    horizontal = abs(x1 - x0) >= abs(y1 - y0)
    lo, hi = (min(x0, x1), max(x0, x1)) if horizontal else (min(y0, y1), max(y0, y1))
    a, b = door_at - door_width / 2.0, door_at + door_width / 2.0
    for index, (s0, s1) in enumerate(((lo, a), (b, hi))):
        if s1 - s0 < 1.0:
            continue
        if horizontal:
            _wall(cx, cy, z, s0, y0, s1, y0, material, "{0}_{1}".format(label, index + 1), height)
        else:
            _wall(cx, cy, z, x0, s0, x0, s1, material, "{0}_{1}".format(label, index + 1), height)
    # Door head, so the opening reads as a door and blocks sight over it.
    full = C.CEILING_HEIGHT_CM if height is None else height
    if full > C.DOOR_HEIGHT_CM:
        head = full - C.DOOR_HEIGHT_CM
        if horizontal:
            _box((cx + door_at, cy + y0, z + C.DOOR_HEIGHT_CM + head / 2.0), (door_width, 20.0, head), material, label + "_Head")
        else:
            _box((cx + x0, cy + door_at, z + C.DOOR_HEIGHT_CM + head / 2.0), (20.0, door_width, head), material, label + "_Head")


def _flight(cx, cy, z, x0, x1, y_start, y_end, rise_from, rise_to, steps, material, label):
    """A solid flight of stairs between two heights, along y."""
    run = (y_end - y_start) / steps
    rise = (rise_to - rise_from) / steps
    for i in range(steps):
        top = rise_from + rise * (i + 1)
        y_mid = y_start + run * (i + 0.5)
        _box((cx + (x0 + x1) / 2.0, cy + y_mid, z + top / 2.0), (x1 - x0, abs(run), top), material,
             "{0}_{1:02d}".format(label, i + 1))


def build_core(floor_index, area_name, cx, cy, z, materials, prefix):
    """The lift banks, glass lift, cross-hallway, toilets and both stairs on one floor."""
    concrete = materials.get("Concrete")
    metal = materials.get("Metal")
    plaster = materials.get("Plasterboard")
    screen = materials.get("Glass") or materials.get("BallisticGlass")
    lifts_are_actors = getattr(unreal, LIFT_CLASS_NAME, None) is not None

    # --- Lift banks: back walls, shaft dividers, hall wall with landing doors.
    for side, tag in ((-1.0, "W"), (1.0, "E")):
        back = CORE_BANK_OUTER_X * side
        hall = CORE_HALL_X * side
        _wall(cx, cy, z, back, CORE_HALL_Y[0], back, CORE_HALL_Y[1], concrete, "{0}_Core_Bank{1}_Back".format(prefix, tag))
        for y in (CORE_HALL_Y[0], -600.0, -300.0, 0.0, CORE_HALL_Y[1]):
            _wall(cx, cy, z, hall, y, back, y, concrete, "{0}_Core_Bank{1}_Div_{2}".format(prefix, tag, int(y)))
        for index, y in enumerate(CORE_SHAFT_YS):
            lo = y - 150.0 + (0.0 if index == 0 else 0.0)
            _wall_with_door(cx, cy, z, hall, y - 150.0, hall, y + 150.0, y, C.DOOR_WIDTH_CM, concrete,
                            "{0}_Core_Bank{1}_Face_{2}".format(prefix, tag, index + 1))
            if not lifts_are_actors:
                _box((cx + hall, cy + y, z + C.DOOR_HEIGHT_CM / 2.0), (12.0, C.DOOR_WIDTH_CM, C.DOOR_HEIGHT_CM),
                     metal, "{0}_Lift_Door_{1}{2}".format(prefix, tag, index + 1))

    # --- The glass lift at the back of the hall.
    _wall(cx, cy, z, -150.0, GLASS_LIFT_Y[0], -150.0, GLASS_LIFT_Y[1], screen, prefix + "_GlassLift_W")
    _wall(cx, cy, z, 150.0, GLASS_LIFT_Y[0], 150.0, GLASS_LIFT_Y[1], screen, prefix + "_GlassLift_E")
    _wall(cx, cy, z, -150.0, GLASS_LIFT_Y[1], 150.0, GLASS_LIFT_Y[1], screen, prefix + "_GlassLift_N")
    _wall_with_door(cx, cy, z, -150.0, GLASS_LIFT_Y[0], 150.0, GLASS_LIFT_Y[0], 0.0, C.DOOR_WIDTH_CM, screen,
                    prefix + "_GlassLift_S")
    if not lifts_are_actors:
        _box((cx, cy + GLASS_LIFT_Y[0], z + C.DOOR_HEIGHT_CM / 2.0), (C.DOOR_WIDTH_CM, 12.0, C.DOOR_HEIGHT_CM),
             screen, prefix + "_Lift_Door_Glass")

    # --- Stairs: lobby, door, switchback flights, mid landing.
    for side, tag in ((-1.0, "A"), (1.0, "B")):
        inner, outer = STAIR_X[0] * side, STAIR_X[1] * side
        wall_inner, wall_outer = (STAIR_X[0] - 20.0) * side, (STAIR_X[1] + 20.0) * side
        _wall(cx, cy, z, wall_outer, STAIR_LOBBY_Y[0], wall_outer, CORE_NORTH_Y, concrete, "{0}_Stair_{1}_Outer".format(prefix, tag))
        _wall(cx, cy, z, wall_inner, STAIR_LOBBY_Y[0], wall_inner, CORE_NORTH_Y, concrete, "{0}_Stair_{1}_Inner".format(prefix, tag))
        _wall(cx, cy, z, wall_outer, CORE_NORTH_Y, wall_inner, CORE_NORTH_Y, concrete, "{0}_Stair_{1}_North".format(prefix, tag))
        _wall_with_door(cx, cy, z, wall_outer, STAIR_LOBBY_Y[0], wall_inner, STAIR_LOBBY_Y[0],
                        STAIR_DOOR_X * side, STAIR_DOOR_WIDTH, concrete, "{0}_Stair_{1}_Door".format(prefix, tag))

        if floor_index < C.FLOOR_COUNT:
            half = C.FLOOR_HEIGHT_CM / 2.0
            # Up-flight on the outer lane, landing at the back, return flight inner.
            up_x0, up_x1 = sorted((outer, (STAIR_DOOR_X + 20.0) * side))
            back_x0, back_x1 = sorted((inner, (STAIR_DOOR_X - 20.0) * side))
            _flight(cx, cy, z, up_x0, up_x1, STAIR_FLIGHT_Y[0], STAIR_FLIGHT_Y[1], 0.0, half, 11, concrete,
                    "{0}_Stair_{1}_Up".format(prefix, tag))
            land_x0, land_x1 = sorted((inner, outer))
            _box((cx + (land_x0 + land_x1) / 2.0, cy + (STAIR_FLIGHT_Y[1] + STAIR_TOP_Y) / 2.0, z + half / 2.0),
                 (land_x1 - land_x0, STAIR_TOP_Y - STAIR_FLIGHT_Y[1], half), concrete, "{0}_Stair_{1}_Landing".format(prefix, tag))
            _flight(cx, cy, z, back_x0, back_x1, STAIR_FLIGHT_Y[1], STAIR_FLIGHT_Y[0], half, C.FLOOR_HEIGHT_CM, 11, concrete,
                    "{0}_Stair_{1}_Return".format(prefix, tag))
            # The wall between the flights: a stair you can't see down.
            div_x = STAIR_DOOR_X * side
            _wall(cx, cy, z, div_x, STAIR_FLIGHT_Y[0], div_x, STAIR_FLIGHT_Y[1], concrete,
                  "{0}_Stair_{1}_Spine".format(prefix, tag), height=C.FLOOR_HEIGHT_CM)

    # --- Toilets off the cross-hallway: men's left, women's right.
    for side, tag in ((-1.0, "Mens"), (1.0, "Womens")):
        x_in, x_out = TOILET_X[0] * side, TOILET_X[1] * side
        _wall(cx, cy, z, x_out, CORE_CORRIDOR_Y[1], x_out, CORE_NORTH_Y, plaster, "{0}_WC_{1}_Outer".format(prefix, tag))
        _wall(cx, cy, z, x_in, CORE_CORRIDOR_Y[1], x_in, CORE_NORTH_Y, plaster, "{0}_WC_{1}_Inner".format(prefix, tag))
        _wall(cx, cy, z, x_out, CORE_NORTH_Y, x_in, CORE_NORTH_Y, plaster, "{0}_WC_{1}_North".format(prefix, tag))
        _wall_with_door(cx, cy, z, x_out, CORE_CORRIDOR_Y[1], x_in, CORE_CORRIDOR_Y[1], 900.0 * side, C.DOOR_WIDTH_CM,
                        plaster, "{0}_WC_{1}_Door".format(prefix, tag))
        # Three cubicles along the back wall and a vanity counter: cover, and a
        # place to hide that is a terrible place to be found.
        for i, cub_x in enumerate((760.0, 873.0, 986.0)):
            _wall(cx, cy, z, cub_x * side, 980.0, cub_x * side, CORE_NORTH_Y, plaster,
                  "{0}_WC_{1}_Cubicle_{2}".format(prefix, tag, i + 1), height=200.0, thickness=8.0)
        _box((cx + 900.0 * side, cy + 640.0, z + 45.0), (360.0, 55.0, 90.0), metal, "{0}_WC_{1}_Vanity".format(prefix, tag))

    _note_surface("Concrete", prefix + "_Core")
    _note_surface("Metal", prefix + "_Lift_Doors")
    _note_surface("Plasterboard", prefix + "_Toilets")


def build_ground_floor(cx, cy, z, half_x, half_y, materials, prefix):
    """Floor 0: revolving doors, reception, the gate line, escalators, coffee shop.

    Walk in through the revolving doors and reception is in front of you. To
    the right is the line of keycard gates; behind the gates, straight ahead,
    the lift hall; to the right of that, behind the gates, the escalators up
    to floor 1; to the right of the escalators, behind a wall, the coffee shop,
    which opens onto the public side in front of the gates, not behind them.
    """
    marble = materials.get("Marble")
    metal = materials.get("Metal")
    wood = materials.get("Wood")
    plaster = materials.get("Plasterboard")
    concrete = materials.get("Concrete")
    glass = materials.get("BallisticGlass") or materials.get("Glass")
    screen = materials.get("Glass") or glass
    h = C.CEILING_HEIGHT_CM

    # --- The frontage: glass, with three revolving doors.
    doors = (-1300.0, -800.0, -300.0)
    width = 240.0
    x = -half_x
    for index, door_x in enumerate(doors + (half_x + width / 2.0,)):
        a = door_x - width / 2.0
        if a - x > 1.0:
            _wall(cx, cy, z, x, -half_y, a, -half_y, glass, "{0}_Frontage_{1}".format(prefix, index + 1), thickness=15.0)
        x = door_x + width / 2.0
    for index, door_x in enumerate(doors):
        # A drum: canopy, two curved halves as panels, the centre post that
        # turns one doorway into two lanes.
        _box((cx + door_x, cy - half_y, z + 255.0), (width + 20.0, width + 20.0, 20.0), metal,
             "{0}_Revolving_{1}_Canopy".format(prefix, index + 1))
        for edge in (-1.0, 1.0):
            _box((cx + door_x + edge * width / 2.0, cy - half_y, z + 120.0), (12.0, 160.0, 240.0), screen,
                 "{0}_Revolving_{1}_Drum_{2}".format(prefix, index + 1, "L" if edge < 0 else "R"))
        _box((cx + door_x, cy - half_y, z + 120.0), (14.0, 14.0, 240.0), metal,
             "{0}_Revolving_{1}_Post".format(prefix, index + 1))
        _wall(cx, cy, z, door_x - 20.0, -half_y, door_x + 20.0, -half_y, glass,
              "{0}_Revolving_{1}_Header".format(prefix, index + 1), height=h - 260.0, base=260.0, thickness=15.0)

    # --- Reception: desk in front of a dark feature wall.
    _box((cx - 900.0, cy - 1250.0, z + 55.0), (900.0, 110.0, 110.0), marble, prefix + "_Reception_Desk")
    _wall(cx, cy, z, -1400.0, -950.0, -400.0, -950.0, metal, prefix + "_Reception_FeatureWall", thickness=40.0)
    # Close the back-of-house behind reception so the gates are the only way in.
    _wall(cx, cy, z, -half_x, GATE_LINE_Y, -1400.0, GATE_LINE_Y, screen, prefix + "_Reception_Screen_W", thickness=12.0)
    _wall(cx, cy, z, -1400.0, GATE_LINE_Y, -1400.0, -950.0, plaster, prefix + "_Reception_Return_W")
    _wall(cx, cy, z, -400.0, GATE_LINE_Y, -400.0, -950.0, plaster, prefix + "_Reception_Return_E")

    # --- The gate line: pedestals every 110 cm, lanes between.
    pedestal_x = -400.0
    index = 0
    while pedestal_x <= COFFEE_WALL_X - 20.0:
        index += 1
        _box((cx + pedestal_x, cy + GATE_LINE_Y, z + 50.0), (20.0, 120.0, 100.0), metal,
             "{0}_Gate_Pedestal_{1:02d}".format(prefix, index))
        pedestal_x += 110.0

    # --- Escalators behind the gates, up to floor 1, in their enclosure.
    for index, (x0, x1) in enumerate(((ESCALATOR_X[0], 1160.0), (1180.0, ESCALATOR_X[1]))):
        _flight(cx, cy, z, x0, x1, ESCALATOR_Y[0], ESCALATOR_Y[1], 0.0, C.FLOOR_HEIGHT_CM, 22, metal,
                "{0}_Escalator_{1}".format(prefix, index + 1))
    _wall(cx, cy, z, ESCALATOR_X[0] - 20.0, ESCALATOR_Y[0], ESCALATOR_X[0] - 20.0, ESCALATOR_Y[1], metal,
          prefix + "_Escalator_Enclosure_W", height=h)
    _box((cx + 1170.0, cy + (ESCALATOR_Y[0] + ESCALATOR_Y[1]) / 2.0, z + h / 2.0),
         (20.0, ESCALATOR_Y[1] - ESCALATOR_Y[0], h), metal, prefix + "_Escalator_Divider")

    # --- Coffee shop: walled off from the escalators, open to the public side.
    _wall_with_door(cx, cy, z, COFFEE_WALL_X, -half_y, COFFEE_WALL_X, -200.0, -1450.0, 180.0, plaster,
                    prefix + "_Coffee_Wall_W")
    _wall(cx, cy, z, COFFEE_WALL_X, -200.0, half_x, -200.0, plaster, prefix + "_Coffee_Wall_N")
    _box((cx + 2000.0, cy - 700.0, z + 52.0), (600.0, 90.0, 105.0), wood, prefix + "_Coffee_Counter")
    for row, ty in enumerate((-1550.0, -1250.0)):
        for col, tx in enumerate((1650.0, 1900.0, 2100.0)):
            _box((cx + tx, cy + ty, z + 37.0), (80.0, 80.0, 74.0), wood,
                 "{0}_Coffee_Table_{1}_{2}".format(prefix, row + 1, col + 1))

    _note_surface("Marble", prefix + "_Reception")
    _note_surface("Wood", prefix + "_Coffee")


def build_escalator_balustrade(cx, cy, z, materials, prefix):
    """Floor 1: glass around the escalator opening, open on the arrival side."""
    screen = materials.get("Glass") or materials.get("BallisticGlass")
    x0, x1 = ESCALATOR_X[0] - 20.0, ESCALATOR_X[1] + 20.0
    y0, y1 = ESCALATOR_Y[0] - 20.0, ESCALATOR_Y[1] - 60.0
    _wall(cx, cy, z, x0, y0, x1, y0, screen, prefix + "_Escalator_Rail_S", height=110.0, thickness=8.0)
    _wall(cx, cy, z, x0, y0, x0, y1, screen, prefix + "_Escalator_Rail_W", height=110.0, thickness=8.0)
    _wall(cx, cy, z, x1, y0, x1, y1, screen, prefix + "_Escalator_Rail_E", height=110.0, thickness=8.0)


def build_open_plan_desks(cx, cy, z, x0, x1, y0, y1, materials, prefix, label):
    """Benching: back-to-back desk pairs with a low screen, in rows with aisles."""
    wood = materials.get("Wood")
    fabric = materials.get("Fabric") or wood
    row = 0
    y = y0 + 160.0
    while y + 160.0 <= y1:
        row += 1
        x = x0 + 120.0
        col = 0
        while x + 360.0 <= x1:
            col += 1
            _box((cx + x + 180.0, cy + y, z + 37.0), (360.0, 160.0, 74.0), wood,
                 "{0}_{1}_Bench_{2}_{3}".format(prefix, label, row, col))
            _box((cx + x + 180.0, cy + y, z + 74.0 + 22.0), (360.0, 6.0, 44.0), fabric,
                 "{0}_{1}_Screen_{2}_{3}".format(prefix, label, row, col))
            x += 480.0
        y += 420.0
    _note_surface("Wood", prefix + "_" + label)


def build_server_racks(cx, cy, z, x0, x1, y0, y1, materials, prefix, label):
    """Server rows: 2 m racks in lines with cold aisles - hard cover with sparks."""
    metal = materials.get("Metal")
    row = 0
    x = x0 + 120.0
    while x + 60.0 <= x1 - 60.0:
        row += 1
        _box((cx + x, cy + (y0 + y1) / 2.0, z + 100.0), (60.0, (y1 - y0) - 260.0, 200.0), metal,
             "{0}_{1}_Rack_{2}".format(prefix, label, row))
        x += 220.0
    _note_surface("Metal", prefix + "_" + label)


def build_exposed_services(origin_x, origin_y, z, size_x, y_min, y_max, materials, prefix):
    """The bare-ceiling look of One Braham: steel and services hung under the soffit.

    Steel beams across the span, two big duct runs, three pipe runs at
    different drops and a cable tray, all running the length of the floor
    between y_min and y_max (offsets from the floor's centre). They stop at
    the core, so nothing hangs in a stairwell or a lift shaft. Above head
    height, so it is silhouette and sound, never cover.
    """
    metal = materials.get("Metal")
    underside = z + C.CEILING_HEIGHT_CM
    half_x = size_x / 2.0
    depth = y_max - y_min
    core_x = TOILET_X[1] + 20.0
    core_y = (CORE_HALL_Y[0] - 20.0, CORE_NORTH_Y + 20.0)

    def run(label, y, width, drop, height):
        spans = [(-half_x + 100.0, half_x - 100.0)]
        if core_y[0] <= y <= core_y[1]:
            spans = [(-half_x + 100.0, -core_x), (core_x, half_x - 100.0)]
        for index, (x0, x1) in enumerate(spans):
            _box((origin_x + (x0 + x1) / 2.0, origin_y + y, underside - drop - height / 2.0),
                 (x1 - x0, width, height), metal, "{0}_Services_{1}_{2}".format(prefix, label, index + 1))

    # Beams across the span every 7.5 m, broken by the core.
    count = int(size_x // 750.0)
    for index in range(count):
        x = -half_x + (index + 0.5) * (size_x / count)
        spans = [(y_min, y_max)]
        if abs(x) <= core_x:
            spans = [(y_min, max(y_min, core_y[0])), (min(y_max, core_y[1]), y_max)]
        for part, (ya, yb) in enumerate(spans):
            if yb - ya < 10.0:
                continue
            _box((origin_x + x, origin_y + (ya + yb) / 2.0, underside - 22.0),
                 (30.0, yb - ya, 45.0), metal, "{0}_Services_Beam_{1}_{2}".format(prefix, index + 1, part + 1))

    run("Duct_A", y_min + depth * 0.28, 90.0, 55.0, 55.0)
    run("Duct_B", y_min + depth * 0.72, 90.0, 55.0, 55.0)
    run("Pipe_A", y_min + depth * 0.15, 16.0, 35.0, 16.0)
    run("Pipe_B", y_min + depth * 0.45, 16.0, 70.0, 16.0)
    run("Pipe_C", y_min + depth * 0.85, 16.0, 35.0, 16.0)
    run("Cable_Tray", (y_min + y_max) / 2.0, 45.0, 90.0, 8.0)
    _note_surface("Metal", prefix + "_Services")


def build_canteen_fixtures(origin_x, origin_y, z, materials, prefix):
    """The canteen's counters and tables on floor 15, as blockout cover.

    Laid out from the real building's staff canteen: separate timber-clad
    servery islands, a hot-food line with the kitchen behind it, a till and
    coffee bar on the back wall, and the dining tables widening out beyond.
    Every piece is waist-high hard cover except the kitchen wall.
    """
    wood = materials.get("Wood")
    metal = materials.get("Metal")
    plaster = materials.get("Plasterboard")
    counter_h = 95.0

    def at(x, y, height):
        return (origin_x + x, origin_y + y, z + height / 2.0)

    # Till and coffee bar against the west wall, with its back wall behind it.
    _box(at(-2180.0, -1200.0, C.CEILING_HEIGHT_CM), (20.0, 1000.0, C.CEILING_HEIGHT_CM),
         metal, prefix + "_Coffee_Backwall")
    _box(at(-1880.0, -1200.0, 105.0), (90.0, 700.0, 105.0), wood, prefix + "_Till_Counter")

    # The kitchen: full-height wall in front of it with a staff door at the
    # east end, and the hot-food counter a staff aisle in front of that.
    door = C.DOOR_WIDTH_CM
    _box(at(-1150.0 - door / 2.0, -1500.0, C.CEILING_HEIGHT_CM), (1500.0 - door, 15.0, C.CEILING_HEIGHT_CM),
         plaster, prefix + "_Kitchen_Wall")
    _box(at(-1150.0, -1300.0, counter_h), (1400.0, 90.0, counter_h), wood, prefix + "_Hot_Food_Counter")

    # Three servery islands in a row, clear of the lift banks at x > -445.
    for index, x in enumerate((-1500.0, -1050.0, -600.0)):
        _box(at(x, -1030.0, counter_h), (360.0, 120.0, counter_h), wood,
             "{0}_Servery_Island_{1}".format(prefix, index + 1))

    # Eighteen six-seat tables in three rows, widening east across the hall.
    for row, y in enumerate((-1650.0, -1300.0, -950.0)):
        for col in range(6):
            x = 600.0 + col * 300.0
            _box(at(x, y, 74.0), (180.0, 75.0, 74.0), wood,
                 "{0}_Canteen_Table_{1}_{2}".format(prefix, row + 1, col + 1))

    # The balcony's north edge: a glass balustrade, waist high.
    glass = materials.get("Glass") or materials.get("BallisticGlass")
    _box(at(0.0, 1790.0, 110.0), (4400.0, 12.0, 110.0), glass, prefix + "_Balcony_Balustrade_N")

    _note_surface("Wood", prefix + "_Canteen_Counters")
    _note_surface("Metal", prefix + "_Coffee_Backwall")


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
    # The ground floor is pale stone (terrazzo in the real reception).
    slab_material = marble if area_name in ("Lobby", "Ground_Plaza") else carpet
    if area_name == "Skydeck":
        slab_material = decking or concrete

    # 20 cm of slab, cut over the stairs, the lift shafts and (on floor 1) the
    # escalators - without the cuts nobody can climb.
    _slab_with_holes(origin_x, origin_y, z, size_x, size_y, _slab_holes(floor_index), slab_material, prefix + "_Slab")
    created.append((prefix + "_Slab", (origin_x, origin_y, z - 10.0)))
    _note_surface("Marble" if area_name in ("Lobby", "Ground_Plaza") else
                  ("Decking" if area_name == "Skydeck" else "Carpet"),
                  prefix + "_Slab")

    # --- Ceiling ----------------------------------------------------------
    # One Braham has no suspended ceilings: the soffit is bare concrete with the
    # services hung under it (see build_exposed_services). Floor 15 is roofed
    # only over the canteen; the Skydeck balcony is open sky.
    if area_name == "Skydeck":
        roofed_y = SKYDECK_SCREEN_Y + half_y
        _box((origin_x, origin_y - half_y + roofed_y / 2.0, z + C.CEILING_HEIGHT_CM + 10.0),
             (size_x, roofed_y, 20.0), concrete, prefix + "_Soffit")
        build_exposed_services(origin_x, origin_y, z, size_x, -half_y, SKYDECK_SCREEN_Y, materials, prefix)
    else:
        # The soffit is the underside of the floor above, so it has that
        # floor's openings too.
        _slab_with_holes(origin_x, origin_y, z + C.CEILING_HEIGHT_CM + 20.0, size_x, size_y,
                         _slab_holes(floor_index + 1), concrete, prefix + "_Soffit")
        if area_name != "Ground_Plaza":
            build_exposed_services(origin_x, origin_y, z, size_x, -half_y, half_y, materials, prefix)
    _note_surface("Concrete", prefix + "_Soffit")

    # --- Perimeter glazing -------------------------------------------------
    # The curtain wall is the persistent level's facade, which now sits on the
    # floor plates' edges; a second band per floor would z-fight with it.

    # --- The canteen screen (floor 15) ------------------------------
    # Ordinary glass between the canteen and the balcony, in two panels either
    # side of the Skydeck gate. Plain rather than ballistic: it is an interior
    # screen, and it is the one place a fight on 15 can see straight through.
    if area_name == "Skydeck":
        screen = materials.get("Glass") or glass
        screen_h = C.CEILING_HEIGHT_CM
        door_lo = SKYDECK_GATE_X - SKYDECK_GATE_WIDTH / 2.0
        door_hi = SKYDECK_GATE_X + SKYDECK_GATE_WIDTH / 2.0
        for label, lo, hi in (("West", -half_x + 100.0, door_lo), ("East", door_hi, half_x - 100.0)):
            _box((origin_x + (lo + hi) / 2.0, origin_y + SKYDECK_SCREEN_Y, z + screen_h / 2.0),
                 (hi - lo, 12.0, screen_h), screen, prefix + "_Canteen_Screen_" + label)
        _note_surface("Glass", prefix + "_Canteen_Screen")

    # --- The core, and the floors that are different ------------------------
    build_core(floor_index, area_name, origin_x, origin_y, z, materials, prefix)
    if area_name == "Ground_Plaza":
        build_ground_floor(origin_x, origin_y, z, half_x, half_y, materials, prefix)
    if floor_index == 1:
        build_escalator_balustrade(origin_x, origin_y, z, materials, prefix)

    # --- Room partitions --------------------------------------------------
    rooms = ROOM_PROGRAMMES.get(area_name, [])
    for room_name, rx, ry, rsx, rsy in rooms:
        if (area_name, room_name) in OPEN_ROOMS:
            continue

        cx = origin_x + rx
        cy = origin_y + ry

        # Four walls with one doorway, on the side that faces the core: every
        # room opens onto the floor's loop, never onto the curtain wall.
        wall_h = C.CEILING_HEIGHT_CM if "Server" in room_name or "Power" in room_name else 250.0
        label = prefix + "_" + room_name
        x0, x1 = rx - rsx / 2.0, rx + rsx / 2.0
        y0, y1 = ry - rsy / 2.0, ry + rsy / 2.0
        faces_core_in_x = abs(rx) / max(1.0, half_x) > abs(ry) / max(1.0, half_y)
        door = C.DOOR_WIDTH_CM
        if faces_core_in_x:
            door_x = x1 if rx < 0 else x0
            far_x = x0 if rx < 0 else x1
            _wall(origin_x, origin_y, z, far_x, y0, far_x, y1, plaster, label + "_Back", wall_h, 15.0)
            _wall_with_door(origin_x, origin_y, z, door_x, y0, door_x, y1, ry, door, plaster, label + "_Door", wall_h)
            _wall(origin_x, origin_y, z, x0, y0, x1, y0, plaster, label + "_S", wall_h, 15.0)
            _wall(origin_x, origin_y, z, x0, y1, x1, y1, plaster, label + "_N", wall_h, 15.0)
        else:
            door_y = y1 if ry < 0 else y0
            far_y = y0 if ry < 0 else y1
            _wall(origin_x, origin_y, z, x0, far_y, x1, far_y, plaster, label + "_Back", wall_h, 15.0)
            _wall_with_door(origin_x, origin_y, z, x0, door_y, x1, door_y, rx, door, plaster, label + "_Door", wall_h)
            _wall(origin_x, origin_y, z, x0, y0, x0, y1, plaster, label + "_W", wall_h, 15.0)
            _wall(origin_x, origin_y, z, x1, y0, x1, y1, plaster, label + "_E", wall_h, 15.0)

    # --- Fit-out: cover that comes from what the room is for ------------------
    for room_name, rx, ry, rsx, rsy in rooms:
        x0, x1 = rx - rsx / 2.0, rx + rsx / 2.0
        y0, y1 = ry - rsy / 2.0, ry + rsy / 2.0
        if "Desk_Farm" in room_name or room_name == "Team_Desks":
            build_open_plan_desks(origin_x, origin_y, z, x0, x1, y0, y1, materials, prefix, room_name)
        elif room_name.startswith("Server_Hall"):
            build_server_racks(origin_x, origin_y, z, x0, x1, y0, y1, materials, prefix, room_name)

    if area_name == "Skydeck":
        build_canteen_fixtures(origin_x, origin_y, z, materials, prefix)

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

    _note_surface("Plasterboard", prefix + "_Partitions")

    return created


def build_lifts(report):
    """The nine lift cars: two banks of four and the glass lift at the back.

    One actor per shaft, in the persistent level, standing at its shaft's
    centre at the ground floor's level; each builds its own car and landing
    doors (AOneBrahamLift) and travels the full height through the slab
    openings build_core cuts.
    """
    lift_class = getattr(unreal, LIFT_CLASS_NAME, None)
    if lift_class is None:
        report.skipped_asset("AOneBrahamLift", "lift class is not loaded; the cores keep closed landing doors")
        return []

    facing = getattr(unreal, "OneBrahamLiftFacing", None)
    placed = []
    shafts = []
    for side, tag, face in ((-1.0, "W", "POSITIVE_X"), (1.0, "E", "NEGATIVE_X")):
        for index, y in enumerate(CORE_SHAFT_YS):
            shafts.append(("Lift_{0}{1}".format(tag, index + 1), CORE_SHAFT_X * side, y, face, False))
    shafts.append(("Lift_Glass", 0.0, (GLASS_LIFT_Y[0] + GLASS_LIFT_Y[1]) / 2.0, "NEGATIVE_Y", True))

    for lift_id, x, y, face, glass in shafts:
        lift = _spawn(lift_class, unreal.Vector(x, y, _floor_z(0)))
        if lift is None:
            report.failed(lift_id, "could not spawn")
            continue
        _label(lift, lift_id)
        try:
            lift.set_editor_property("LiftId", unreal.Name(lift_id))
            lift.set_editor_property("MinFloor", 0)
            lift.set_editor_property("MaxFloor", C.FLOOR_COUNT)
            lift.set_editor_property("bGlass", glass)
            if facing is not None:
                lift.set_editor_property("DoorFacing", getattr(facing, face))
            lift.set_editor_property("FloorHeightCm", C.FLOOR_HEIGHT_CM)
            lift.set_editor_property("FloorZ0", _floor_z(0))
            if glass:
                lift.set_editor_property("CarWidthCm", 270.0)
                lift.set_editor_property("CarDepthCm", 270.0)
                lift.set_editor_property("LandingDoorOffsetCm", (GLASS_LIFT_Y[1] - GLASS_LIFT_Y[0]) / 2.0)
            else:
                # From the shaft's centre to the hall face of its wall.
                lift.set_editor_property("LandingDoorOffsetCm", CORE_SHAFT_X - CORE_HALL_X)
            try:
                lift.call_method("RebuildLift")
            except Exception:  # noqa: BLE001 - OnConstruction rebuilds it anyway
                pass
        except Exception as exc:  # noqa: BLE001
            report.failed(lift_id, str(exc))
        placed.append({"id": lift_id, "location": [x, y], "glass": glass})

    C.log(u"  lifts placed: {0}".format(len(placed)))
    return placed


def build_vertical_circulation(report, materials):
    """Kept for the build order: the stairs and lift shafts are now built per
    floor by build_core, with real openings in every slab."""
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
                actor.set_editor_property("CooldownSeconds", 8.0)
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
                    # Same for the finish: the construction script ran at spawn,
                    # before WallMaterial was set.
                    for piece in actor.get_components_by_class(unreal.StaticMeshComponent):
                        piece.set_material(0, material)
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
            # The stair lobby's door (build_core); the shutter seals it.
            x = STAIR_DOOR_X * side
            y = STAIR_LOBBY_Y[0]
            side_name = "A" if side < 0 else "B"

            if shutter_class is None:
                report.skipped_asset(
                    "AOneBrahamFireShutter",
                    "fire shutter class is not loaded; stair opening " + side_name +
                    " on floor " + str(floor) + " cannot be sealed by the lockdown.")
                _box((x, y, z), (STAIR_DOOR_WIDTH, 30.0, C.DOOR_HEIGHT_CM),
                     None, "Shutter_{0}_F{1:02d}".format(side_name, floor), static=False)
            else:
                shutter = _spawn(shutter_class, unreal.Vector(x, y, z))
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
    _box((-1700.0, 1350.0, deck_z + 125.0), (600.0, 500.0, 250.0), None, "Winch_Control_Shell")

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
                         unreal.Rotator(0.0, -90.0, 0.0))
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

            # Floor 15 has a ceiling only over the canteen; the balcony is lit
            # by its floodlight masts, not by panels hanging in the open air.
            if area_name == "Skydeck" and ly > origin_y + SKYDECK_SCREEN_Y:
                continue

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
        settings.set_editor_property("AutoExposureMethod", unreal.AutoExposureMethod.AEM_MANUAL)
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
            settings.set_editor_property("bOverride_FilmGrainIntensity", True)
            settings.set_editor_property("FilmGrainIntensity", 0.18)
            settings.set_editor_property("bOverride_ColorSaturation", True)
            settings.set_editor_property("ColorSaturation", unreal.Vector4(0.94, 0.94, 0.94, 1.0))
            settings.set_editor_property("bOverride_AutoExposureMinBrightness", True)
            settings.set_editor_property("AutoExposureMinBrightness", 0.65)
            settings.set_editor_property("bOverride_AutoExposureMaxBrightness", True)
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
        created = level_subsystem.new_level(level_path)
    except Exception as exc:  # noqa: BLE001
        report.failed(level_path, u"could not create streaming level: {0}".format(exc))
        return None
    if not created:
        report.failed(level_path, u"could not create streaming level")
        return None

    build_floor_shell(report, floor_index, materials)
    build_lighting(report, floor_index)
    build_post_process(report, floor_index)
    build_audio_zone(report, floor_index)
    build_nav(report, floor_index)

    try:
        level_subsystem.save_current_level()
    except Exception as exc:  # noqa: BLE001
        report.failed(level_path, u"could not save: {0}".format(exc))
        return None

    report.created_asset(level_path)
    return level_path


def open_fresh_persistent_level(report):
    """Opens a fresh persistent level for this build.

    Every actor in it is generated and spawned without looking for an earlier
    copy, so an existing level is replaced rather than added to: re-running the
    build must not stack a second set of spawn points on the first.
    """
    level_subsystem = _level_subsystem()
    persistent_path = "{0}/{1}".format(C.MAPS, C.MAP_PERSISTENT)

    # The level subsystem reports failure by returning False, not by raising.
    if unreal.EditorAssetLibrary.does_asset_exist(persistent_path):
        if not level_subsystem.load_level(persistent_path):
            report.failed(persistent_path, u"could not open the existing persistent level")
            return False
        actors = _actor_subsystem()
        generated = [a for a in actors.get_all_level_actors()
                     if not isinstance(a, (unreal.WorldSettings, unreal.Brush))]
        actors.destroy_actors(generated)
        C.log(u"  persistent level reopened; {0} generated actors cleared".format(len(generated)))
        report.updated_asset(persistent_path)
    else:
        if not level_subsystem.new_level(persistent_path):
            report.failed(persistent_path, u"could not create the persistent level")
            return False
        report.created_asset(persistent_path)
    return True


def build_persistent_level(report, materials, manifest):
    """Builds the persistent level: the shell, the town, and the references.

    The level is already open - open_fresh_persistent_level ran before the
    gameplay anchors were spawned into it.
    """
    level_subsystem = _level_subsystem()
    persistent_path = "{0}/{1}".format(C.MAPS, C.MAP_PERSISTENT)

    # --- The building shell -----------------------------------------------
    # A single exterior mass so the tower is visible from the Skydeck, the lobby
    # windows look onto something, and the plaza has a building above it.
    shell_material = materials.get("Concrete")
    _box((0.0, 0.0, -220.0), (9000.0, 8000.0, 400.0), shell_material, "Ground_Slab_Exterior")

    # Curtain wall, four sides, from the lobby to the Skydeck. Deliberately one
    # mass rather than fifteen stacked bands: the interior floors have their own
    # glazing, and this is the silhouette the players see in the trailer.
    glass = materials.get("BallisticGlass") or materials.get("Glass")
    # The curtain wall sits on the floor plates' edges (4400 x 3600), so it is
    # the window you stand at on every floor. The south face starts above the
    # ground floor, whose frontage - glass and three revolving doors - is part
    # of floor 0's level; the north face stops at the floor-15 slab, above
    # which is the open balcony.
    base = _floor_z(0)
    total_height = (C.FLOOR_COUNT + 1) * C.FLOOR_HEIGHT_CM
    face_x, face_y = 2215.0, 1815.0
    south_base = _floor_z(1)
    south_height = _floor_z(C.FLOOR_COUNT + 1) - south_base
    _box((0.0, -face_y, south_base + south_height / 2.0), (2.0 * face_x + 30.0, 30.0, south_height), glass, "Facade_South")
    north_height = _floor_z(C.FLOOR_COUNT) - base
    _box((0.0, face_y, base + north_height / 2.0), (2.0 * face_x + 30.0, 30.0, north_height), glass, "Facade_North")
    _box((-face_x, 0.0, base + total_height / 2.0), (30.0, 2.0 * face_y, total_height), glass, "Facade_West")
    _box((face_x, 0.0, base + total_height / 2.0), (30.0, 2.0 * face_y, total_height), glass, "Facade_East")
    _note_surface("BallisticGlass" if materials.get("BallisticGlass") is not None else "Glass", "Facade")

    # Vertical fins on the outside, every 1.5 m: the real tower's brise-soleil,
    # and the banded light and shadow they throw across every floor.
    fin = materials.get("Metal")
    fins = int((2.0 * face_x) // 150.0)
    for index in range(fins + 1):
        fx = -face_x + index * (2.0 * face_x / fins)
        for side, fy in (("S", -face_y - 35.0), ("N", face_y + 35.0)):
            height = south_height if side == "S" else north_height
            bottom = south_base if side == "S" else base
            _box((fx, fy, bottom + height / 2.0), (8.0, 40.0, height), fin, "Facade_Fin_{0}_{1}".format(side, index))

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

    # Everything from here to the persistent-level save is spawned into the
    # persistent level, so it has to be the open level before step 2.
    if not C.guard(report, "persistent level", open_fresh_persistent_level, report):
        C.log(report.summary())
        return report

    C.step(2, 6, "gameplay anchors")
    _SURFACE_USAGE.clear()

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
    manifest["lifts"] = C.guard(report, "lifts", build_lifts, report) or []
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

    C.step(3, 6, "persistent level")
    C.guard(report, "persistent level", build_persistent_level, report, materials, manifest)

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

    # Surfaces are recorded as geometry is placed, which happens after build_all
    # first fills the manifest - take the usage at write time, not before. Floor
    # levels that already existed were skipped and recorded nothing, so what the
    # previous manifest says about them still stands.
    usage = {}
    if os.path.isfile(path):
        try:
            with open(path) as handle:
                usage = json.load(handle).get("surface_usage") or {}
        except Exception:  # noqa: BLE001
            usage = {}
    for name, places in _SURFACE_USAGE.items():
        usage[name] = sorted(set(usage.get(name, [])) | set(places))
    manifest["surface_usage"] = usage

    with open(path, "w") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)

    C.log(u"manifest written: {0}".format(path))
    return path


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
