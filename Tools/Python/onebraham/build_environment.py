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
SPHERE_MESH = "/Engine/BasicShapes/Sphere.Sphere"  # 100 cm diameter: tree crowns
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
    ("OpenPlan", "Break_Out_Kitchen"): "Wood",
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

    # The crown: the suite on carpet, the lounge and the atrium garden on
    # timber; the Winter Garden on timber decking, the terrace on stone.
    ("Summit", "Management_Suite"):  "Carpet",
    ("Summit", "Collab_Lounge"):     "Wood",
    ("Summit", "Atrium_Garden"):     "Wood",
    ("WinterGarden", "Winter_Garden"): "Wood",
    ("WinterGarden", "Roof_Terrace"):  "Concrete",
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
    "Summit":        "Carpet",
    "WinterGarden":  "Decking",
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
        ("Reception_Foyer", 500, -1475, 3300, 650),
        ("Security_Office", 1800, -500, 800, 900),
        ("Post_Room", 1800, 650, 800, 900),
        ("Bike_Store", -1800, 1500, 800, 600),
    ],
    "Lobby": [
        # West of the reception void: the gallery's floor stops at its edge.
        ("Arrival_Gallery", -925, -1475, 2450, 650),
        # Starts 2.5 m back from the reception void: the feature stair lands there.
        ("Client_Showcase", 1775, 225, 750, 1850),
        ("Lab", -1775, 600, 750, 1300),
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
    # The crown over the canteen. 16: the Management Suite - the real
    # building's top office floor - beside a lounge and the foot of the atrium.
    "Summit": [
        ("Management_Suite", 1500, -1300, 1300, 800),
        ("Collab_Lounge", -150, -1175, 1000, 1250),
        ("Atrium_Garden", -1450, -1175, 1300, 1250),
    ],
    # 17: the Winter Garden round the atrium void, and the open roof terrace.
    "WinterGarden": [
        ("Winter_Garden", 200, -1175, 1100, 1250),
        ("Roof_Terrace", 1500, -1175, 1400, 1250),
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
    ("Summit", "Collab_Lounge"),
    ("Summit", "Atrium_Garden"),
    ("WinterGarden", "Winter_Garden"),
    ("WinterGarden", "Roof_Terrace"),
}

# Floor 15's split: the canteen is roofed and glazed south of this line, the
# balcony open to the north. The Skydeck gate is the door in the screen.
SKYDECK_SCREEN_Y = -550.0
SKYDECK_GATE_X = 900.0
SKYDECK_GATE_WIDTH = 300.0

# The crown: floors 16 and 17 stand over the canteen half (south of the screen
# line) and over the core stalk, which already rises through the balcony; the
# rest of the plate is the open Skydeck. CROWN_OUT is the balcony's footprint,
# cut out of the crown floors' slabs. On 17 the atrium opens down to 16, and
# the east end of the canteen block is the open roof terrace.
CROWN_STALK_X = 1120.0
CROWN_NORTH_Y = 1260.0
CROWN_OUT = [
    (-2200.0, -CROWN_STALK_X, SKYDECK_SCREEN_Y, 1800.0),
    (CROWN_STALK_X, 2200.0, SKYDECK_SCREEN_Y, 1800.0),
    (-CROWN_STALK_X, CROWN_STALK_X, CROWN_NORTH_Y, 1800.0),
]
ATRIUM_VOID = (-1950.0, -850.0, -1550.0, -800.0)
TERRACE_17 = (800.0, 2200.0, -1800.0, SKYDECK_SCREEN_Y)
WINTER_GARDEN_EAST_X = 780.0

# The reception is double height, as the real one is: floor 1's slab is open
# over the reception side, from the frontage back to the feature wall.
RECEPTION_VOID = (300.0, 2200.0, -1800.0, -955.0)

# The main stair, to the left of the glass lift as you face it from the lift
# hall (+x), is a spiral that runs the full height of the tower: one turn per
# storey round a central column, entered through the stair door. The other core
# stair (-x) is the switchback fire stair - the real building has two.
SPIRAL_SIDE = 1.0
SPIRAL_CENTRE = (440.0, 965.0)
SPIRAL_R_OUT = 205.0
SPIRAL_R_IN = 22.0
SPIRAL_STEPS = 20                 # per storey: 20 cm rise, 18 degrees each
SPIRAL_HOLE = (SPIRAL_CENTRE[0] - 215.0, SPIRAL_CENTRE[0] + 215.0,
               SPIRAL_CENTRE[1] - 215.0, SPIRAL_CENTRE[1] + 215.0)

# The reception's feature stair: an open steel stair with timber treads along
# the east glazing, from the public foyer up to the floor-1 gallery.
FEATURE_STAIR_X = (1960.0, 2160.0)

# Floor 1, the top of the escalators: the wall straight ahead of the riders.
ESCALATOR_ARRIVAL_WALL_Y = -80.0
FEATURE_STAIR_Y = (-1571.0, -955.0)     # bottom (ground) -> top (floor-1 slab edge)
FEATURE_STAIR_STEPS = 22


# The "soft corners" of the real curtain wall: the glass turns each corner on
# a 240 cm radius from floor 1 up, and the slabs are cut back to sit inside it.
SOFT_CORNER_R = 240.0          # at the glass plane (2215 x 1815)
SLAB_CORNER_R = 225.0          # at the slab edge, 15 cm inside the glass


def _corner_cuts():
    """Stepped rectangles that remove the slab outside each rounded corner."""
    r = SLAB_CORNER_R
    def outside(u):           # how far in from the corner the slab must go, at u along the edge
        return r - math.sqrt(max(0.0, r * r - (r - u) ** 2))
    bounds = [0.0, 12.0, 30.0, 55.0, 85.0, 120.0, 160.0, 200.0, r]
    cuts = []
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            for u0, u1 in zip(bounds, bounds[1:]):
                v = outside(u0)
                if v < 1.0:
                    continue
                xa, xb = 2200.0 - u1, 2200.0 - u0
                ya, yb = 1800.0 - v, 1800.0
                rx = (xa, xb) if sx > 0 else (-xb, -xa)
                ry = (ya, yb) if sy > 0 else (-yb, -ya)
                cuts.append((rx[0], rx[1], ry[0], ry[1]))
                # and the mirror across the diagonal, so both edges are cut
                rx2 = (2200.0 - v, 2200.0) if sx > 0 else (-2200.0, -2200.0 + v)
                ry2 = (1800.0 - u1, 1800.0 - u0) if sy > 0 else (-1800.0 + u0, -1800.0 + u1)
                cuts.append((rx2[0], rx2[1], ry2[0], ry2[1]))
    return cuts


SOFT_CORNER_CUTS = _corner_cuts()


def _in_rect(x, y, rect):
    return rect[0] <= x <= rect[1] and rect[2] <= y <= rect[3]


def _district_material(name):
    """A finish authored by build_district (One Braham's own palette)."""
    path = "{0}/MI_OB_District_{1}".format(C.MATERIALS_INSTANCES, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    return None

# Anchors: named, gameplay-relevant positions per area. The manifest this script
# writes is the authoritative list the C++ and Docs reference.
ANCHORS = {
    # Clear floor only: the loop round the core, the bands' open zones, never
    # inside the core, a desk row or a walled room.
    "Ground_Plaza": [
        ("Plaza_Spawn_West", -1800, -1000),
        ("Plaza_Spawn_East", 1900, -1500),
        ("Plaza_Spawn_North", 0, 1500),
    ],
    "Lobby": [
        ("Lobby_Spawn_A", -1800, -1450),
        ("Lobby_Spawn_B", 900, -750),     # clear of the reception void
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
        # In front of the meeting pods, not in them: the pods fill y 1400-1750,
        # and an anchor at 1450 stood among a pod's chairs, against its door.
        ("Office_Spawn_C", -1250, 1250),
        ("Office_Spawn_D", 1250, 1250),
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

# Spawn clearance: the same capsule AOneBrahamSpawnPoint::ValidateClearance
# sweeps at BeginPlay (40 cm radius, 92 cm half height, standing 4 cm above the
# anchor), so a spawn the build passes is a spawn the game passes. The anchor is
# also where the pawn's own capsule centre lands, SPAWN_CENTRE_CM above the floor,
# so the pawn's body is tested too, from just off the floor up.
SPAWN_CENTRE_CM = 90.0
SPAWN_TEST_RADIUS_CM = 40.0
SPAWN_TEST_HALF_HEIGHT_CM = 92.0
SPAWN_TEST_LIFT_CM = 4.0
SPAWN_PAWN_HALF_HEIGHT_CM = 84.0      # bottom 6 cm off the slab, clear of rugs and mats

# Where an obstructed anchor looks for clear floor: rings round its authored
# spot, nearest first, with a wider radius than the test so the anchor that is
# chosen is off the walls and out of doorways, not merely not inside them.
SPAWN_SEARCH_RINGS_CM = (75.0, 150.0, 225.0, 300.0, 400.0, 500.0, 650.0, 800.0)
SPAWN_SEARCH_STEPS = 16
SPAWN_SEARCH_RADIUS_CM = 70.0

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


def _generated_actors(actors):
    """Everything a build placed in a level, for clearing before a rebuild.

    The only actors a level owns itself are its WorldSettings and its builder
    brush - an exact Brush. Volumes are Brush subclasses, so filtering on
    isinstance(Brush) kept every post-process, nav and audio volume: each build
    stacked another set on the last, and an old grade kept winning.
    """
    return [a for a in actors
            if not isinstance(a, unreal.WorldSettings) and type(a) is not unreal.Brush]


def _label(actor, label):
    if actor is None:
        return
    try:
        actor.set_actor_label(label)
    except Exception:  # noqa: BLE001 - labels are a nicety, never fatal
        pass


def _box(location, size_cm, material=None, label=None, static=True, mesh_path=None):
    """Places a cube scaled to `size_cm` as a single piece of geometry.

    A cube is 100 cm, so scale = cm / 100. Using one primitive for the blockout
    means the collision, the nav mesh, the physical material and the lightmaps
    are all correct from the first run: this is real, walkable geometry.
    """
    mesh = _load(mesh_path or CUBE_MESH)
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
    "Summit":        (0.65, 2.0, 0.60, 0.020, 0.32),
    "WinterGarden":  (0.60, 3.4, 1.00, 0.022, 0.40),
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
# The public side of the ground floor is -x. Facing north from the revolving
# doors, Unreal's +y forward puts -x on your RIGHT (the engine is left-handed),
# and the brief is "gates to the right, escalators right of them, coffee shop
# right of that". The core is unchanged: the stairs at +x stay on the left of
# the glass lift.
# Four escalators side by side, rising north. Their foot is set back from the
# gate line: through the main gates you turn right along the passage at their
# foot, then left up them; their own card gates face that passage from the
# public side.
ESCALATOR_X = (-1430.0, -890.0)          # floor 0 to floor 1, rising north
ESCALATOR_Y = (-900.0, -200.0)
ESCALATOR_LANES = 4
MAIN_GATES_END_X = -680.0                # the main gate line stops at the lift hall
COFFEE_WALL_X = -1450.0
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
        holes.append(SPIRAL_HOLE)
        for side in (-1.0,):
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
        holes.append(RECEPTION_VOID)
    if floor_index >= 1:
        holes.extend(SOFT_CORNER_CUTS)
    if floor_index >= 16:
        holes.extend(CROWN_OUT)
    if floor_index == 17:
        holes.append(ATRIUM_VOID)
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


def build_spiral_stair(cx, cy, z, materials, label):
    """One storey of the spiral: treads 1-19 round the column, rising from this
    floor's stair lobby to the next floor's, which is tread 20."""
    concrete = materials.get("Concrete")
    steel = _district_material("BlackCladding") or materials.get("Metal")
    rise = C.FLOOR_HEIGHT_CM / SPIRAL_STEPS
    radius = (SPIRAL_R_IN + SPIRAL_R_OUT) / 2.0
    length = SPIRAL_R_OUT - SPIRAL_R_IN
    width = 2.0 * math.pi * SPIRAL_R_OUT / SPIRAL_STEPS - 2.0
    ox, oy = SPIRAL_CENTRE
    _box((cx + ox, cy + oy, z + C.FLOOR_HEIGHT_CM / 2.0), (2.0 * SPIRAL_R_IN + 8.0, 2.0 * SPIRAL_R_IN + 8.0,
         C.FLOOR_HEIGHT_CM), steel, label + "_Column", mesh_path="/Engine/BasicShapes/Cylinder.Cylinder")
    for i in range(1, SPIRAL_STEPS):
        angle = math.radians(-90.0 + 360.0 / SPIRAL_STEPS * i)
        top = rise * i
        tread = _box((cx + ox + radius * math.cos(angle), cy + oy + radius * math.sin(angle), z + top - 3.0),
                     (length, width, 6.0), concrete, "{0}_Tread_{1:02d}".format(label, i))
        if tread is not None:
            tread.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=math.degrees(angle)), False)
        post_r = SPIRAL_R_OUT - 6.0
        _box((cx + ox + post_r * math.cos(angle), cy + oy + post_r * math.sin(angle), z + top + 50.0),
             (4.0, 4.0, 100.0), steel, "{0}_Baluster_{1:02d}".format(label, i))


def _sign(cx, cy, x, y, zc, yaw, text, size, colour, label, align="center"):
    """Wayfinding copy: unlit text that glows like an illuminated sign."""
    actor = _spawn(unreal.TextRenderActor, unreal.Vector(cx + x, cy + y, zc),
                   unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if actor is None:
        return None
    component = actor.text_render
    component.set_editor_property("text", text)
    component.set_editor_property("world_size", size)
    component.set_editor_property("horizontal_alignment", {
        "center": unreal.HorizTextAligment.EHTA_CENTER, "left": unreal.HorizTextAligment.EHTA_LEFT,
        "right": unreal.HorizTextAligment.EHTA_RIGHT}[align])
    component.set_editor_property("vertical_alignment", unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
    component.set_editor_property("text_render_color", unreal.Color(r=colour[0], g=colour[1], b=colour[2], a=255))
    glow = unreal.load_asset("/Engine/EngineMaterials/DefaultTextMaterialTranslucent.DefaultTextMaterialTranslucent")
    if glow is not None:
        component.set_material(0, glow)
    _label(actor, label)
    return actor


def _wc_fittings(cx, cy, z, side, tag, materials, prefix):
    """Inside each WC: cubicle doors, pans, basins set in the vanity, a long
    mirror, a dryer; outside, the door sign."""
    white = unreal.EditorAssetLibrary.load_asset(C.MATERIALS_INSTANCES + "/MI_OB_Brand_White") or materials.get("Plasterboard")
    steel = materials.get("Metal")
    mirror = materials.get("Glass") or steel
    label = "{0}_WC_{1}".format(prefix, tag)
    for i, (a, b) in enumerate(((704.0, 760.0), (760.0, 873.0), (873.0, 986.0))):
        mid = (a + b) / 2.0 if i else 730.0
        if i:
            # Cubicle door, ajar: a panel turned 30 degrees into the cubicle.
            panel = _box((cx + mid * side, cy + 980.0, z + 105.0), (b - a - 12.0, 4.0, 190.0), materials.get("Plasterboard"),
                         "{0}_CubicleDoor_{1}".format(label, i))
            if panel is not None:
                panel.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=side * 30.0), False)
            _box((cx + mid * side, cy + 1180.0, z + 20.0), (40.0, 55.0, 40.0), white, "{0}_Pan_{1}".format(label, i))
            _box((cx + mid * side, cy + 1222.0, z + 75.0), (38.0, 16.0, 30.0), white, "{0}_Cistern_{1}".format(label, i))
    for b in range(3):
        bx = (790.0 + b * 110.0) * side
        _box((cx + bx, cy + 640.0, z + 88.0), (44.0, 34.0, 6.0), white, "{0}_Basin_{1}".format(label, b + 1))
        _box((cx + bx, cy + 615.0, z + 110.0), (4.0, 4.0, 26.0), steel, "{0}_Tap_{1}".format(label, b + 1))
    _box((cx + 900.0 * side, cy + 606.0, z + 160.0), (340.0, 1.0, 90.0), mirror, label + "_Mirror")
    _box((cx + 1060.0 * side, cy + 700.0, z + 120.0), (24.0, 14.0, 30.0), white, label + "_Dryer")
    # Door sign on the hallway face.
    word = "MEN" if tag == "Mens" else "WOMEN"
    _sign(cx, cy, (900.0 + 85.0) * side, CORE_CORRIDOR_Y[1] - 12.0, z + 190.0, -90.0, word, 14.0,
          (235, 238, 245), label + "_Sign")


def build_wayfinding(floor_index, area_name, cx, cy, z, prefix):
    """The building's signage, on every floor: the floor number large over the
    glass lift at the end of the lift hall, a lifts sign at the hall's mouth,
    STAIRS over both stair doors, and green FIRE EXIT signs on the stair doors."""
    names = {0: "G", 15: "15"}
    number = names.get(floor_index, "{0:d}".format(floor_index))
    light = (240, 242, 248)
    _sign(cx, cy, 0.0, GLASS_LIFT_Y[0] - 14.0, z + 330.0, -90.0, number, 70.0, light, prefix + "_Sign_FloorNumber")
    area = {"Ground_Plaza": "RECEPTION", "Lobby": "CLIENT SHOWCASE  ·  LAB", "Mezzanine": "MEETING ROOMS",
            "OpenPlan": "OPEN PLAN", "ServerComms": "TECHNOLOGY", "Executive": "EXECUTIVE",
            "Skydeck": "CANTEEN  ·  SKYDECK", "Summit": "MANAGEMENT SUITE",
            "WinterGarden": "WINTER GARDEN  ·  ROOF TERRACE"}.get(area_name, "")
    _sign(cx, cy, 0.0, GLASS_LIFT_Y[0] - 14.0, z + 282.0, -90.0, area, 11.0, (190, 200, 220),
          prefix + "_Sign_FloorName")
    _sign(cx, cy, 0.0, CORE_HALL_Y[0] - 14.0, z + 300.0, -90.0, "LIFTS  1–8   ·   GLASS LIFT", 13.0, light,
          prefix + "_Sign_Lifts")
    for s_side, s_tag in ((-1.0, "A"), (1.0, "B")):
        door_x = STAIR_DOOR_X * s_side
        _sign(cx, cy, door_x, STAIR_LOBBY_Y[0] - 14.0, z + 245.0, -90.0,
              "STAIRS" if s_side < 0 else "SPIRAL STAIR", 13.0, light, "{0}_Sign_Stair{1}".format(prefix, s_tag))
        _sign(cx, cy, door_x, STAIR_LOBBY_Y[0] - 14.0, z + 222.0, -90.0, "FIRE EXIT", 10.0, (40, 220, 90),
              "{0}_Sign_FireExit{1}".format(prefix, s_tag))


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

        if side == SPIRAL_SIDE:
            if floor_index < C.FLOOR_COUNT:
                build_spiral_stair(cx, cy, z, materials, "{0}_Stair_{1}".format(prefix, tag))
        elif floor_index < C.FLOOR_COUNT:
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

        # Stair lighting: the ceiling grid never lands inside the core, so each
        # stair gets its own bulkheads - one over the lobby, one over the mid
        # landing. Cold, low and shadowed: the stairs are meant to feel like
        # the building's back passages, not like a gap in the lighting.
        for label, ly, lz in (("Lobby", (STAIR_LOBBY_Y[0] + STAIR_FLIGHT_Y[0]) / 2.0, z + C.CEILING_HEIGHT_CM - 40.0),
                              ("Landing", (STAIR_FLIGHT_Y[1] + STAIR_TOP_Y) / 2.0, z + C.FLOOR_HEIGHT_CM / 2.0 + 260.0)):
            bulkhead = _spawn(unreal.PointLight, unreal.Vector(cx + STAIR_DOOR_X * side, cy + ly, lz))
            if bulkhead is None:
                continue
            component = bulkhead.point_light_component
            component.set_editor_property("IntensityUnits", unreal.LightUnits.LUMENS)
            component.set_editor_property("Intensity", 1800.0)
            component.set_editor_property("AttenuationRadius", 650.0)
            component.set_editor_property("SourceRadius", 12.0)
            component.set_editor_property("LightColor", unreal.Color(r=235, g=240, b=255, a=255))
            component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
            component.set_editor_property("MaxDrawDistance", 3000.0)
            _label(bulkhead, "{0}_Stair_{1}_Bulkhead_{2}".format(prefix, tag, label))

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
        _wc_fittings(cx, cy, z, side, tag, materials, prefix)

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
    doors = (300.0, 800.0, 1300.0)
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

    # The stick curtain wall of the real base: black mullions every 150 cm,
    # each with a glass fin standing inside it, double height across the
    # reception void (the floor-1 slab is open behind it) and a black transom
    # where the floor-1 slab meets the glass elsewhere.
    black = _district_material("BlackCladding") or metal
    void_x0, void_x1 = RECEPTION_VOID[0], RECEPTION_VOID[1]
    mullion_x = -half_x
    index = 0
    while mullion_x <= half_x + 1.0:
        index += 1
        if not any(abs(mullion_x - door_x) < width / 2.0 + 10.0 for door_x in doors):
            tall = void_x0 <= mullion_x <= void_x1
            height = 2.0 * C.FLOOR_HEIGHT_CM if tall else h
            _box((cx + mullion_x, cy - half_y - 10.0, z + height / 2.0), (8.0, 14.0, height), black,
                 "{0}_Frontage_Mullion_{1:02d}".format(prefix, index))
            _box((cx + mullion_x, cy - half_y + 28.0, z + height / 2.0), (2.5, 50.0, height), screen,
                 "{0}_Frontage_GlassFin_{1:02d}".format(prefix, index))
        mullion_x += 150.0
    _box((cx, cy - half_y - 10.0, z + h - 10.0), (2.0 * half_x, 16.0, 20.0), black, prefix + "_Frontage_Transom")

    # --- Reception: desk in front of a dark feature wall.
    _box((cx + 900.0, cy - 1250.0, z + 55.0), (900.0, 110.0, 110.0), marble, prefix + "_Reception_Desk")
    # The black-clad feature volume behind the desk, double height like the
    # reception itself (the real one carries the owner's roundel; ours stays
    # plain - the game's tower is OB, not anyone's trademark).
    black = _district_material("BlackCladding") or metal
    _box((cx + 900.0, cy - 978.0, z + 390.0), (1000.0, 45.0, 780.0), black, prefix + "_Reception_FeatureWall")
    # Close the back-of-house behind reception so the gates are the only way in.
    # The screen stops short of the feature stair, which rises over the corner
    # of the back-of-house; a return closes it.
    _wall(cx, cy, z, 1400.0, GATE_LINE_Y, FEATURE_STAIR_X[0] - 20.0, GATE_LINE_Y, screen,
          prefix + "_Reception_Screen_E", thickness=12.0)
    _wall(cx, cy, z, FEATURE_STAIR_X[0] - 20.0, GATE_LINE_Y, FEATURE_STAIR_X[0] - 20.0, -955.0, plaster,
          prefix + "_Reception_Screen_Return", height=200.0)
    build_feature_stair(cx, cy, z, materials, prefix)
    _wall(cx, cy, z, 1400.0, GATE_LINE_Y, 1400.0, -950.0, plaster, prefix + "_Reception_Return_E")
    _wall(cx, cy, z, 400.0, GATE_LINE_Y, 400.0, -950.0, plaster, prefix + "_Reception_Return_W")

    # --- The gate lines. Pedestals every 110 cm, lanes between, each with its
    # own glass wings held open (folded flat against its sides, inside its
    # length, standing above it - never in the lane). The main line runs from
    # reception's corner to the lift hall; then a short wall; then the
    # escalators' own line, right in front of them.
    def gate_line(x_from, x_to, label):
        index = 0
        step = 110.0 if x_to > x_from else -110.0
        x = x_from
        while (x <= x_to + 1.0) if step > 0 else (x >= x_to - 1.0):
            index += 1
            _box((cx + x, cy + GATE_LINE_Y, z + 50.0), (20.0, 120.0, 100.0), metal,
                 "{0}_{1}_Pedestal_{2:02d}".format(prefix, label, index))
            for face in (-1.0, 1.0):
                _box((cx + x + face * 11.5, cy + GATE_LINE_Y, z + 85.0), (3.0, 100.0, 130.0), screen,
                     "{0}_{1}_Pedestal_{2:02d}_Glass_{3}".format(prefix, label, index, "W" if face < 0 else "E"))
            x += step

    gate_line(400.0, MAIN_GATES_END_X, "Gate")
    _wall(cx, cy, z, MAIN_GATES_END_X - 20.0, GATE_LINE_Y, ESCALATOR_X[1] + 20.0, GATE_LINE_Y, plaster,
          prefix + "_Gate_Wall")
    gate_line(ESCALATOR_X[1], COFFEE_WALL_X + 20.0, "EscalatorGate")

    # --- Four escalators up to floor 1: step flights between sloped glass
    # balustrades with black handrails and a stainless deck, the east side
    # closed by the enclosure that carries the escalator's OLED wall.
    lanes = ESCALATOR_LANES
    divider = 20.0
    lane_w = (ESCALATOR_X[1] - ESCALATOR_X[0] - divider * (lanes - 1)) / lanes
    run = ESCALATOR_Y[1] - ESCALATOR_Y[0]
    rise = C.FLOOR_HEIGHT_CM
    slope = math.degrees(math.atan2(rise, run))
    length = math.hypot(run, rise)
    glass_rail = materials.get("Glass") or screen
    rubber = _district_material("BlackCladding") or metal
    steel = _district_material("Aluminium") or metal
    for lane in range(lanes):
        x0 = ESCALATOR_X[0] + lane * (lane_w + divider)
        _flight(cx, cy, z, x0, x0 + lane_w, ESCALATOR_Y[0], ESCALATOR_Y[1], 0.0, rise, 22, metal,
                "{0}_Escalator_{1}".format(prefix, lane + 1))
    for edge in range(lanes + 1):
        ex = ESCALATOR_X[0] + edge * (lane_w + divider) - divider / 2.0
        ex = max(ESCALATOR_X[0] - divider / 2.0, min(ESCALATOR_X[1] + divider / 2.0, ex))
        mid_y = (ESCALATOR_Y[0] + ESCALATOR_Y[1]) / 2.0
        for part, (w, h_, off, material) in (("Deck", (divider, 40.0, 20.0, steel)),
                                             ("Glass", (2.0, 90.0, 85.0, glass_rail)),
                                             ("Handrail", (10.0, 8.0, 134.0, rubber))):
            piece = _box((cx + ex, cy + mid_y, z + rise / 2.0 + off), (w, length + 40.0, h_), material,
                         "{0}_Escalator_{1}_{2}".format(prefix, part, edge + 1))
            if piece is not None:
                piece.set_actor_rotation(unreal.Rotator(roll=-slope, pitch=0.0, yaw=0.0), False)
    # The enclosure faces the lift hall and carries the escalator's OLED wall.
    _wall(cx, cy, z, ESCALATOR_X[1] + 20.0 + divider, ESCALATOR_Y[0] - 40.0, ESCALATOR_X[1] + 20.0 + divider,
          ESCALATOR_Y[1], metal, prefix + "_Escalator_Enclosure_E", height=h)

    # --- Coffee shop: walled off from the escalators, open to the public side.
    _wall_with_door(cx, cy, z, COFFEE_WALL_X, -half_y, COFFEE_WALL_X, -200.0, -1450.0, 180.0, plaster,
                    prefix + "_Coffee_Wall_E")
    _wall(cx, cy, z, -half_x, -200.0, COFFEE_WALL_X, -200.0, plaster, prefix + "_Coffee_Wall_N")
    _box((cx - 2000.0, cy - 700.0, z + 52.0), (600.0, 90.0, 105.0), wood, prefix + "_Coffee_Counter")
    for row, ty in enumerate((-1550.0, -1250.0)):
        for col, tx in enumerate((-1650.0, -1900.0, -2100.0)):
            _box((cx + tx, cy + ty, z + 37.0), (80.0, 80.0, 74.0), wood,
                 "{0}_Coffee_Table_{1}_{2}".format(prefix, row + 1, col + 1))

    build_reception_lounge(cx, cy, z, materials, prefix)
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


def _fitout(name, fallback=None):
    return _district_material(name) or fallback


def build_lift_lobby(cx, cy, z, materials, prefix):
    """The lift hall as the photographs show it: pale large-format porcelain
    underfoot, cream panelled walls with brushed-steel frames at every landing
    door, a dropped ceiling with warm coves and downlights, and a plant in a
    tall black pot by the glass lift."""
    cream = _fitout("PanelCream", materials.get("Plasterboard"))
    porcelain = _fitout("Porcelain", materials.get("Concrete"))
    steel = materials.get("Metal")
    white = _fitout("LightRibbon")
    hx = CORE_HALL_X
    y0, y1 = CORE_HALL_Y
    _box((cx, cy + (y0 + y1) / 2.0, z + 0.4), (2.0 * hx - 4.0, y1 - y0, 0.8), porcelain, prefix + "_LiftHall_Porcelain")
    for side, tag in ((-1.0, "W"), (1.0, "E")):
        face = (hx - 6.0) * side
        for index, y in enumerate(CORE_SHAFT_YS):
            _wall_with_door(cx, cy, z, face, y - 150.0, face, y + 150.0, y, C.DOOR_WIDTH_CM + 10.0, cream,
                            "{0}_LiftHall_Panel{1}_{2}".format(prefix, tag, index + 1), height=300.0)
            for jamb in (-1.0, 1.0):
                _box((cx + face - side * 2.0, cy + y + jamb * (C.DOOR_WIDTH_CM / 2.0 + 6.0), z + 115.0),
                     (8.0, 8.0, 230.0), steel, "{0}_LiftHall_Jamb{1}_{2}_{3}".format(prefix, tag, index + 1, int(jamb)))
        # Warm cove along the top of each wall.
        if white is not None:
            _box((cx + (hx - 14.0) * side, cy + (y0 + y1) / 2.0, z + 298.0), (3.0, y1 - y0 - 20.0, 2.0), white,
                 "{0}_LiftHall_Cove{1}".format(prefix, tag))
    _box((cx, cy + (y0 + y1) / 2.0, z + 305.0), (2.0 * hx - 30.0, y1 - y0, 6.0),
         _fitout("CeilingWhite", cream), prefix + "_LiftHall_Ceiling")
    for i in range(4):
        y = y0 + (i + 0.5) * (y1 - y0) / 4.0
        lamp = _spawn(unreal.PointLight, unreal.Vector(cx, cy + y, z + 290.0))
        if lamp is not None:
            component = lamp.point_light_component
            component.set_editor_property("IntensityUnits", unreal.LightUnits.LUMENS)
            component.set_editor_property("Intensity", 1600.0)
            component.set_editor_property("AttenuationRadius", 520.0)
            component.set_editor_property("LightColor", unreal.Color(r=255, g=236, b=210, a=255))
            component.set_editor_property("CastShadows", i % 2 == 0)
            component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
            _label(lamp, "{0}_LiftHall_Downlight_{1}".format(prefix, i + 1))
    _fig_tree(cx, cy, z, 140.0, y1 - 40.0, prefix + "_LiftHall_Plant", height=170.0)
    # Soft fill from the pale floor up to the ceiling and walls, as the coves do.
    for i in range(2):
        fill = _spawn(unreal.PointLight, unreal.Vector(cx, cy + y0 + (i + 0.5) * (y1 - y0) / 2.0, z + 40.0))
        if fill is not None:
            component = fill.point_light_component
            component.set_editor_property("IntensityUnits", unreal.LightUnits.LUMENS)
            component.set_editor_property("Intensity", 2200.0)
            component.set_editor_property("AttenuationRadius", 700.0)
            component.set_editor_property("SourceRadius", 150.0)
            component.set_editor_property("CastShadows", False)
            component.set_editor_property("SpecularScale", 0.0)
            component.set_editor_property("LightColor", unreal.Color(r=255, g=240, b=222, a=255))
            component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
            _label(fill, "{0}_LiftHall_Fill_{1}".format(prefix, i + 1))


def build_linear_lights(area_name, cx, cy, z, size_x, size_y, prefix):
    """The offices' suspended linear LEDs: long thin lines under the exposed
    services, in runs across the floor plate, kept off the core."""
    white = _fitout("LightRibbon")
    if white is None:
        return
    half_x, half_y = size_x / 2.0, size_y / 2.0
    drop = z + C.CEILING_HEIGHT_CM - 62.0
    x = -half_x + 250.0
    col = 0
    while x < half_x - 200.0:
        col += 1
        y = -half_y + 200.0
        seg = 0
        while y + 450.0 < half_y - 150.0:
            mid = y + 225.0
            if not (abs(x) < 1200.0 and -1000.0 < mid < 1300.0):
                if area_name != "WinterGarden" and not (area_name == "Skydeck" and mid > SKYDECK_SCREEN_Y) \
                        and not (area_name == "Summit" and mid > SKYDECK_SCREEN_Y):
                    seg += 1
                    _box((cx + x, cy + mid, drop), (4.0, 440.0, 3.0), white,
                         "{0}_LinearLED_{1}_{2}".format(prefix, col, seg))
            y += 520.0
        x += 330.0


def _felt_discs(cx, cy, z, x0, x1, y0, y1, prefix, label):
    felt = _fitout("FeltWhite")
    for i, (fx, fy, d) in enumerate(((0.25, 0.3, 120.0), (0.55, 0.5, 150.0), (0.8, 0.25, 110.0), (0.4, 0.75, 130.0))):
        _box((cx + x0 + (x1 - x0) * fx, cy + y0 + (y1 - y0) * fy, z + C.CEILING_HEIGHT_CM - 75.0 - i * 6.0),
             (d, d, 5.0), felt, "{0}_{1}_FeltDisc_{2}".format(prefix, label, i + 1),
             mesh_path="/Engine/BasicShapes/Cylinder.Cylinder")


def _booth(cx, cy, z, x, y, facing, prefix, label):
    """A high-back two-seat booth: the purple acoustic kind, back to the room."""
    purple = _fitout("BoothPurple")
    black = _fitout("BlackCladding")
    _box((cx + x, cy + y, z + 22.0), (150.0, 60.0, 44.0), purple, label + "_Seat")
    _box((cx + x, cy + y - facing * 28.0, z + 70.0), (150.0, 12.0, 140.0), purple, label + "_Back")
    for e in (-1.0, 1.0):
        _box((cx + x + e * 75.0, cy + y, z + 55.0), (8.0, 60.0, 110.0), purple, "{0}_Arm{1}".format(label, int(e)))
    for e in (-1.0, 1.0):
        _box((cx + x + e * 65.0, cy + y - facing * 25.0, z + 4.0), (3.0, 3.0, 8.0), black, "{0}_Foot{1}".format(label, int(e)))


def build_breakout(cx, cy, z, x0, x1, y0, y1, materials, prefix, label, kitchen=False):
    """Breakout space: purple high-back booths face to face round timber
    tables, loose armchairs, felt discs overhead. A breakout kitchen is laid
    out by build_breakout_kitchen instead: the run, the island and one booth
    pair do not fit the generic arrangement."""
    if kitchen:
        build_breakout_kitchen(cx, cy, z, x0, x1, y0, y1, materials, prefix, label)
        return
    timber = _fitout("TimberLight", materials.get("Wood"))
    black = _fitout("BlackCladding")
    w, d = x1 - x0, y1 - y0
    long_x = w >= d
    pairs = max(1, int((w if long_x else d) // 330.0))
    for p_ in range(pairs):
        t = (p_ + 0.5) / pairs
        if long_x:
            bx, by = x0 + w * t, y0 + d * 0.3
            _booth(cx, cy, z, bx, by - 70.0, 1.0, prefix, "{0}_{1}_Booth{2}A".format(prefix, label, p_ + 1))
            _booth(cx, cy, z, bx, by + 70.0, -1.0, prefix, "{0}_{1}_Booth{2}B".format(prefix, label, p_ + 1))
            _box((cx + bx, cy + by, z + 37.0), (120.0, 60.0, 4.0), timber, "{0}_{1}_BoothTable{2}".format(prefix, label, p_ + 1))
            _box((cx + bx, cy + by, z + 18.0), (6.0, 6.0, 36.0), black, "{0}_{1}_BoothTableLeg{2}".format(prefix, label, p_ + 1))
        else:
            bx, by = x0 + w * 0.5, y0 + d * t
            for e, tag in ((-1.0, "A"), (1.0, "B")):
                seat = _box((cx + bx + e * 70.0, cy + by, z + 22.0), (60.0, 150.0, 44.0), _fitout("BoothPurple"),
                            "{0}_{1}_Booth{2}{3}_Seat".format(prefix, label, p_ + 1, tag))
                _box((cx + bx + e * 98.0, cy + by, z + 70.0), (12.0, 150.0, 140.0), _fitout("BoothPurple"),
                     "{0}_{1}_Booth{2}{3}_Back".format(prefix, label, p_ + 1, tag))
            _box((cx + bx, cy + by, z + 37.0), (60.0, 120.0, 4.0), timber, "{0}_{1}_BoothTable{2}".format(prefix, label, p_ + 1))
    _felt_discs(cx, cy, z, x0, x1, y0, y1, prefix, label)


def _block(cx, cy, z, x0, x1, y0, y1, z0, z1, material, label, mesh_path=None):
    """A box given by its extents rather than its centre and size: joinery is
    drawn to its faces, and this keeps the shadow gaps and the reveals exact."""
    return _box((cx + (x0 + x1) / 2.0, cy + (y0 + y1) / 2.0, z + (z0 + z1) / 2.0),
                (abs(x1 - x0), abs(y1 - y0), abs(z1 - z0)), material, label, mesh_path=mesh_path)


def _soft_light(actor_class, location, rotation, lumens, radius, colour, label, width=None, height=None):
    """A small fill light that casts no shadows: under-cabinet strips, pendants
    and canopy lights. A floor's shadows come from its daylight and its ceiling
    lines; a hundred little shadow casters would cost more than they show."""
    lamp = _spawn(actor_class, location, rotation)
    if lamp is None:
        return None
    component = lamp.rect_light_component if actor_class is unreal.RectLight else lamp.point_light_component
    component.set_editor_property("IntensityUnits", unreal.LightUnits.LUMENS)
    component.set_editor_property("Intensity", lumens)
    component.set_editor_property("AttenuationRadius", radius)
    # unreal.Color's positional order is its BGRA memory layout: callers
    # always pass r=, g=, b=, a= by keyword.
    component.set_editor_property("LightColor", colour)
    component.set_editor_property("CastShadows", False)
    component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
    if width is not None:
        # A rect light's width is along its local Y: face it down with
        # pitch -90 and yaw it 90 for a strip that runs along X.
        component.set_editor_property("SourceWidth", width)
        component.set_editor_property("SourceHeight", height)
    _label(lamp, label)
    return lamp


# A UK kitchen run, in cm: 150 mm plinth, 720 mm carcass, 40 mm worktop (top
# at 910), 100 mm upstand, wall units 720 tall hung 540 above the worktop,
# 330 deep, 600 module throughout.
KITCHEN_PLINTH = 15.0
KITCHEN_CARCASS_TOP = 87.0
KITCHEN_WORKTOP_TOP = 91.0
KITCHEN_WALL_UNIT = (145.0, 217.0)


def build_breakout_kitchen(cx, cy, z, x0, x1, y0, y1, materials, prefix, label):
    """The office floors' breakout kitchen, in the corner by the west glazing.

    A 4.3 m run against a laminate back panel stood off the glass: a tall
    integrated fridge-freezer at the corner end, then a pull-out recycling bin,
    the sink with a mixer tap and a boiling-water tap beside it, an integrated
    dishwasher, drawers, and a bean-to-cup coffee machine on the worktop. Wall
    units above with an LED strip under them. Facing it across a 120 cm aisle,
    the long timber-topped island the real floors have, with bar stools and
    pendants over it; a purple booth pair and a three-stream bin station by
    the south glass. Everything leaves the room's open north and east edges
    clear, and the floor's spawn anchor in the corner stands in the open.
    """
    front_mat = _fitout("PanelCream", materials.get("Plasterboard"))
    timber = _fitout("TimberLight", materials.get("Wood"))
    black = _fitout("BlackCladding", materials.get("Metal"))
    steel = _fitout("Aluminium", materials.get("Metal"))
    ribbon = _fitout("LightRibbon")
    tag = "{0}_{1}".format(prefix, label)

    def block(xa, xb, ya, yb, za, zb, material, name, mesh_path=None):
        return _block(cx, cy, z, xa, xb, ya, yb, za, zb, material, "{0}_{1}".format(tag, name), mesh_path)

    # --- The run, against the west side ----------------------------------
    # 200 cm in from the south glass to keep clear of the rounded corner.
    back = x0                       # the back panel's face to the glass
    carcass_front = back + 60.0
    run_y0, run_y1 = y0 + 200.0, y1 - 20.0
    units = (("TallFridge", 60.0), ("Recycling", 60.0), ("Sink", 60.0), ("Dishwasher", 60.0),
             ("Drawers", 60.0), ("Coffee", 60.0), ("Cupboard", 70.0))
    top_y0 = run_y0 + units[0][1]   # the worktop starts past the tall unit

    def front(ya, yb, za, zb, name, handle):
        """One door or drawer front, 2 cm proud of the carcass, with a 3 mm
        shadow gap all round and a brushed bar handle."""
        block(carcass_front, carcass_front + 2.0, ya + 0.2, yb - 0.2, za + 0.2, zb - 0.2, front_mat, name)
        hx0, hx1 = carcass_front + 2.0, carcass_front + 4.0
        if handle == "across":
            half = min(20.0, (yb - ya) / 2.0 - 8.0)
            mid = (ya + yb) / 2.0
            block(hx0, hx1, mid - half, mid + half, zb - 7.0, zb - 5.5, steel, name + "_Handle")
        elif handle == "up":
            block(hx0, hx1, yb - 5.0, yb - 3.5, zb - 25.0, zb - 5.0, steel, name + "_Handle")
        elif handle == "down":
            block(hx0, hx1, yb - 5.0, yb - 3.5, za + 5.0, za + 25.0, steel, name + "_Handle")

    block(back + 2.0, carcass_front - 5.0, run_y0, run_y1, 0.0, KITCHEN_PLINTH, black, "Plinth")
    y = run_y0
    for name, width in units:
        ya, yb = y, y + width
        if name == "TallFridge":
            # Full height, its top line with the wall units' top.
            block(back + 2.0, carcass_front, ya, yb, KITCHEN_PLINTH, KITCHEN_WALL_UNIT[1], front_mat, "TallFridge_Carcass")
            front(ya, yb, KITCHEN_PLINTH, 95.0, "TallFridge_FreezerDoor", "up")
            front(ya, yb, 95.0, KITCHEN_WALL_UNIT[1], "TallFridge_FridgeDoor", "down")
        else:
            block(back + 2.0, carcass_front, ya, yb, KITCHEN_PLINTH, KITCHEN_CARCASS_TOP, front_mat, name + "_Carcass")
            if name == "Drawers":
                for i, (za, zb) in enumerate(((KITCHEN_PLINTH, 44.0), (44.0, 66.0), (66.0, KITCHEN_CARCASS_TOP))):
                    front(ya, yb, za, zb, "Drawer{0}".format(i + 1), "across")
            elif name in ("Recycling", "Dishwasher"):
                # One full-height front each: the bin pulls out, the dishwasher
                # door drops; both have the handle across the top.
                front(ya, yb, KITCHEN_PLINTH, KITCHEN_CARCASS_TOP, name + "_Front", "across")
            else:
                front(ya, yb, KITCHEN_PLINTH, KITCHEN_CARCASS_TOP, name + "_Door", "up")
        y = yb

    # Oak worktop with a 2 cm overhang, its upstand, and the back panel to the
    # underside of the wall units.
    block(back + 2.0, carcass_front + 4.0, top_y0, run_y1, KITCHEN_CARCASS_TOP, KITCHEN_WORKTOP_TOP, timber, "Worktop")
    block(back + 2.0, back + 4.0, top_y0, run_y1, KITCHEN_WORKTOP_TOP, KITCHEN_WORKTOP_TOP + 10.0, timber, "Upstand")
    block(back, back + 2.0, top_y0, run_y1, KITCHEN_WORKTOP_TOP, KITCHEN_WALL_UNIT[1], front_mat, "BackPanel")

    # The sink: an inset steel bowl with a drainer, the mixer tap behind it and
    # the boiling-water tap beside it, over the dishwasher.
    sink_y = run_y0 + 150.0
    block(back + 9.0, back + 54.0, sink_y - 25.0, sink_y + 25.0, KITCHEN_WORKTOP_TOP, KITCHEN_WORKTOP_TOP + 0.6, steel, "Sink_Rim")
    block(back + 13.0, back + 50.0, sink_y - 21.0, sink_y + 5.0, KITCHEN_WORKTOP_TOP + 0.6, KITCHEN_WORKTOP_TOP + 0.7, black, "Sink_Bowl")
    block(back + 8.0, back + 13.0, sink_y - 2.5, sink_y + 2.5, KITCHEN_WORKTOP_TOP, KITCHEN_WORKTOP_TOP + 32.0, steel,
          "Sink_Tap", mesh_path=CYLINDER_MESH)
    block(back + 8.0, back + 30.0, sink_y - 1.5, sink_y + 1.5, KITCHEN_WORKTOP_TOP + 29.0, KITCHEN_WORKTOP_TOP + 32.0, steel, "Sink_Spout")
    boil_y = sink_y + 38.0
    block(back + 8.0, back + 12.0, boil_y - 2.0, boil_y + 2.0, KITCHEN_WORKTOP_TOP, KITCHEN_WORKTOP_TOP + 28.0, black,
          "BoilingTap", mesh_path=CYLINDER_MESH)
    block(back + 8.0, back + 24.0, boil_y - 1.2, boil_y + 1.2, KITCHEN_WORKTOP_TOP + 25.5, KITCHEN_WORKTOP_TOP + 28.0, black, "BoilingTap_Spout")
    block(back + 5.0, back + 15.0, boil_y + 6.0, boil_y + 16.0, KITCHEN_WORKTOP_TOP, KITCHEN_WORKTOP_TOP + 1.0, steel, "BoilingTap_DripTray")

    # Bean-to-cup coffee machine on the coffee unit: black body, steel drip tray.
    coffee_y = run_y0 + 330.0
    block(back + 8.0, back + 52.0, coffee_y - 16.0, coffee_y + 16.0, KITCHEN_WORKTOP_TOP, KITCHEN_WORKTOP_TOP + 50.0, black, "CoffeeMachine")
    block(back + 52.0, back + 58.0, coffee_y - 14.0, coffee_y + 14.0, KITCHEN_WORKTOP_TOP, KITCHEN_WORKTOP_TOP + 2.5, steel, "CoffeeMachine_DripTray")
    block(back + 52.0, back + 53.0, coffee_y - 10.0, coffee_y + 10.0, KITCHEN_WORKTOP_TOP + 36.0, KITCHEN_WORKTOP_TOP + 46.0, steel, "CoffeeMachine_Panel")

    # Wall units over the worktop, a door per module, handles on the bottom
    # rail; the LED strip on their underside, with its one soft light.
    wall_front = back + 33.0
    block(back + 2.0, wall_front, top_y0, run_y1, KITCHEN_WALL_UNIT[0], KITCHEN_WALL_UNIT[1], front_mat, "WallUnits")
    y = top_y0
    for door, (_, width) in enumerate(units[1:]):
        yb = y + width
        block(wall_front, wall_front + 2.0, y + 0.2, yb - 0.2, KITCHEN_WALL_UNIT[0] + 0.2, KITCHEN_WALL_UNIT[1] - 0.2, front_mat,
              "WallUnit_Door{0}".format(door + 1))
        block(wall_front + 2.0, wall_front + 4.0, yb - 5.0, yb - 3.5, KITCHEN_WALL_UNIT[0] + 4.0, KITCHEN_WALL_UNIT[0] + 22.0, steel,
              "WallUnit_Door{0}_Handle".format(door + 1))
        y = yb
    if ribbon is not None:
        block(wall_front - 6.0, wall_front - 3.0, top_y0 + 5.0, run_y1 - 5.0, KITCHEN_WALL_UNIT[0] - 1.0, KITCHEN_WALL_UNIT[0],
              ribbon, "UnderCabinet_LED")
    _soft_light(unreal.RectLight,
                unreal.Vector(cx + wall_front - 5.0, cy + (top_y0 + run_y1) / 2.0, z + KITCHEN_WALL_UNIT[0] - 2.0),
                unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0), 900.0, 180.0, unreal.Color(r=255, g=238, b=214, a=255),
                tag + "_UnderCabinet_Light", width=run_y1 - top_y0 - 10.0, height=3.0)

    # --- The island: grey body, long oak top, bar stools, three pendants ----
    # A 120 cm aisle from the handles and the worktop nosing (carcass + 4) to
    # the island top's edge (5 cm proud of its body).
    ix0 = carcass_front + 4.0 + 120.0 + 5.0
    ix1 = ix0 + 90.0
    iy0, iy1 = y1 - 240.0, y1 - 20.0
    block(ix0, ix1, iy0, iy1, 0.0, KITCHEN_PLINTH, black, "Island_Plinth")
    block(ix0, ix1, iy0, iy1, KITCHEN_PLINTH, KITCHEN_CARCASS_TOP, front_mat, "Island")
    # The top oversails the seating side by 30 cm for knees.
    block(ix0 - 5.0, ix1 + 30.0, iy0 - 5.0, iy1 + 5.0, KITCHEN_CARCASS_TOP, KITCHEN_WORKTOP_TOP, timber, "IslandTop")
    stool_x = ix1 + 48.0
    for i in range(3):
        sy = iy0 + 40.0 + i * 70.0
        name = "Stool{0}".format(i + 1)
        block(stool_x - 18.0, stool_x + 18.0, sy - 18.0, sy + 18.0, 62.0, 65.0, black, name + "_Seat", mesh_path=CYLINDER_MESH)
        block(stool_x - 2.5, stool_x + 2.5, sy - 2.5, sy + 2.5, 2.0, 62.0, black, name + "_Stem")
        block(stool_x - 20.0, stool_x + 20.0, sy - 20.0, sy + 20.0, 0.0, 2.0, black, name + "_Base", mesh_path=CYLINDER_MESH)
        block(stool_x - 16.0, stool_x + 16.0, sy - 16.0, sy + 16.0, 24.0, 25.5, steel, name + "_FootRing", mesh_path=CYLINDER_MESH)
    pend_x = (ix0 + ix1) / 2.0
    ceiling = C.CEILING_HEIGHT_CM - 40.0          # the soffit's underside
    for i in range(3):
        py = iy0 + 40.0 + i * 70.0
        name = "Pendant{0}".format(i + 1)
        block(pend_x - 0.5, pend_x + 0.5, py - 0.5, py + 0.5, 225.0, ceiling, black, name + "_Drop")
        block(pend_x - 15.0, pend_x + 15.0, py - 15.0, py + 15.0, 200.0, 225.0, black, name + "_Shade", mesh_path=CYLINDER_MESH)
        if ribbon is not None:
            block(pend_x - 12.0, pend_x + 12.0, py - 12.0, py + 12.0, 199.5, 200.0, ribbon, name + "_Diffuser",
                  mesh_path=CYLINDER_MESH)
    _soft_light(unreal.PointLight, unreal.Vector(cx + pend_x, cy + (iy0 + iy1) / 2.0, z + 190.0), None,
                1200.0, 320.0, unreal.Color(r=255, g=226, b=190, a=255), tag + "_Pendant_Light")

    # --- A booth pair by the south glass, end-on to the run ----------------
    purple = _fitout("BoothPurple")
    bx, by = x1 - 130.0, y0 + 140.0
    for e, side in ((-1.0, "A"), (1.0, "B")):
        block(bx + e * 70.0 - 30.0, bx + e * 70.0 + 30.0, by - 75.0, by + 75.0, 0.0, 44.0, purple, "Booth" + side + "_Seat")
        block(bx + e * 98.0 - 6.0, bx + e * 98.0 + 6.0, by - 75.0, by + 75.0, 0.0, 140.0, purple, "Booth" + side + "_Back")
    block(bx - 30.0, bx + 30.0, by - 60.0, by + 60.0, 72.0, 76.0, timber, "BoothTable")
    block(bx - 3.0, bx + 3.0, by - 3.0, by + 3.0, 0.0, 72.0, black, "BoothTable_Leg")

    # --- Three-stream recycling against the south glass --------------------
    for i, stream in enumerate(("General", "Mixed", "Food")):
        bin_x = bx - 175.0 - i * 37.0
        name = "Bin_" + stream
        block(bin_x - 17.0, bin_x + 17.0, y0 + 30.0, y0 + 70.0, 0.0, 78.0, black, name)
        block(bin_x - 17.5, bin_x + 17.5, y0 + 29.5, y0 + 70.5, 78.0, 82.0, steel, name + "_Lid")
        block(bin_x - 9.0, bin_x + 9.0, y0 + 45.0, y0 + 55.0, 82.0, 82.3, black, name + "_Aperture")

    _felt_discs(cx, cy, z, x0, x1, y0, y1, prefix, label)
    _note_surface("Wood", tag + "_Worktop")
    _note_surface("Metal", tag + "_Sink")


def _cubby(cx, cy, z, x, y, prefix, label, along_x=True):
    """Light-timber cubby shelving: the room dividers of the real floors."""
    timber = _fitout("TimberLight")
    w, h, d = 160.0, 180.0, 36.0
    for i in range(4):
        zz = z + i * h / 3.0
        size = (w, d, 3.0) if along_x else (d, w, 3.0)
        _box((cx + x, cy + y, zz + 1.5), size, timber, "{0}_Shelf{1}".format(label, i))
    for j in range(4):
        off = -w / 2.0 + j * w / 3.0
        pos = (cx + x + off, cy + y, z + h / 2.0) if along_x else (cx + x, cy + y + off, z + h / 2.0)
        size = (3.0, d, h) if along_x else (d, 3.0, h)
        _box(pos, size, timber, "{0}_Upright{1}".format(label, j))
    green = _fitout("Leaves")
    for k in range(2):
        off = -40.0 + k * 80.0
        pos = (cx + x + off, cy + y, z + h + 18.0) if along_x else (cx + x, cy + y + off, z + h + 18.0)
        _box(pos, (36.0, 30.0, 36.0), green, "{0}_Plant{1}".format(label, k), mesh_path=SPHERE_MESH)


GLAZED_ROOMS = ("Meeting_Room", "Meeting_Pod_1", "Meeting_Pod_2", "Boardroom", "Corner_Office_West",
                "Corner_Office_East", "Exec_Suite", "Management_Suite")


def _glazed_frame(cx, cy, z, x0, x1, y0, y1, height, label):
    """Black head rail, base rail and mullions every 120 cm round a glass room."""
    black = _fitout("BlackCladding")
    for side, (ax, ay, bx, by) in enumerate(((x0, y0, x1, y0), (x0, y1, x1, y1), (x0, y0, x0, y1), (x1, y0, x1, y1))):
        length = math.hypot(bx - ax, by - ay)
        along_x = abs(bx - ax) > abs(by - ay)
        for rail_z, rail_h in ((height - 3.0, 6.0), (3.0, 6.0)):
            size = (length + 6.0, 6.0, rail_h) if along_x else (6.0, length + 6.0, rail_h)
            _box((cx + (ax + bx) / 2.0, cy + (ay + by) / 2.0, z + rail_z), size, black,
                 "{0}_Frame{1}_{2}".format(label, side, int(rail_z)))
        count = max(1, int(length // 120.0))
        for i in range(1, count):
            t = i / float(count)
            px, py = ax + (bx - ax) * t, ay + (by - ay) * t
            _box((cx + px, cy + py, z + height / 2.0), (5.0, 5.0, height), black,
                 "{0}_Mullion{1}_{2}".format(label, side, i))


def _meeting_table(cx, cy, z, rx, ry, rsx, rsy, materials, label):
    """A long pale-oak table on black legs, chairs down both sides."""
    timber = _fitout("TimberLight", materials.get("Wood"))
    black = _fitout("BlackCladding")
    along_x = rsx >= rsy
    length = (rsx if along_x else rsy) * 0.6
    width = min(110.0, (rsy if along_x else rsx) * 0.4)
    size = (length, width, 4.0) if along_x else (width, length, 4.0)
    _box((cx + rx, cy + ry, z + 73.0), size, timber, label + "_Table")
    for e in (-1.0, 1.0):
        leg = (cx + rx + (e * length * 0.4 if along_x else 0.0), cy + ry + (0.0 if along_x else e * length * 0.4), z + 36.0)
        _box(leg, (6.0, width * 0.8, 70.0) if along_x else (width * 0.8, 6.0, 70.0), black, "{0}_TableLeg{1}".format(label, int(e)))
    seats = max(2, int(length // 90.0))
    for i in range(seats):
        t = -length / 2.0 + (i + 0.5) * length / seats
        for side in (-1.0, 1.0):
            off = side * (width / 2.0 + 35.0)
            x, y = (rx + t, ry + off) if along_x else (rx + off, ry + t)
            _task_chair(cx, cy, z, x, y, side if along_x else 0.0, materials, "{0}_Chair{1}{2}".format(label, i, int(side)))


def _fig_tree(cx, cy, z, x, y, label, height=300.0):
    """A fiddle-leaf fig in a tall black pot: a stem and a loose, leafy crown."""
    pot = _fitout("BlackCladding")
    bark = _fitout("Bark")
    leaves = _fitout("Leaves")
    _box((cx + x, cy + y, z + 32.0), (55.0, 55.0, 64.0), pot, label + "_Pot", mesh_path="/Engine/BasicShapes/Cylinder.Cylinder")
    _box((cx + x, cy + y, z + 64.0 + height * 0.3), (5.0, 5.0, height * 0.6), bark, label + "_Stem")
    for i, (ox, oy, oz, sz) in enumerate(((0, 0, 0.75, 120.0), (35, -20, 0.6, 90.0), (-30, 25, 0.88, 85.0),
                                          (20, 30, 0.95, 70.0))):
        _box((cx + x + ox, cy + y + oy, z + 64.0 + height * oz), (sz, sz * 0.9, sz * 0.8), leaves,
             "{0}_Leaves{1}".format(label, i + 1), mesh_path=SPHERE_MESH)


def build_reception_lounge(cx, cy, z, materials, prefix):
    """The lounge by the glazing, as photographed: an oak herringbone floor set
    into the terrazzo, a dark curved sofa, two timber-framed armchairs, a round
    glass table, and fiddle-leaf figs in tall black pots against the glass."""
    x0, x1, y0, y1 = -1350.0, -250.0, -1760.0, -1290.0
    _box((cx + (x0 + x1) / 2.0, cy + (y0 + y1) / 2.0, z + 0.5), (x1 - x0, y1 - y0, 1.0),
         _fitout("Herringbone", materials.get("Wood")), prefix + "_Lounge_Herringbone")
    sofa = _fitout("SofaDark")
    # The curved sofa: an arc of segments round the table.
    ccx, ccy, radius = -800.0, -1540.0, 190.0
    for i in range(7):
        a = math.radians(200.0 + i * 20.0)
        seg = _box((cx + ccx + radius * math.cos(a), cy + ccy + radius * math.sin(a), z + 22.0), (62.0, 80.0, 44.0), sofa,
                   "{0}_Lounge_Sofa_{1}".format(prefix, i + 1))
        back = _box((cx + ccx + (radius + 38.0) * math.cos(a), cy + ccy + (radius + 38.0) * math.sin(a), z + 52.0),
                    (64.0, 14.0, 60.0), sofa, "{0}_Lounge_SofaBack_{1}".format(prefix, i + 1))
        for piece in (seg, back):
            if piece is not None:
                piece.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=math.degrees(a) + 90.0), False)
    _box((cx + ccx, cy + ccy, z + 40.0), (90.0, 90.0, 3.0), materials.get("Glass"), prefix + "_Lounge_Table",
         mesh_path="/Engine/BasicShapes/Cylinder.Cylinder")
    _box((cx + ccx, cy + ccy, z + 19.0), (8.0, 8.0, 38.0), _fitout("BlackCladding"), prefix + "_Lounge_TableLeg")
    timber = _fitout("Timber", materials.get("Wood"))
    for i, (ax, ay) in enumerate(((-1150.0, -1450.0), (-450.0, -1450.0))):
        _box((cx + ax, cy + ay, z + 38.0), (60.0, 60.0, 8.0), _fitout("PanelCream"), "{0}_Lounge_Armchair{1}_Seat".format(prefix, i + 1))
        _box((cx + ax, cy + ay + 28.0, z + 70.0), (60.0, 8.0, 70.0), timber, "{0}_Lounge_Armchair{1}_Back".format(prefix, i + 1))
        for lx in (-26.0, 26.0):
            _box((cx + ax + lx, cy + ay, z + 18.0), (4.0, 50.0, 36.0), timber, "{0}_Lounge_Armchair{1}_Leg{2}".format(prefix, i + 1, int(lx)))
    for i, fx in enumerate((-1290.0, -980.0, -560.0, -320.0)):
        _fig_tree(cx, cy, z, fx, -1730.0, "{0}_Lounge_Fig{1}".format(prefix, i + 1), height=260.0 + (i % 2) * 60.0)


def _picnic_bench(cx, cy, z, x, y, label):
    black = _fitout("BlackCladding")
    _box((cx + x, cy + y, z + 74.0), (220.0, 75.0, 5.0), black, label + "_Top")
    for e in (-1.0, 1.0):
        _box((cx + x, cy + y + e * 65.0, z + 45.0), (220.0, 28.0, 4.0), black, "{0}_Seat{1}".format(label, int(e)))
        _box((cx + x + e * 95.0, cy + y, z + 36.0), (6.0, 150.0, 72.0), black, "{0}_Frame{1}".format(label, int(e)))


def _task_chair(cx, cy, z, x, y, facing, materials, label):
    """A task chair: star base, gas column, seat, backrest - pushed in, a
    little askew, the way a real office leaves them."""
    black = _district_material("BlackCladding") or materials.get("Metal")
    fabric = _district_material("MeshWhite") or materials.get("Fabric") or materials.get("Carpet")
    _box((cx + x, cy + y, z + 4.0), (58.0, 58.0, 6.0), black, label + "_Base")
    _box((cx + x, cy + y, z + 24.0), (6.0, 6.0, 36.0), black, label + "_Column")
    _box((cx + x, cy + y, z + 46.0), (50.0, 48.0, 8.0), fabric, label + "_Seat")
    back = _box((cx + x, cy + y + facing * 24.0, z + 78.0), (46.0, 6.0, 52.0), fabric, label + "_Back")
    if back is not None:
        back.set_actor_rotation(unreal.Rotator(roll=facing * 6.0, pitch=0.0, yaw=0.0), False)


def build_open_plan_desks(cx, cy, z, x0, x1, y0, y1, materials, prefix, label):
    """Benching as the real floors have it: back-to-back pairs of 180 cm
    workstations on a central spine - thin oak tops on steel legs, a fabric
    screen, dual monitors lit, task chairs, a pedestal under each desk, and a
    planter closing each row. The tops are cover only at crouch; the screen
    and monitors hide a head, not a body."""
    wood = _district_material("DeskWhite") or materials.get("Wood")
    fabric = materials.get("Fabric") or wood
    steel = _district_material("Aluminium") or materials.get("Metal")
    black = _district_material("BlackCladding") or materials.get("Metal")
    glow = unreal.EditorAssetLibrary.load_asset(C.MATERIALS_INSTANCES + "/MI_OB_Signage_SignalBlue")
    pedestal_mat = materials.get("Metal")
    row = 0
    y = y0 + 160.0
    while y + 160.0 <= y1:
        row += 1
        x = x0 + 120.0
        col = 0
        while x + 360.0 <= x1:
            col += 1
            tag = "{0}_{1}_Bench_{2}_{3}".format(prefix, label, row, col)
            bx = x + 180.0
            # Top and legs.
            _box((cx + bx, cy + y, z + 72.5), (360.0, 160.0, 3.0), wood, tag + "_Top")
            for lx in (-170.0, 0.0, 170.0):
                _box((cx + bx + lx, cy + y, z + 35.5), (6.0, 150.0, 71.0), steel,
                     "{0}_Leg_{1}".format(tag, int(lx)))
            # Spine screen down the middle.
            _box((cx + bx, cy + y, z + 74.0 + 20.0), (356.0, 3.0, 40.0), fabric, tag + "_Screen")
            # Each side: two desks, each with two monitors, a pedestal, a chair.
            for side in (-1.0, 1.0):
                for seat, sx in enumerate((-90.0, 90.0)):
                    stag = "{0}_{1}{2}".format(tag, "S" if side < 0 else "N", seat + 1)
                    for m, mx in enumerate((-26.0, 26.0)):
                        mon = (cx + bx + sx + mx, cy + y + side * 22.0, z + 74.0 + 30.0)
                        _box(mon, (50.0, 3.0, 30.0), black, "{0}_Monitor{1}".format(stag, m + 1))
                        if glow is not None:
                            _box((mon[0], mon[1] + side * 1.7, mon[2] + 1.0), (47.0, 0.4, 26.0), glow,
                                 "{0}_Monitor{1}_Screen".format(stag, m + 1))
                        _box((mon[0], mon[1] - side * 2.0, z + 74.0 + 7.0), (6.0, 6.0, 14.0), black,
                             "{0}_Monitor{1}_Stand".format(stag, m + 1))
                    _box((cx + bx + sx + 55.0, cy + y + side * 45.0, z + 30.0), (40.0, 55.0, 60.0),
                         pedestal_mat, stag + "_Pedestal")
                    # Chairs pushed in, each one a little turned.
                    jitter = ((row * 7 + col * 3 + seat) % 5 - 2) * 6.0
                    _task_chair(cx, cy, z, bx + sx + jitter, y + side * 115.0, side, materials, stag + "_Chair")
            x += 480.0
        # A tall planter or a cubby unit closes each row on the aisle.
        if col:
            if row % 2:
                _planter(cx, cy, z, x0 + 30.0, x0 + 100.0, y - 60.0, y + 60.0, prefix,
                         "{0}_Row{1}".format(label, row), trees=0, height=110.0)
            else:
                _cubby(cx, cy, z, x0 + 55.0, y, prefix, "{0}_{1}_Row{2}_Cubby".format(prefix, label, row), along_x=False)
        y += 420.0
    _note_surface("Wood", prefix + "_" + label)


def build_server_racks(cx, cy, z, x0, x1, y0, y1, materials, prefix, label):
    """Server rows: individual 60 cm racks, 2 m tall, black bodies behind
    perforated steel doors, status LEDs on both faces, a yellow cable tray over
    each row. The aisles are long and straight: hard cover, sparks, and the
    one room in the building lit mostly by its machines."""
    black = _district_material("BlackCladding") or materials.get("Metal")
    door = materials.get("Grating") or materials.get("Metal")
    tray = unreal.EditorAssetLibrary.load_asset(C.MATERIALS_INSTANCES + "/MI_OB_Warning_Yellow") or materials.get("Metal")
    blue = unreal.EditorAssetLibrary.load_asset(C.MATERIALS_INSTANCES + "/MI_OB_Signage_SignalBlue")
    white = unreal.EditorAssetLibrary.load_asset(C.MATERIALS_INSTANCES + "/MI_OB_Signage_White")
    row = 0
    x = x0 + 120.0
    while x + 60.0 <= x1 - 60.0:
        row += 1
        ry0, ry1 = y0 + 130.0, y1 - 130.0
        y = ry0
        rack = 0
        while y + 60.0 <= ry1:
            rack += 1
            tag = "{0}_{1}_Rack_{2}_{3:02d}".format(prefix, label, row, rack)
            _box((cx + x, cy + y + 30.0, z + 100.0), (56.0, 58.0, 200.0), black, tag)
            for face in (-1.0, 1.0):
                _box((cx + x + face * 29.0, cy + y + 30.0, z + 100.0), (2.0, 54.0, 194.0), door,
                     "{0}_Door{1}".format(tag, "W" if face < 0 else "E"))
                for led in range(4):
                    mat = blue if (rack + led + row) % 3 else white
                    if mat is None:
                        continue
                    _box((cx + x + face * 30.5, cy + y + 12.0 + led * 9.0, z + 150.0 - ((rack * 7 + led) % 5) * 22.0),
                         (1.0, 3.0, 2.0), mat, "{0}_LED{1}{2}".format(tag, "W" if face < 0 else "E", led))
            y += 60.0
        _box((cx + x, cy + (ry0 + ry1) / 2.0, z + 250.0), (40.0, ry1 - ry0, 8.0), tray,
             "{0}_{1}_CableTray_{2}".format(prefix, label, row))
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
    build_servery_detail(origin_x, origin_y, z, materials, prefix)
    build_canteen_kitchen(origin_x, origin_y, z, materials, prefix)

    # Three servery islands in a row, clear of the lift banks at x > -445.
    for index, x in enumerate((-1500.0, -1050.0, -600.0)):
        _box(at(x, -1030.0, counter_h), (360.0, 120.0, counter_h), wood,
             "{0}_Servery_Island_{1}".format(prefix, index + 1))

    # Eighteen six-seat tables in three rows, widening east across the hall:
    # pale oak tops on black frames, three chairs a side, pushed in unevenly.
    black = _district_material("BlackCladding") or metal
    oak = _district_material("TimberLight") or wood
    for row, y in enumerate((-1650.0, -1300.0, -950.0)):
        for col in range(6):
            x = 600.0 + col * 300.0
            tag = "{0}_Canteen_Table_{1}_{2}".format(prefix, row + 1, col + 1)
            _box((origin_x + x, origin_y + y, z + 74.0), (180.0, 75.0, 4.0), oak, tag)
            for e in (-1.0, 1.0):
                _box((origin_x + x + e * 80.0, origin_y + y, z + 36.0), (5.0, 65.0, 72.0), black, "{0}_Frame{1}".format(tag, int(e)))
            for seat in range(3):
                sx = x - 60.0 + seat * 60.0
                for side in (-1.0, 1.0):
                    push = 48.0 + ((row * 5 + col * 3 + seat) % 4) * 6.0
                    _box((origin_x + sx, origin_y + y + side * push, z + 45.0), (40.0, 40.0, 5.0), black,
                         "{0}_Chair{1}{2}_Seat".format(tag, seat, int(side)))
                    _box((origin_x + sx, origin_y + y + side * (push + 18.0), z + 70.0), (38.0, 4.0, 45.0), black,
                         "{0}_Chair{1}{2}_Back".format(tag, seat, int(side)))
                    _box((origin_x + sx, origin_y + y + side * push, z + 22.0), (4.0, 4.0, 44.0), black,
                         "{0}_Chair{1}{2}_Leg".format(tag, seat, int(side)))

    # The balcony's north edge: a glass balustrade, waist high.
    glass = materials.get("Glass") or materials.get("BallisticGlass")
    _box(at(0.0, 1790.0, 110.0), (4400.0, 12.0, 110.0), glass, prefix + "_Balcony_Balustrade_N")

    _note_surface("Wood", prefix + "_Canteen_Counters")
    _note_surface("Metal", prefix + "_Coffee_Backwall")


def build_servery_detail(cx, cy, z, materials, prefix):
    """The hot-food line and coffee bar as the photograph shows them: steel
    service wells under a sneeze-guard and a warm heat-lamp gantry, menu screens
    over the line, a coffee machine and grinder on the till, a lit glass-door
    chiller; a tray slide along the counter, the tray stack where the queue
    starts and the cutlery station where it ends. The extract canopy is the
    kitchen's own, over its cooking line (build_canteen_kitchen)."""
    steel = _district_material("Aluminium") or materials.get("Metal")
    black = _district_material("BlackCladding") or materials.get("Metal")
    glass = materials.get("Glass") or materials.get("BallisticGlass")
    warm = _district_material("LightRibbon")
    for i in range(7):
        wx = -1750.0 + i * 200.0
        _box((cx + wx, cy - 1300.0, z + 96.0), (170.0, 60.0, 3.0), steel, "{0}_Hot_Food_Well{1}".format(prefix, i + 1))
    _box((cx - 1150.0, cy - 1335.0, z + 130.0), (1400.0, 2.0, 60.0), glass, prefix + "_Sneeze_Guard")
    _box((cx - 1150.0, cy - 1300.0, z + 175.0), (1400.0, 40.0, 6.0), black, prefix + "_Heat_Gantry")
    if warm is not None:
        _box((cx - 1150.0, cy - 1300.0, z + 171.0), (1380.0, 6.0, 2.0), warm, prefix + "_Heat_Lamps")
    # Facing down, yawed so the source's width (its local Y) runs along the line.
    lamp = _spawn(unreal.RectLight, unreal.Vector(cx - 1150.0, cy - 1300.0, z + 168.0),
                  unreal.Rotator(roll=0.0, pitch=-90.0, yaw=90.0))
    if lamp is not None:
        component = lamp.rect_light_component
        component.set_editor_property("IntensityUnits", unreal.LightUnits.LUMENS)
        component.set_editor_property("Intensity", 6000.0)
        component.set_editor_property("SourceWidth", 1300.0)
        component.set_editor_property("SourceHeight", 30.0)
        component.set_editor_property("AttenuationRadius", 400.0)
        component.set_editor_property("LightColor", unreal.Color(r=255, g=190, b=120, a=255))
        component.set_editor_property("CastShadows", False)
        component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
        _label(lamp, prefix + "_Heat_Lamp_Light")
    # Menu screens over the line.
    for i, mx in enumerate((-1550.0, -1150.0, -750.0)):
        _display(cx, cy, (mx, -1492.0, z + 238.0), unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0), 340.0, 150.0,
                 "CanteenMenu", "{0}_Display_Menu{1}".format(prefix, i + 1), phase=i * 7.0, brightness=45.0)
    # Coffee bar.
    _box((cx - 1880.0, cy - 1400.0, z + 128.0), (60.0, 70.0, 45.0), steel, prefix + "_Coffee_Machine")
    _box((cx - 1880.0, cy - 1320.0, z + 125.0), (25.0, 25.0, 40.0), black, prefix + "_Coffee_Grinder")
    _box((cx - 2140.0, cy - 900.0, z + 100.0), (70.0, 180.0, 200.0), black, prefix + "_Chiller")
    _box((cx - 2104.0, cy - 900.0, z + 100.0), (2.0, 170.0, 185.0), glass, prefix + "_Chiller_Door")
    if warm is not None:
        _box((cx - 2125.0, cy - 900.0, z + 100.0), (2.0, 160.0, 175.0), warm, prefix + "_Chiller_Light")

    # The tray slide: three steel rails on brackets along the queue side of
    # the hot-food counter, at 85 cm, for the length of the wells.
    for i in range(3):
        _block(cx, cy, z, -1835.0, -465.0, -1248.0 + i * 8.0, -1245.0 + i * 8.0, 83.0, 86.0, steel,
               "{0}_Tray_Slide_Rail{1}".format(prefix, i + 1))
    for i in range(7):
        bx = -1800.0 + i * 220.0
        _block(cx, cy, z, bx - 2.0, bx + 2.0, -1255.0, -1228.0, 80.0, 83.0, steel, "{0}_Tray_Slide_Bracket{1}".format(prefix, i + 1))

    # The queue starts at the east end of the islands: a stack of trays in a
    # steel holder on the third island. It ends at the till, where the first
    # island carries the cutlery station - four cutlery cylinders in a steel
    # caddy, a napkin dispenser and a condiment tray.
    _block(cx, cy, z, -470.0 - 25.0, -470.0 + 25.0, -1030.0 - 21.0, -1030.0 + 21.0, 95.0, 97.0, steel, prefix + "_Tray_Holder")
    for i in range(12):
        _block(cx, cy, z, -470.0 - 23.0, -470.0 + 23.0, -1030.0 - 18.0, -1030.0 + 18.0, 97.0 + i * 2.0, 98.6 + i * 2.0,
               black, "{0}_Tray_{1}".format(prefix, i + 1))
    caddy_x = -1620.0
    _block(cx, cy, z, caddy_x - 30.0, caddy_x + 30.0, -1050.0, -1010.0, 95.0, 103.0, steel, prefix + "_Cutlery_Caddy")
    for i in range(4):
        px = caddy_x - 22.5 + i * 15.0
        _block(cx, cy, z, px - 5.5, px + 5.5, -1035.5, -1024.5, 103.0, 117.0, steel,
               "{0}_Cutlery_Pot{1}".format(prefix, i + 1), mesh_path=CYLINDER_MESH)
    _block(cx, cy, z, caddy_x + 42.0, caddy_x + 60.0, -1040.0, -1020.0, 95.0, 120.0, black, prefix + "_Napkin_Dispenser")
    _block(cx, cy, z, caddy_x + 70.0, caddy_x + 110.0, -1045.0, -1015.0, 95.0, 97.0, black, prefix + "_Condiment_Tray")
    for i in range(3):
        px = caddy_x + 78.0 + i * 12.0
        _block(cx, cy, z, px - 3.5, px + 3.5, -1033.5, -1026.5, 97.0, 115.0, black if i != 1 else steel,
               "{0}_Condiment{1}".format(prefix, i + 1), mesh_path=CYLINDER_MESH)
    _note_surface("Metal", prefix + "_Servery_Stations")


# Floor 15's kitchen: the strip behind the kitchen wall, from the wall's face
# to the south slab edge. It is a route as well as a room - open at its east end
# by the staff door and at its west end behind the till - so the aisle down the
# middle stays 120 cm clear end to end, knobs and handles included.
CANTEEN_KITCHEN_WALL_Y = -1507.5
CANTEEN_KITCHEN_GLASS_Y = -1800.0


def _steel_bench(cx, cy, z, xa, xb, ya, yb, steel, label, upstand_y=None, undershelf=True):
    """A stainless catering bench: 900 top with a 100 upstand on the wall
    side, box-section legs and a pot shelf underneath."""
    _block(cx, cy, z, xa, xb, ya, yb, 86.0, 90.0, steel, label + "_Top")
    if upstand_y is not None:
        _block(cx, cy, z, xa, xb, upstand_y - 1.0, upstand_y + 1.0, 90.0, 100.0, steel, label + "_Upstand")
    for i, (lx, ly) in enumerate(((xa + 4.0, ya + 4.0), (xb - 4.0, ya + 4.0), (xa + 4.0, yb - 4.0), (xb - 4.0, yb - 4.0))):
        _block(cx, cy, z, lx - 2.0, lx + 2.0, ly - 2.0, ly + 2.0, 0.0, 86.0, steel, "{0}_Leg{1}".format(label, i + 1))
    if undershelf:
        _block(cx, cy, z, xa + 2.0, xb - 2.0, ya + 2.0, yb - 2.0, 20.0, 21.5, steel, label + "_Shelf")


def build_canteen_kitchen(cx, cy, z, materials, prefix):
    """The canteen's back-of-house kitchen on floor 15, behind the hot-food line.

    The cooking line stands against the kitchen wall, back to back with the
    servery, under one extract canopy: a two-door upright fridge, a combination
    oven on its stand, a six-burner range, a solid top, a twin-tank fryer, a
    chargrill and a bratt pan. Past the canopy, towards the staff door: a prep
    bench with a wall shelf, the pot-wash sink with its pre-rinse spray, a
    pass-through dishwasher with its outlet table, and the hand-wash basin by
    the door. Along the glass, a line of stainless prep benches with
    undercounter fridges, a mixer, boards and a microwave. All of it is 90 cm
    catering kit at catering sizes; the aisle between stays clear.
    """
    steel = _district_material("Stainless") or _district_material("Aluminium") or materials.get("Metal")
    black = _district_material("BlackCladding") or materials.get("Metal")
    white = _district_material("DeskWhite") or materials.get("Plasterboard")
    ribbon = _district_material("LightRibbon")
    wall_y = CANTEEN_KITCHEN_WALL_Y
    tag = prefix + "_Kitchen"

    def block(xa, xb, ya, yb, za, zb, material, name, mesh_path=None):
        return _block(cx, cy, z, xa, xb, ya, yb, za, zb, material, "{0}_{1}".format(tag, name), mesh_path)

    def on_legs(xa, xb, depth, za, zb, name):
        """An appliance body on four 15 cm adjustable feet, backed to the wall."""
        front = wall_y - depth
        block(xa, xb, front, wall_y, za, zb, steel, name)
        for i, (fx, fy) in enumerate(((xa + 5.0, front + 5.0), (xb - 5.0, front + 5.0),
                                      (xa + 5.0, wall_y - 5.0), (xb - 5.0, wall_y - 5.0))):
            block(fx - 2.0, fx + 2.0, fy - 2.0, fy + 2.0, 0.0, za, black, "{0}_Foot{1}".format(name, i + 1),
                  mesh_path=CYLINDER_MESH)
        return front

    # --- The cooking line, west to east ----------------------------------
    # Two-door upright fridge.
    front = on_legs(-1820.0, -1680.0, 80.0, 12.0, 200.0, "Upright_Fridge")
    for i, (da, db) in enumerate(((-1819.0, -1751.0), (-1749.0, -1681.0))):
        block(da, db, front - 1.5, front, 14.0, 198.0, steel, "Upright_Fridge_Door{0}".format(i + 1))
        hx = db - 6.0 if i == 0 else da + 6.0
        block(hx - 1.5, hx + 1.5, front - 5.0, front - 1.5, 90.0, 150.0, steel, "Upright_Fridge_Handle{0}".format(i + 1))
    block(-1815.0, -1685.0, front - 1.6, front - 1.5, 185.0, 195.0, black, "Upright_Fridge_Display")

    # Combination oven on its stand.
    xa, xb = -1670.0, -1580.0
    stand_front = wall_y - 80.0
    for i, (lx, ly) in enumerate(((xa + 4.0, stand_front + 4.0), (xb - 4.0, stand_front + 4.0),
                                  (xa + 4.0, wall_y - 4.0), (xb - 4.0, wall_y - 4.0))):
        block(lx - 2.0, lx + 2.0, ly - 2.0, ly + 2.0, 0.0, 70.0, steel, "Combi_Stand_Leg{0}".format(i + 1))
    block(xa, xb, stand_front, wall_y, 66.0, 70.0, steel, "Combi_Stand_Top")
    block(xa + 2.0, xb - 2.0, stand_front + 2.0, wall_y - 2.0, 20.0, 21.5, steel, "Combi_Stand_Shelf")
    combi_front = wall_y - 85.0
    block(xa, xb, combi_front, wall_y, 70.0, 150.0, steel, "Combi_Oven")
    block(xa + 4.0, xa + 64.0, combi_front - 1.0, combi_front, 76.0, 144.0, black, "Combi_Oven_Door")
    block(xa + 68.0, xb - 3.0, combi_front - 1.0, combi_front, 110.0, 144.0, black, "Combi_Oven_Controls")
    block(xa + 58.0, xa + 61.0, combi_front - 5.0, combi_front - 1.0, 90.0, 130.0, steel, "Combi_Oven_Handle")

    # Six-burner range over an oven.
    xa, xb = -1580.0, -1490.0
    front = on_legs(xa, xb, 90.0, 15.0, 89.0, "Range")
    block(xa, xb, front, wall_y, 89.0, 90.0, black, "Range_Hob")
    for row in range(2):
        for col in range(3):
            bx = xa + 15.0 + col * 30.0
            by = front + 25.0 + row * 40.0
            block(bx - 11.0, bx + 11.0, by - 11.0, by + 11.0, 90.0, 92.0, black,
                  "Range_Burner{0}{1}".format(row + 1, col + 1), mesh_path=CYLINDER_MESH)
    block(xa + 2.0, xb - 2.0, front - 4.0, front, 78.0, 86.0, black, "Range_Knob_Rail")
    block(xa + 8.0, xb - 8.0, front - 4.0, front - 1.0, 70.0, 72.0, steel, "Range_Oven_Handle")

    # Solid top.
    xa, xb = -1490.0, -1400.0
    front = on_legs(xa, xb, 90.0, 15.0, 89.0, "Solid_Top")
    block(xa + 2.0, xb - 2.0, front + 2.0, wall_y - 2.0, 89.0, 91.0, black, "Solid_Top_Plate")
    block(xa + 2.0, xb - 2.0, front - 4.0, front, 78.0, 86.0, black, "Solid_Top_Knob_Rail")

    # Twin-tank fryer: two tanks, a basket hung in each.
    xa, xb = -1400.0, -1320.0
    front = on_legs(xa, xb, 90.0, 15.0, 90.0, "Fryer")
    for i in range(2):
        tx = xa + 20.0 + i * 40.0
        block(tx - 16.0, tx + 16.0, front + 15.0, front + 60.0, 90.0, 90.3, black, "Fryer_Tank{0}".format(i + 1))
        block(tx - 1.0, tx + 1.0, front + 2.0, front + 18.0, 96.0, 98.0, steel, "Fryer_Basket{0}_Handle".format(i + 1))
        block(tx - 12.0, tx + 12.0, front + 18.0, front + 50.0, 90.3, 100.0, steel, "Fryer_Basket{0}".format(i + 1))
    block(xa + 4.0, xb - 4.0, wall_y - 14.0, wall_y - 2.0, 90.0, 115.0, steel, "Fryer_Flue")

    # Chargrill on a stand: cast bars across a lava-rock bed.
    xa, xb = -1320.0, -1240.0
    front = on_legs(xa, xb, 90.0, 15.0, 88.0, "Chargrill")
    for i in range(9):
        gx = xa + 8.0 + i * 8.0
        block(gx - 1.5, gx + 1.5, front + 5.0, wall_y - 18.0, 88.0, 91.0, black, "Chargrill_Bar{0}".format(i + 1))
    block(xa + 2.0, xb - 2.0, front - 4.0, front, 78.0, 86.0, black, "Chargrill_Knob_Rail")

    # Bratt pan: a tilting pan with its lid up against the wall.
    xa, xb = -1240.0, -1120.0
    front = on_legs(xa, xb, 85.0, 40.0, 90.0, "Bratt_Pan")
    block(xa + 5.0, xb - 5.0, wall_y - 12.0, wall_y - 10.0, 90.0, 150.0, steel, "Bratt_Pan_Lid")
    block(xb - 2.0, xb + 6.0, front + 30.0, front + 50.0, 55.0, 75.0, black, "Bratt_Pan_Tilt_Wheel",
          mesh_path=CYLINDER_MESH)

    # The extract canopy over the whole cooking line: 15 cm past each end and
    # 30 cm past the fronts, its underside at 205 cm, ducted up to the soffit.
    canopy_x0, canopy_x1 = -1835.0 + 150.0, -1105.0
    canopy_front = wall_y - 120.0
    block(canopy_x0, canopy_x1, canopy_front, wall_y, 205.0, 255.0, steel, "Extract_Canopy")
    block(canopy_x0 + 10.0, canopy_x1 - 10.0, wall_y - 45.0, wall_y - 5.0, 204.0, 205.0, black, "Extract_Canopy_Filters")
    if ribbon is not None:
        block(canopy_x0 + 20.0, canopy_x1 - 20.0, canopy_front + 12.0, canopy_front + 16.0, 204.0, 205.0, ribbon,
              "Extract_Canopy_Light")
    for i, dx in enumerate((-1540.0, -1250.0)):
        block(dx - 25.0, dx + 25.0, wall_y - 80.0, wall_y - 40.0, 255.0, C.CEILING_HEIGHT_CM - 40.0, steel,
              "Extract_Duct{0}".format(i + 1))
    _soft_light(unreal.RectLight,
                unreal.Vector(cx + (canopy_x0 + canopy_x1) / 2.0, cy + canopy_front + 14.0, z + 202.0),
                unreal.Rotator(roll=0.0, pitch=-90.0, yaw=90.0), 6000.0, 260.0, unreal.Color(r=255, g=244, b=232, a=255),
                tag + "_Canopy_Light", width=canopy_x1 - canopy_x0 - 40.0, height=4.0)

    # --- Past the canopy, towards the staff door --------------------------
    _steel_bench(cx, cy, z, -1110.0, -930.0, wall_y - 70.0, wall_y, steel, tag + "_Prep_Bench", upstand_y=wall_y - 1.0)
    block(-1110.0, -930.0, wall_y - 30.0, wall_y, 150.0, 152.0, steel, "Wall_Shelf")
    for i, sx in enumerate((-1100.0, -940.0)):
        block(sx - 1.0, sx + 1.0, wall_y - 28.0, wall_y, 135.0, 150.0, steel, "Wall_Shelf_Bracket{0}".format(i + 1))
    for i in range(3):
        px = -1080.0 + i * 55.0
        block(px - 26.5, px + 26.5, wall_y - 30.0, wall_y - 2.0, 152.0, 172.0 - i * 4.0, steel, "GN_Pans{0}".format(i + 1))

    # Pot wash: a double bowl and drainer with the pre-rinse spray over it.
    _steel_bench(cx, cy, z, -920.0, -740.0, wall_y - 70.0, wall_y, steel, tag + "_Pot_Wash", upstand_y=wall_y - 1.0)
    for i, bx in enumerate((-890.0, -830.0)):
        block(bx - 26.0, bx + 26.0, wall_y - 60.0, wall_y - 12.0, 90.0, 90.2, black, "Pot_Wash_Bowl{0}".format(i + 1))
    for i in range(6):
        gx = -790.0 + i * 8.0
        block(gx - 1.0, gx + 1.0, wall_y - 60.0, wall_y - 12.0, 90.0, 90.4, steel, "Pot_Wash_Drainer{0}".format(i + 1))
    block(-862.0, -858.0, wall_y - 8.0, wall_y - 4.0, 100.0, 215.0, steel, "Pre_Rinse_Pole", mesh_path=CYLINDER_MESH)
    block(-864.0, -856.0, wall_y - 10.0, wall_y - 2.0, 140.0, 195.0, steel, "Pre_Rinse_Spring", mesh_path=CYLINDER_MESH)
    block(-862.0, -858.0, wall_y - 40.0, wall_y - 8.0, 212.0, 215.0, steel, "Pre_Rinse_Arm")
    block(-863.0, -857.0, wall_y - 42.0, wall_y - 36.0, 125.0, 140.0, black, "Pre_Rinse_Head")

    # Pass-through dishwasher, its hood down, and the outlet table after it.
    front = wall_y - 75.0
    block(-740.0, -670.0, front, wall_y, 0.0, 85.0, steel, "Dishwasher")
    block(-742.0, -668.0, front - 2.0, wall_y, 85.0, 150.0, steel, "Dishwasher_Hood")
    block(-735.0, -675.0, front - 6.0, front - 3.0, 120.0, 122.0, steel, "Dishwasher_Hood_Handle")
    block(-735.0, -705.0, front - 2.5, front - 2.0, 135.0, 145.0, black, "Dishwasher_Controls")
    _steel_bench(cx, cy, z, -670.0, -570.0, wall_y - 70.0, wall_y, steel, tag + "_Outlet_Table", upstand_y=wall_y - 1.0)

    # Hand-wash basin by the door, and its towel dispenser.
    block(-560.0, -520.0, wall_y - 30.0, wall_y, 78.0, 95.0, steel, "Hand_Wash_Basin")
    block(-543.0, -537.0, wall_y - 6.0, wall_y - 2.0, 95.0, 112.0, steel, "Hand_Wash_Tap", mesh_path=CYLINDER_MESH)
    block(-555.0, -525.0, wall_y - 12.0, wall_y, 130.0, 160.0, steel, "Towel_Dispenser")

    # --- Along the glass: prep benches -----------------------------------
    glass_y = CANTEEN_KITCHEN_GLASS_Y + 5.0
    bench_front = glass_y + 70.0
    for i in range(6):
        xa = -1820.0 + i * 222.0
        xb = xa + 200.0
        name = "Glass_Bench{0}".format(i + 1)
        fridge = i in (0, 3)
        _steel_bench(cx, cy, z, xa, xb, glass_y, bench_front, steel, "{0}_{1}".format(tag, name),
                     upstand_y=glass_y + 1.0, undershelf=not fridge)
        if fridge:
            # Undercounter fridge: two doors under the top.
            block(xa + 6.0, xb - 6.0, glass_y + 3.0, bench_front - 3.0, 12.0, 86.0, steel, name + "_Fridge")
            for d in range(2):
                da = xa + 6.0 + d * 94.0
                block(da + 1.0, da + 93.0, bench_front - 3.0, bench_front - 1.5, 14.0, 84.0, steel,
                      "{0}_Fridge_Door{1}".format(name, d + 1))
                block(da + 20.0, da + 74.0, bench_front - 1.5, bench_front + 1.5, 78.0, 80.0, steel,
                      "{0}_Fridge_Handle{1}".format(name, d + 1))
        mid = (xa + xb) / 2.0
        if i == 1:
            # Planetary mixer: base, column, head and bowl.
            block(mid - 22.0, mid + 22.0, glass_y + 12.0, glass_y + 58.0, 90.0, 96.0, steel, name + "_Mixer_Base")
            block(mid - 10.0, mid + 10.0, glass_y + 8.0, glass_y + 24.0, 96.0, 150.0, steel, name + "_Mixer_Column")
            block(mid - 12.0, mid + 12.0, glass_y + 8.0, glass_y + 50.0, 140.0, 156.0, steel, name + "_Mixer_Head")
            block(mid - 15.0, mid + 15.0, glass_y + 25.0, glass_y + 55.0, 100.0, 125.0, steel, name + "_Mixer_Bowl",
                  mesh_path=CYLINDER_MESH)
        elif i == 2:
            for b in range(2):
                block(mid - 50.0 + b * 55.0, mid - 5.0 + b * 55.0, glass_y + 20.0, glass_y + 52.0, 90.0, 92.0 + b * 0.5,
                      white, "{0}_Board{1}".format(name, b + 1))
        elif i == 4:
            for p in range(4):
                block(mid - 26.5, mid + 26.5, glass_y + 20.0, glass_y + 52.0, 90.0 + p * 6.5, 96.0 + p * 6.5, steel,
                      "{0}_GN_Pan{1}".format(name, p + 1))
        elif i == 5:
            block(mid - 26.0, mid + 26.0, glass_y + 15.0, glass_y + 58.0, 90.0, 122.0, steel, name + "_Microwave")
            block(mid - 22.0, mid + 10.0, glass_y + 58.0, glass_y + 58.5, 95.0, 118.0, black, name + "_Microwave_Door")

    # Batten lights down the aisle, about 500 lux on the benches (CIBSE for a
    # commercial kitchen); the floor above the wall has none of its own.
    for i, lx in enumerate((-1450.0, -1000.0, -650.0)):
        if ribbon is not None:
            block(lx - 75.0, lx + 75.0, -1666.0, -1658.0, 300.0, 304.0, ribbon, "Batten{0}".format(i + 1))
    _soft_light(unreal.RectLight, unreal.Vector(cx - 1050.0, cy - 1662.0, z + 298.0),
                unreal.Rotator(roll=0.0, pitch=-90.0, yaw=90.0), 15000.0, 600.0, unreal.Color(r=255, g=248, b=240, a=255),
                tag + "_Aisle_Light", width=950.0, height=8.0)

    _note_surface("Metal", tag + "_Line")


def _planter(cx, cy, z, x0, x1, y0, y1, prefix, label, trees=0, height=90.0):
    """A long powder-coated steel planter of grasses, as on the real terraces,
    with multi-stem trees standing in it."""
    planter = _district_material("Planter")
    grass = _district_material("Grass")
    leaves = _district_material("Leaves")
    bark = _district_material("Bark")
    w, d = x1 - x0, y1 - y0
    mx, my = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    _box((cx + mx, cy + my, z + height / 2.0), (w, d, height), planter, "{0}_{1}_Planter".format(prefix, label))
    # The planting: a soft mound of grasses proud of the rim.
    _box((cx + mx, cy + my, z + height + 15.0), (w - 20.0, d - 20.0, 30.0), grass or leaves,
         "{0}_{1}_Grasses".format(prefix, label))
    for i in range(trees):
        t = (i + 0.5) / float(trees)
        tx, ty = (x0 + w * t, my) if w >= d else (mx, y0 + d * t)
        for stem, (ox, oy) in enumerate(((-12.0, 0.0), (10.0, 9.0), (4.0, -12.0))):
            _box((cx + tx + ox, cy + ty + oy, z + height + 170.0), (7.0, 7.0, 340.0), bark,
                 "{0}_{1}_Tree{2}_Stem{3}".format(prefix, label, i + 1, stem + 1))
        # A loose crown of three overlapping spheres, not a cube.
        for blob, (ox, oy, oz, d) in enumerate(((0.0, 0.0, 0.0, 240.0), (70.0, -40.0, -50.0, 170.0),
                                                (-60.0, 50.0, 40.0, 180.0))):
            _box((cx + tx + ox, cy + ty + oy, z + height + 400.0 + oz), (d, d, d * 0.8), leaves,
                 "{0}_{1}_Tree{2}_Crown{3}".format(prefix, label, i + 1, blob + 1), mesh_path=SPHERE_MESH)


def _balustrade(cx, cy, z, x0, y0, x1, y1, materials, label):
    """Frameless glass balustrade, 110 cm, with a slim steel cap."""
    glass = materials.get("Glass") or materials.get("BallisticGlass")
    cap = _district_material("Aluminium") or materials.get("Metal")
    _wall(cx, cy, z, x0, y0, x1, y1, glass, label, height=110.0, thickness=4.0)
    _wall(cx, cy, z, x0, y0, x1, y1, cap, label + "_Cap", height=4.0, thickness=6.0, base=110.0)


# The OLED screens' slides: fictional, the building's own voice. Each is
# (eyebrow, headline, subline, colour A, colour B).
VIOLET, BLUE = (0.42, 0.08, 1.0), (0.04, 0.42, 1.0)
AMBER, ROSE = (1.0, 0.45, 0.08), (1.0, 0.12, 0.35)
TEAL, LIME = (0.02, 0.85, 0.7), (0.35, 1.0, 0.25)
RED, ORANGE = (1.0, 0.05, 0.05), (1.0, 0.35, 0.02)
DISPLAY_SLIDES = {
    "Reception": [
        ("ONE BRAHAM  ·  ALDGATE", "Welcome to OB", "Eighteen floors. One way up.", VIOLET, BLUE),
        ("OB", "On break?", "The building runs on it. So do you.", ROSE, VIOLET),
        ("THE WINTER GARDEN  ·  LEVEL 17", "Above it all.", "Bar and roof terrace, the City at your feet", AMBER, ROSE),
        ("CANTEEN  ·  LEVEL 15", "Lunch is on 15.", "Hot food  ·  coffee bar  ·  the Skydeck", TEAL, LIME),
        ("SECURITY NOTICE", "Wear your pass.", "Keycards on show beyond the gates, at all times", RED, ORANGE),
    ],
    "EscalatorWall": [
        ("ON BREAK?", "Take five on 15.", "Canteen  ·  coffee bar  ·  the Skydeck balcony", TEAL, BLUE),
        ("LEVEL 1  ·  CLIENT SHOWCASE", "See what's next.", "Live demonstrations, daily, in the Showcase", VIOLET, ROSE),
        ("WELLBEING", "Take the stairs.", "The spiral stair runs all the way to 17", LIME, TEAL),
    ],
    "EscalatorTop": [
        ("LEVEL 1", "Welcome up.", "Lifts ahead on the left  ·  Showcase  ·  Lab", BLUE, VIOLET),
        ("TODAY ON 17", "Sunset sessions.", "The Winter Garden bar opens at five", AMBER, ROSE),
    ],
    "CanteenMenu": [
        ("TODAY'S HOT FOOD", "Chicken katsu", "Sticky rice  ·  pickled slaw  ·  £6.40", AMBER, ROSE),
        ("PLANT KITCHEN", "Squash & lentil dal", "Basmati  ·  coconut raita  ·  £5.20", LIME, TEAL),
        ("GRILL", "Smash burger", "Fries  ·  house pickles  ·  £7.10", RED, ORANGE),
    ],
    "EscalatorCeiling": [
        ("", "", "", VIOLET, BLUE),
        ("", "", "", TEAL, BLUE),
        ("", "", "", ROSE, VIOLET),
    ],
}


def _display(cx, cy, location, rotation, width, height, slides_key, label, show_copy=True, phase=0.0,
             brightness=60.0):
    """Places an AOneBrahamDisplay with its slides."""
    display_class = getattr(unreal, "OneBrahamDisplay", None)
    if display_class is None:
        return None
    x, y, zc = location
    actor = _spawn(display_class, unreal.Vector(cx + x, cy + y, zc), rotation)
    if actor is None:
        return None
    slides = []
    for eyebrow, headline, subline, a, b in DISPLAY_SLIDES[slides_key]:
        slide = unreal.OneBrahamDisplaySlide()
        slide.set_editor_property("Eyebrow", eyebrow)
        slide.set_editor_property("Headline", headline)
        slide.set_editor_property("Subline", subline)
        slide.set_editor_property("ColourA", unreal.LinearColor(a[0], a[1], a[2], 1.0))
        slide.set_editor_property("ColourB", unreal.LinearColor(b[0], b[1], b[2], 1.0))
        slides.append(slide)
    for prop, value in (("WidthCm", width), ("HeightCm", height), ("bShowCopy", show_copy),
                        ("Slides", slides), ("PhaseSeconds", phase), ("Brightness", brightness),
                        ("ScreenMaterial", unreal.EditorAssetLibrary.load_asset(C.MATERIALS + "/M_OB_AdScreen")),
                        ("BezelMaterial", _district_material("BlackCladding")),
                        ("AccentMaterial", unreal.EditorAssetLibrary.load_asset(
                            C.MATERIALS_INSTANCES + "/MI_OB_Signage_White")),
                        ("TextMaterial", unreal.load_asset(
                            "/Engine/EngineMaterials/DefaultTextMaterialTranslucent.DefaultTextMaterialTranslucent"))):
        actor.set_editor_property(prop, value)
    try:
        actor.rerun_construction_scripts()
    except Exception:  # noqa: BLE001 - construction runs on save/load anyway
        pass
    _label(actor, label)
    return actor


def build_ad_screens_in_place(cx, cy, z, floor_index, prefix):
    """The OLED screens. Reception: on the black feature volume over the desk.
    The escalators: a double-height screen wall the whole length of the run, a
    screen ceiling over the whole run, and the back wall at the top, right in
    front of you as you arrive."""
    facing_south = unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0)
    if floor_index == 0:
        _display(cx, cy, (900.0, -1004.0, z + 560.0), facing_south, 920.0, 260.0, "Reception",
                 prefix + "_Display_Reception")
        # The escalator wall: the enclosure's face, from just above the steps'
        # foot to the floor-1 ceiling, the length of the run.
        run_mid = (ESCALATOR_Y[0] + ESCALATOR_Y[1]) / 2.0
        bottom, top = z + 40.0, z + C.FLOOR_HEIGHT_CM + C.CEILING_HEIGHT_CM - 40.0
        _display(cx, cy, (ESCALATOR_X[1] + 25.0, run_mid, (bottom + top) / 2.0),
                 unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0),
                 ESCALATOR_Y[1] - ESCALATOR_Y[0] + 40.0, top - bottom, "EscalatorWall",
                 prefix + "_Display_EscalatorWall", phase=2.5)
    if floor_index == 1:
        mid = (ESCALATOR_X[0] + ESCALATOR_X[1]) / 2.0
        plaster = unreal.EditorAssetLibrary.load_asset(C.MATERIALS_INSTANCES + "/MI_Plasterboard")
        # The arrival: the wall straight ahead, the west side closed - the only
        # way on is left, then left again past the lift banks.
        _wall(cx, cy, z, ESCALATOR_X[0] - 20.0, ESCALATOR_ARRIVAL_WALL_Y, ESCALATOR_X[1] + 60.0,
              ESCALATOR_ARRIVAL_WALL_Y, plaster, prefix + "_Escalator_BackWall")
        _wall(cx, cy, z, ESCALATOR_X[0] - 20.0, GATE_LINE_Y, ESCALATOR_X[0] - 20.0, ESCALATOR_ARRIVAL_WALL_Y,
              plaster, prefix + "_Escalator_WestWall")
        _display(cx, cy, (mid, ESCALATOR_ARRIVAL_WALL_Y - 14.0, z + 205.0), facing_south,
                 ESCALATOR_X[1] - ESCALATOR_X[0], 300.0, "EscalatorTop", prefix + "_Display_EscalatorTop", phase=4.0)
        # The ceiling over the whole run, facing down: field only.
        _display(cx, cy, (mid, (ESCALATOR_Y[0] + ESCALATOR_Y[1]) / 2.0, z + C.CEILING_HEIGHT_CM - 48.0),
                 unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0),
                 ESCALATOR_Y[1] - ESCALATOR_Y[0] + 80.0, ESCALATOR_X[1] - ESCALATOR_X[0] + 40.0,
                 "EscalatorCeiling", prefix + "_Display_EscalatorCeiling", show_copy=False, brightness=35.0)


def build_feature_stair(cx, cy, z, materials, prefix):
    """The reception's open stair: floating timber treads on black sawtooth
    stringers, glass balustrades either side, air underneath."""
    x0, x1 = FEATURE_STAIR_X
    y0, y1 = FEATURE_STAIR_Y
    steps = FEATURE_STAIR_STEPS
    going = (y1 - y0) / steps
    rise = C.FLOOR_HEIGHT_CM / steps
    timber = _district_material("Timber") or materials.get("Wood")
    steel = _district_material("BlackCladding") or materials.get("Metal")
    glass = materials.get("Glass") or materials.get("BallisticGlass")
    for i in range(steps):
        top = rise * (i + 1)
        y = y0 + going * (i + 0.5)
        _box((cx + (x0 + x1) / 2.0, cy + y, z + top - 2.0), (x1 - x0, going + 2.0, 4.0), timber,
             "{0}_FeatureStair_Tread_{1:02d}".format(prefix, i + 1))
        for side, sx in (("W", x0 + 4.0), ("E", x1 - 4.0)):
            _box((cx + sx, cy + y, z + top - 20.0), (8.0, going + 2.0, 36.0), steel,
                 "{0}_FeatureStair_Stringer{1}_{2:02d}".format(prefix, side, i + 1))
            _box((cx + sx + (-6.0 if side == "W" else 6.0), cy + y, z + top + 52.0), (2.0, going + 1.0, 100.0), glass,
                 "{0}_FeatureStair_Glass{1}_{2:02d}".format(prefix, side, i + 1))


def build_reception_gallery(cx, cy, z, materials, prefix):
    """Floor 1 over the double-height reception: the gallery's glass edge, and
    the reception ceiling - black linear baffles with sweeping light ribbons
    running through them, as the real one has."""
    x0, x1, y0, y1 = RECEPTION_VOID
    _balustrade(cx, cy, z, x0, y0, x0, y1, materials, prefix + "_Reception_Gallery_W")
    # The north edge opens where the feature stair arrives.
    _balustrade(cx, cy, z, x0, y1, FEATURE_STAIR_X[0] - 10.0, y1, materials, prefix + "_Reception_Gallery_N")

    baffle = _district_material("Baffle") or materials.get("Metal")
    underside = z + C.CEILING_HEIGHT_CM - 20.0
    y = y0 + 20.0
    index = 0
    while y < y1 - 10.0:
        index += 1
        _box((cx + (x0 + x1) / 2.0, cy + y, underside - 14.0), (x1 - x0 - 20.0, 4.0, 28.0), baffle,
             "{0}_Reception_Baffle_{1:02d}".format(prefix, index))
        y += 25.0

    # Three ribbons of light sweeping the length of the hall, one in the
    # violet the real one uses. Short emissive segments along a sine.
    white = _district_material("LightRibbon")
    violet = _district_material("LightRibbonViolet") or white
    for ribbon, (yc, amp, period, phase, mat) in enumerate((
            (-1600.0, 130.0, 900.0, 0.0, white), (-1350.0, 170.0, 1300.0, 1.4, violet),
            (-1120.0, 110.0, 1000.0, 2.6, white))):
        step = 40.0
        x = x0 + 60.0
        prev = None
        while x <= x1 - 60.0:
            y = yc + amp * math.sin(x / period * 2.0 * math.pi + phase)
            if prev is not None:
                px, py = prev
                length = math.hypot(x - px, y - py)
                yaw = math.degrees(math.atan2(y - py, x - px))
                piece = _box((cx + (x + px) / 2.0, cy + (y + py) / 2.0, underside - 70.0 - ribbon * 18.0),
                             (length + 2.0, 14.0, 2.0), mat,
                             "{0}_Reception_Ribbon{1}_{2:03d}".format(prefix, ribbon + 1, int(x)))
                if piece is not None:
                    piece.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), False)
            prev = (x, y)
            x += step


def build_summit(cx, cy, z, materials, prefix):
    """Floor 16: the Management Suite and the collaboration lounge, round the
    foot of the atrium garden that rises through 17 to the Winter Garden's
    glass roof."""
    x0, x1, y0, y1 = ATRIUM_VOID
    # The atrium garden: planters framing the void's footprint, trees tall
    # enough to reach up into the Winter Garden.
    _planter(cx, cy, z, x0, x1, y0, y0 + 120.0, prefix, "Atrium_S", trees=3)
    _planter(cx, cy, z, x0, x1, y1 - 120.0, y1, prefix, "Atrium_N", trees=3)
    _planter(cx, cy, z, x0, x0 + 120.0, y0 + 160.0, y1 - 160.0, prefix, "Atrium_W", trees=1)

    # The lounge: low sofas and timber tables, cover at crouch height.
    fabric = materials.get("Fabric")
    timber = _district_material("Timber") or materials.get("Wood")
    for row, y in enumerate((-1550.0, -1200.0, -850.0)):
        for col, x in enumerate((-550.0, 0.0, 450.0)):
            _box((cx + x, cy + y, z + 22.0), (180.0, 80.0, 44.0), fabric,
                 "{0}_Lounge_Sofa_{1}_{2}".format(prefix, row + 1, col + 1))
            _box((cx + x, cy + y + 95.0, z + 20.0), (110.0, 60.0, 40.0), timber,
                 "{0}_Lounge_Table_{1}_{2}".format(prefix, row + 1, col + 1))


def build_winter_garden(cx, cy, z, materials, prefix):
    """Floor 17: the Winter Garden - a glass-roofed bar and garden round the
    atrium void - and, through a glass screen, the open roof terrace."""
    glass = materials.get("Glass") or materials.get("BallisticGlass")
    timber = _district_material("Timber") or materials.get("Wood")
    x0, x1, y0, y1 = ATRIUM_VOID
    for label, (ax0, ay0, ax1, ay1) in (("S", (x0, y0, x1, y0)), ("N", (x0, y1, x1, y1)),
                                        ("W", (x0, y0, x0, y1)), ("E", (x1, y0, x1, y1))):
        _balustrade(cx, cy, z, ax0, ay0, ax1, ay1, materials, "{0}_Atrium_Edge_{1}".format(prefix, label))

    # The bar: a long timber counter on the south glazing, stools in front.
    _box((cx + 200.0, cy - 1700.0, z + 55.0), (900.0, 70.0, 110.0), timber, prefix + "_WG_Bar_Counter")
    for i in range(7):
        _box((cx - 200.0 + i * 130.0, cy - 1610.0, z + 38.0), (40.0, 40.0, 76.0), materials.get("Metal"),
             "{0}_WG_Bar_Stool_{1}".format(prefix, i + 1))
    for row, y in enumerate((-1300.0, -900.0)):
        for col, x in enumerate((-300.0, 150.0, 550.0)):
            _box((cx + x, cy + y, z + 37.0), (90.0, 90.0, 74.0), timber,
                 "{0}_WG_Table_{1}_{2}".format(prefix, row + 1, col + 1))
    _planter(cx, cy, z, -650.0, -450.0, -1700.0, -700.0, prefix, "WG_Bed", trees=2)

    # The glass screen between the garden and the terrace, with its door.
    _wall_with_door(cx, cy, z, WINTER_GARDEN_EAST_X, -1800.0, WINTER_GARDEN_EAST_X, SKYDECK_SCREEN_Y,
                    -1175.0, 180.0, glass, prefix + "_WG_Terrace_Screen")
    _wall(cx, cy, z, WINTER_GARDEN_EAST_X, SKYDECK_SCREEN_Y, CROWN_STALK_X, SKYDECK_SCREEN_Y, glass,
          prefix + "_WG_Terrace_Screen_N")

    # The roof terrace: pale large-format paving, charcoal planters of grasses
    # and multi-stem trees, a glass balustrade at the drop to the Skydeck.
    tx0, tx1, ty0, ty1 = TERRACE_17
    paving = _district_material("TerracePaving")
    _box((cx + (tx0 + tx1) / 2.0, cy + (ty0 + ty1) / 2.0, z + 0.3),
         (tx1 - tx0 - 20.0, ty1 - ty0 - 20.0, 0.6), paving, prefix + "_Terrace_Paving")
    _planter(cx, cy, z, 1000.0, 2100.0, -1760.0, -1640.0, prefix, "Terrace_S", trees=2)
    _planter(cx, cy, z, 2060.0, 2160.0, -1500.0, -800.0, prefix, "Terrace_E", trees=1)
    _planter(cx, cy, z, 1200.0, 1700.0, -1200.0, -1100.0, prefix, "Terrace_Mid", trees=0, height=60.0)
    _picnic_bench(cx, cy, z, 1450.0, -900.0, prefix + "_Terrace_Bench1")
    _picnic_bench(cx, cy, z, 1850.0, -1400.0, prefix + "_Terrace_Bench2")
    _balustrade(cx, cy, z, CROWN_STALK_X, SKYDECK_SCREEN_Y, 2200.0, SKYDECK_SCREEN_Y, materials,
                prefix + "_Terrace_Edge_N")


def build_winter_garden_roof(cx, cy, z, materials, prefix):
    """The top of the building: the Winter Garden's glass roof on steel ribs,
    a solid roof over the core stalk, and open sky over the terrace."""
    glass = materials.get("Glass") or materials.get("BallisticGlass")
    rib = _district_material("Aluminium") or materials.get("Metal")
    concrete = materials.get("Concrete")
    top = z + C.CEILING_HEIGHT_CM
    gx0, gx1, gy0, gy1 = -2200.0, WINTER_GARDEN_EAST_X, -1800.0, SKYDECK_SCREEN_Y
    _box((cx + (gx0 + gx1) / 2.0, cy + (gy0 + gy1) / 2.0, top + 5.0), (gx1 - gx0, gy1 - gy0, 4.0), glass,
         prefix + "_WG_Glass_Roof")
    x = gx0
    index = 0
    while x <= gx1 + 1.0:
        index += 1
        _box((cx + x, cy + (gy0 + gy1) / 2.0, top - 12.0), (10.0, gy1 - gy0, 30.0), rib,
             "{0}_WG_Roof_Rib_{1:02d}".format(prefix, index))
        x += 300.0
    for label, y in (("S", gy0), ("N", gy1)):
        _box((cx + (gx0 + gx1) / 2.0, cy + y, top - 12.0), (gx1 - gx0, 12.0, 30.0), rib,
             prefix + "_WG_Roof_Edge_" + label)
    # The core stalk is roofed solid: plant and lift motor rooms live up here.
    _box((cx, cy + (SKYDECK_SCREEN_Y + CROWN_NORTH_Y) / 2.0, top + 10.0),
         (2.0 * CROWN_STALK_X, CROWN_NORTH_Y - SKYDECK_SCREEN_Y, 20.0), concrete, prefix + "_Core_Roof")


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
    if area_name in ("Skydeck", "WinterGarden"):
        slab_material = decking or concrete

    # 20 cm of slab, cut over the stairs, the lift shafts and (on floor 1) the
    # escalators - without the cuts nobody can climb.
    _slab_with_holes(origin_x, origin_y, z, size_x, size_y, _slab_holes(floor_index), slab_material, prefix + "_Slab")
    created.append((prefix + "_Slab", (origin_x, origin_y, z - 10.0)))
    _note_surface("Marble" if area_name in ("Lobby", "Ground_Plaza") else
                  ("Decking" if area_name in ("Skydeck", "WinterGarden") else "Carpet"),
                  prefix + "_Slab")

    # --- Ceiling ----------------------------------------------------------
    # One Braham has no suspended ceilings: the soffit is bare concrete with the
    # services hung under it (see build_exposed_services). Floor 15 is roofed
    # only over the canteen; the Skydeck balcony is open sky.
    #
    # The soffit sits directly UNDER the next floor's slab (z+360 to z+380 under
    # a slab at z+380 to z+400). It used to sit on top of it, 20 cm proud, so on
    # every floor players walked on a hidden concrete layer instead of the carpet
    # or stone, and every stair arrived 20 cm short.
    if area_name == "Skydeck":
        roofed_y = SKYDECK_SCREEN_Y + half_y
        _box((origin_x, origin_y - half_y + roofed_y / 2.0, z + C.CEILING_HEIGHT_CM - 30.0),
             (size_x, roofed_y, 20.0), concrete, prefix + "_Soffit")
        build_exposed_services(origin_x, origin_y, z, size_x, -half_y, SKYDECK_SCREEN_Y, materials, prefix)
    elif area_name == "WinterGarden":
        build_winter_garden_roof(origin_x, origin_y, z, materials, prefix)
    else:
        # The soffit is the underside of the floor above, so it has that
        # floor's openings too.
        soffit_holes = _slab_holes(floor_index + 1)
        if floor_index == 0:
            # The base is square; the tower's rounded corners start above it.
            soffit_holes = [hole for hole in soffit_holes if hole not in SOFT_CORNER_CUTS]
        _slab_with_holes(origin_x, origin_y, z + C.CEILING_HEIGHT_CM - 20.0, size_x, size_y,
                         soffit_holes, concrete, prefix + "_Soffit")
        if area_name == "Summit":
            build_exposed_services(origin_x, origin_y, z, size_x, -half_y, SKYDECK_SCREEN_Y, materials, prefix)
        elif area_name != "Ground_Plaza":
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
    build_lift_lobby(origin_x, origin_y, z, materials, prefix)
    build_wayfinding(floor_index, area_name, origin_x, origin_y, z, prefix)
    if area_name in ("Lobby", "Mezzanine", "OpenPlan", "Executive", "Summit", "Skydeck"):
        build_linear_lights(area_name, origin_x, origin_y, z, size_x, size_y, prefix)
    if area_name == "Ground_Plaza":
        build_ground_floor(origin_x, origin_y, z, half_x, half_y, materials, prefix)
    if floor_index == 1:
        build_escalator_balustrade(origin_x, origin_y, z, materials, prefix)
        build_reception_gallery(origin_x, origin_y, z, materials, prefix)
    if floor_index in (0, 1):
        build_ad_screens_in_place(origin_x, origin_y, z, floor_index, prefix)
    if area_name == "Summit":
        build_summit(origin_x, origin_y, z, materials, prefix)
    if area_name == "WinterGarden":
        build_winter_garden(origin_x, origin_y, z, materials, prefix)

    # --- Room partitions --------------------------------------------------
    rooms = ROOM_PROGRAMMES.get(area_name, [])
    for room_name, rx, ry, rsx, rsy in rooms:
        if (area_name, room_name) in OPEN_ROOMS:
            continue

        cx = origin_x + rx
        cy = origin_y + ry

        # Four walls with one doorway, on the side that faces the core: every
        # room opens onto the floor's loop, never onto the curtain wall.
        # Meeting rooms, the boardroom and the offices are glass boxes in black
        # frames, as on the real floors: you can see who is in them.
        glazed = room_name in GLAZED_ROOMS
        wall_mat = (materials.get("Glass") or plaster) if glazed else plaster
        wall_h = C.CEILING_HEIGHT_CM if "Server" in room_name or "Power" in room_name else (270.0 if glazed else 250.0)
        label = prefix + "_" + room_name
        x0, x1 = rx - rsx / 2.0, rx + rsx / 2.0
        y0, y1 = ry - rsy / 2.0, ry + rsy / 2.0
        faces_core_in_x = abs(rx) / max(1.0, half_x) > abs(ry) / max(1.0, half_y)
        door = C.DOOR_WIDTH_CM
        if faces_core_in_x:
            door_x = x1 if rx < 0 else x0
            far_x = x0 if rx < 0 else x1
            _wall(origin_x, origin_y, z, far_x, y0, far_x, y1, wall_mat, label + "_Back", wall_h, 15.0)
            _wall_with_door(origin_x, origin_y, z, door_x, y0, door_x, y1, ry, door, wall_mat, label + "_Door", wall_h)
            _wall(origin_x, origin_y, z, x0, y0, x1, y0, wall_mat, label + "_S", wall_h, 15.0)
            _wall(origin_x, origin_y, z, x0, y1, x1, y1, wall_mat, label + "_N", wall_h, 15.0)
        else:
            door_y = y1 if ry < 0 else y0
            far_y = y0 if ry < 0 else y1
            _wall(origin_x, origin_y, z, x0, far_y, x1, far_y, wall_mat, label + "_Back", wall_h, 15.0)
            _wall_with_door(origin_x, origin_y, z, x0, door_y, x1, door_y, rx, door, wall_mat, label + "_Door", wall_h)
            _wall(origin_x, origin_y, z, x0, y0, x0, y1, wall_mat, label + "_W", wall_h, 15.0)
            _wall(origin_x, origin_y, z, x1, y0, x1, y1, wall_mat, label + "_E", wall_h, 15.0)
        if glazed:
            _glazed_frame(origin_x, origin_y, z, x0, x1, y0, y1, wall_h, prefix + "_" + room_name)
            _meeting_table(origin_x, origin_y, z, rx, ry, rsx, rsy, materials, prefix + "_" + room_name)

    # --- Fit-out: cover that comes from what the room is for ------------------
    for room_name, rx, ry, rsx, rsy in rooms:
        x0, x1 = rx - rsx / 2.0, rx + rsx / 2.0
        y0, y1 = ry - rsy / 2.0, ry + rsy / 2.0
        if "Desk_Farm" in room_name or room_name == "Team_Desks":
            build_open_plan_desks(origin_x, origin_y, z, x0, x1, y0, y1, materials, prefix, room_name)
        elif room_name.startswith("Server_Hall"):
            build_server_racks(origin_x, origin_y, z, x0, x1, y0, y1, materials, prefix, room_name)
        elif room_name in ("Break_Out_Kitchen", "Breakout_Area", "Exec_Lounge"):
            build_breakout(origin_x, origin_y, z, x0, x1, y0, y1, materials, prefix, room_name,
                           kitchen=room_name == "Break_Out_Kitchen")

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


def build_lifts(report, materials=None):
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
            # Brushed steel car and doors, ballistic glass for the glass lift;
            # unset, every car rendered as the engine's grey checker.
            materials = materials or {}
            steel = materials.get("Metal")
            if steel is not None:
                lift.set_editor_property("CarMaterial", steel)
                lift.set_editor_property("DoorMaterial", steel)
            pane = materials.get("BallisticGlass") or materials.get("Glass")
            if pane is not None:
                lift.set_editor_property("GlassMaterial", pane)
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
            z = _floor_z(floor_index) + SPAWN_CENTRE_CM

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
    problems = _spawn_safety_problems(placed)

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


def _spawn_safety_problems(placed, only=None):
    """The two spawn distance rules, as readable lines (empty when the map is safe).

    Measured on the same storey (see build_spawn_points). With `only`, just the
    pairs that involve that anchor name - what a candidate move is checked against.
    """
    problems = []

    for i, a in enumerate(placed):
        for b in placed[i + 1:]:
            if only is not None and only not in (a["name"], b["name"]):
                continue
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

    return problems


def _spawn_blockers(world, x, y, z, radius=SPAWN_TEST_RADIUS_CM):
    """Labels of whatever blocks a pawn at a spawn anchor, the C++ way.

    Two capsules: the one ValidateClearance sweeps (lifted off the anchor by its
    half height), and the pawn's own body centred on the anchor, which is where
    the game mode puts it. A component counts when it blocks the Pawn channel -
    the test OverlapBlockingTestByChannel(ECC_Pawn) makes - so overlap-only
    triggers, volumes and the nav bounds never move a spawn.
    """
    # Every object type, not just the engine's six: the project's own object
    # channels (Glass, Penetrable, HelicopterBlocker, ProjectileBlocker) block the
    # Pawn channel too, and the C++ test sees them. Python names only the first
    # six members (the rest go by their channel names), so they are cast by value.
    object_types = []
    for value in range(32):
        try:
            object_types.append(unreal.ObjectTypeQuery.cast(value))
        except Exception:  # noqa: BLE001 - past the last object type
            break
    tests = (
        (z + SPAWN_TEST_LIFT_CM + SPAWN_TEST_HALF_HEIGHT_CM, SPAWN_TEST_HALF_HEIGHT_CM),
        (z, SPAWN_PAWN_HALF_HEIGHT_CM),
    )
    blockers = set()
    for centre_z, half_height in tests:
        found = unreal.SystemLibrary.capsule_overlap_components(
            world, unreal.Vector(x, y, centre_z), radius, max(half_height, radius),
            object_types, None, []) or []
        for component in found:
            try:
                if component.get_collision_enabled() in (unreal.CollisionEnabled.NO_COLLISION,
                                                         unreal.CollisionEnabled.PHYSICS_ONLY):
                    continue
                if component.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN) \
                        != unreal.CollisionResponseType.ECR_BLOCK:
                    continue
            except Exception:  # noqa: BLE001 - an unreadable component is assumed to block
                pass
            owner = component.get_owner()
            blockers.add(owner.get_actor_label() if owner is not None else component.get_name())
    return sorted(blockers)


def _spawn_has_floor(world, x, y, floor_z):
    """True when there is walkable floor under (x, y) at this storey.

    A clear capsule over the reception void or outside the curtain wall is
    clear because there is nothing there at all; the pawn would fall a floor.
    """
    hit = unreal.SystemLibrary.line_trace_single(
        world, unreal.Vector(x, y, floor_z + SPAWN_CENTRE_CM), unreal.Vector(x, y, floor_z - 40.0),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return False
    fields = hit.to_tuple()
    blocking, impact = fields[0], fields[5]
    return bool(blocking) and impact.z >= floor_z - 40.0


def settle_spawn_points(report, placed):
    """Moves any spawn anchor the clearance test would fail onto clear floor.

    Runs once every floor is built and attached, because the obstructions are
    floor content - kitchens, booths, desks, meeting pods - that the anchors in
    ANCHORS were placed before. An anchor that passes stays where it was
    authored; one that fails walks out round rings from its spot, nearest
    first, to the first point that is clear (with room to spare), has a floor
    under it and keeps both spawn distance rules. The manifest entry moves with
    the actor. An anchor with nowhere to go is reported by name - a packaged
    build would otherwise log it as obstructed at every BeginPlay.
    """
    if not placed:
        return 0

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors = {}
    for actor in _actor_subsystem().get_all_level_actors():
        if actor.get_class().get_name() == "OneBrahamSpawnPoint":
            actors[actor.get_actor_label()] = actor

    moved = 0
    for entry in placed:
        actor = actors.get(entry["name"])
        if actor is None:
            report.failed("spawn clearance " + entry["name"], "the actor is not in the persistent level")
            continue

        x, y, z = entry["location"]
        floor_z = z - SPAWN_CENTRE_CM
        blockers = _spawn_blockers(world, x, y, z)
        if not blockers and _spawn_has_floor(world, x, y, floor_z):
            continue

        found = None
        for ring in SPAWN_SEARCH_RINGS_CM:
            for step in range(SPAWN_SEARCH_STEPS):
                angle = 2.0 * math.pi * step / SPAWN_SEARCH_STEPS
                cx, cy = x + ring * math.cos(angle), y + ring * math.sin(angle)
                if _spawn_blockers(world, cx, cy, z, SPAWN_SEARCH_RADIUS_CM):
                    continue
                if not _spawn_has_floor(world, cx, cy, floor_z):
                    continue
                entry["location"] = [cx, cy, z]
                if _spawn_safety_problems(placed, only=entry["name"]):
                    entry["location"] = [x, y, z]
                    continue
                found = (cx, cy)
                break
            if found:
                break

        if found is None:
            report.failed("spawn clearance " + entry["name"],
                          u"obstructed by {0} and no clear floor within {1:.0f} cm".format(
                              ", ".join(blockers) or "nothing (no floor)", SPAWN_SEARCH_RINGS_CM[-1]))
            continue

        # Modify() first: a plain location set does not dirty the level, and a
        # move the save never sees is a move the packaged map never gets.
        actor.modify()
        actor.set_actor_location(unreal.Vector(found[0], found[1], z), False, False)
        moved += 1
        C.log(u"    {0} (F{1:02d}) moved {2:.0f} cm to clear floor, off {3}".format(
            entry["name"], entry["floor"], math.hypot(found[0] - x, found[1] - y),
            ", ".join(blockers) or "a missing floor"))

    if moved:
        try:
            if not _level_subsystem().save_all_dirty_levels():
                report.failed("spawn clearance", u"could not save the persistent level")
        except Exception as exc:  # noqa: BLE001
            report.failed("spawn clearance", u"could not save the persistent level: {0}".format(exc))
    C.log(u"  spawn clearance: {0} of {1} anchors moved to clear floor".format(moved, len(placed)))
    return moved


# Points sampled per floor plate (a NAV_SAMPLE_GRID x NAV_SAMPLE_GRID grid) to
# prove the baked nav mesh covers it, and the share that must land on it. Cores,
# shafts, atrium voids and furniture eat some samples; a floor under half
# covered is a floor the bots cannot work.
NAV_SAMPLE_GRID = 5
NAV_MIN_COVERAGE = 0.5


def _nav_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def _on_navmesh(world, point, extent):
    """The nav mesh point within extent of point, or None."""
    hit = unreal.NavigationSystemV1.project_point_to_navigation(world, point, None, None, extent)
    if isinstance(hit, tuple):
        hit = hit[1] if hit and hit[0] else None
    return hit


def build_navigation(report, level_path, floor_indices=None, points=None):
    """Bakes the nav mesh for level_path (and every level it streams in) and saves it.

    Runs last, after everything that moves geometry - the district, the kitchen
    kit, spawn settling - because a nav mesh built before any of it is stale on
    load, and a map that never had one was what bots stood still on: the build
    spawned NavMeshBoundsVolumes and never built inside them.

    The project generates Dynamic (see [/Script/NavigationSystem.RecastNavMesh]
    in DefaultEngine.ini): this bake is what the game loads, and only tiles a
    door, shutter, breach or broken pane dirties are rebuilt at runtime.

    floor_indices, when given, are checked by projecting a grid on each floor
    plate onto the result; a floor that comes back mostly bare is a failure.
    points, when given, are (name, x, y, z) spots that must each be on it.
    """
    level_subsystem = _level_subsystem()

    # An editor world holds navigation building until asset compilation has
    # settled - a ticker that never fires in a commandlet, so a headless build
    # would log "navigation build is locked" and bake nothing. Off for the
    # reload this bake opens; restored after so the editor itself is unchanged.
    nav_cdo = unreal.get_default_object(unreal.NavigationSystemV1)
    wait_flag = "bWaitForAsyncLoadingBeforeBuildingNavigationAutomatically"
    waited = nav_cdo.get_editor_property(wait_flag)
    nav_cdo.set_editor_property(wait_flag, False)
    try:
        if not level_subsystem.load_level(level_path):
            report.failed(level_path, u"could not reopen to build navigation")
            return 0
    finally:
        nav_cdo.set_editor_property(wait_flag, waited)

    world = _nav_world()
    if unreal.NavigationSystemV1.get_navigation_system(world) is None:
        report.failed(level_path, u"has no navigation system to build with")
        return 0

    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")

    navmeshes = [actor for actor in _actor_subsystem().get_all_level_actors()
                 if actor.get_class().get_name() == "RecastNavMesh"]
    if not navmeshes:
        report.failed(level_path, u"navigation build produced no RecastNavMesh")
        return 0
    for navmesh in navmeshes:
        if navmesh.get_editor_property("RuntimeGeneration") != unreal.RuntimeGenerationType.DYNAMIC:
            report.failed(level_path, u"{0} is not Dynamic - check RuntimeGeneration in DefaultEngine.ini".format(
                navmesh.get_actor_label()))
        # The tiles live on this actor; Modify() is what puts its package in the save.
        navmesh.modify()

    covered = 0
    for floor_index in floor_indices or []:
        area_name, origin_x, origin_y, size_x, size_y = _area_for_floor(floor_index)
        if area_name is None:
            continue
        z = _floor_z(floor_index)
        hits = 0
        for i in range(NAV_SAMPLE_GRID):
            for j in range(NAV_SAMPLE_GRID):
                x = origin_x + size_x * ((i + 0.5) / NAV_SAMPLE_GRID - 0.5)
                y = origin_y + size_y * ((j + 0.5) / NAV_SAMPLE_GRID - 0.5)
                # A small box: the nearest polygon has to be on this floor plate,
                # near this sample, not the floor below seen down a stairwell.
                hit = _on_navmesh(world, unreal.Vector(x, y, z + 50.0), unreal.Vector(250.0, 250.0, 100.0))
                if hit is not None and abs(hit.z - z) < 120.0:
                    hits += 1
        share = hits / float(NAV_SAMPLE_GRID * NAV_SAMPLE_GRID)
        if share < NAV_MIN_COVERAGE:
            report.failed(u"navigation F{0:02d}".format(floor_index),
                          u"only {0:.0%} of the {1} floor plate is on the nav mesh".format(share, area_name))
        else:
            covered += 1
        C.log(u"    nav F{0:02d} {1}: {2:.0%} of samples on the nav mesh".format(floor_index, area_name, share))

    # Named spots a bot must be able to stand on (the arena's control points).
    for name, x, y, z in points or []:
        hit = _on_navmesh(world, unreal.Vector(x, y, z + 50.0), unreal.Vector(100.0, 100.0, 100.0))
        if hit is None or abs(hit.z - z) >= 120.0:
            report.failed(u"navigation {0}".format(name), u"is not on the nav mesh")

    try:
        if not level_subsystem.save_all_dirty_levels():
            report.failed(level_path, u"could not save after building navigation")
    except Exception as exc:  # noqa: BLE001
        report.failed(level_path, u"could not save after building navigation: {0}".format(exc))
    C.log(u"  navigation: built and saved for {0} ({1} floors covered)".format(level_path, covered))
    return covered


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

                    # The same placement on the door's zone component. The actor
                    # copies its values across in its own BeginPlay, but the
                    # component's BeginPlay runs first and checks its id then: a
                    # door stamped only on the actor logged "has no ZoneId" in
                    # every packaged boot. Stamped here, the id is in the map.
                    component = zone.get_editor_property("Zone")
                    if component is None:
                        report.failed("access zone " + zone_id, "the door has no zone component")
                    else:
                        component.set_editor_property("ZoneId", unreal.Name(zone_id))
                        component.set_editor_property("RequiredTier",
                                                      getattr(unreal.OneBrahamAccessTier, tier.upper()))
                        component.set_editor_property("FloorIndex", floor)
                        component.set_editor_property("bBreachable", breachable)
                        component.set_editor_property("bEscalationOpens", escalation)
                        component.set_editor_property("PairedZoneId", unreal.Name(paired or ""))
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
                    # Galvanised steel: the class has no material of its own,
                    # and without one every stair door drew the engine checker.
                    steel = unreal.EditorAssetLibrary.load_asset(C.MATERIALS_INSTANCES + "/MI_Metal")
                    if steel is not None:
                        for part in shutter.get_components_by_class(unreal.StaticMeshComponent):
                            part.set_material(0, steel)
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
                    unreal.Color(r=255, g=160, b=40, a=255))
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
            # No panels over open sky (the crown's balcony cut-outs, the roof
            # terrace) or hanging in a void (the double-height reception, the
            # atrium): the floor above's fittings light those from on high.
            px, py = lx - origin_x, ly - origin_y
            if floor_index >= 16 and any(_in_rect(px, py, r) for r in CROWN_OUT):
                continue
            if floor_index == 17 and _in_rect(px, py, TERRACE_17):
                continue
            if (floor_index == 0 and _in_rect(px, py, RECEPTION_VOID)) or \
                    (floor_index == 16 and _in_rect(px, py, ATRIUM_VOID)):
                continue
            bounce_wanted = not ((floor_index == 1 and _in_rect(px, py, RECEPTION_VOID)) or
                                 (floor_index == 17 and _in_rect(px, py, ATRIUM_VOID)))

            light = _spawn(unreal.RectLight, unreal.Vector(lx, ly, ceiling_z),
                           unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0))  # facing the floor
            if light is None:
                continue

            try:
                component = light.rect_light_component
                # Lumens, sized for the grid: one fitting per ~36 m2 has to put
                # 200-300 lux on the desks (an office's real level, and EV100
                # 6.5 under the floor's exposure), and reach the next fitting's
                # pool so the floor reads as lit, not as spotlights in a void.
                component.set_editor_property("IntensityUnits", unreal.LightUnits.LUMENS)
                component.set_editor_property("Intensity", 16000.0)
                # 8 m: past the next fittings on a 6 m grid, but not through
                # three or four floors each way as 16 m did. That put 60-120
                # shadowed lights on a desk-height pixel against the engine's
                # 32 (one-pass projection overflow, missing shadows); at 8 m it
                # is about 14. Every fitting keeps its shadow: an unshadowed
                # one would light the floor below through the slab.
                component.set_editor_property("AttenuationRadius", 800.0)
                component.set_editor_property("SourceWidth", 120.0)
                component.set_editor_property("SourceHeight", 60.0)
                # 4000 K: the colour of a building's own lighting, everywhere.
                component.set_editor_property("LightColor", unreal.Color(r=255, g=246, b=232, a=232))
                # Movable: the project disallows static lighting, so a static
                # light is never baked and Lumen gives it no bounce - pure black
                # shadow beside a white-hot wall.
                component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
                # Every floor is loaded at once: fittings more than ~25 m away
                # (most of them on other floors) fade out instead of costing.
                component.set_editor_property("MaxDrawDistance", 2500.0)
                component.set_editor_property("MaxDistanceFadeRange", 800.0)
            except Exception as exc:  # noqa: BLE001
                report.failed("ceiling light", str(exc))

            _label(light, "Ceiling_Light_{0}_F{1:02d}_{2}".format(area_name, floor_index, spawn_count))

            # The fitting's bounce: what the floor under it throws back onto the
            # walls and ceiling. Lumen gets almost nothing back from the scaled
            # blockout cubes, which left every wall that didn't face a fitting
            # pitch black - unreadable, and a free hiding place. Shadowless and
            # diffuse-only, so it costs little and never adds a second hotspot.
            bounce = _spawn(unreal.PointLight, unreal.Vector(lx, ly, z + 90.0)) if bounce_wanted else None
            if bounce is not None:
                try:
                    component = bounce.point_light_component
                    component.set_editor_property("IntensityUnits", unreal.LightUnits.LUMENS)
                    component.set_editor_property("Intensity", 2400.0)
                    component.set_editor_property("AttenuationRadius", 1100.0)
                    component.set_editor_property("SourceRadius", 200.0)
                    component.set_editor_property("CastShadows", False)
                    component.set_editor_property("SpecularScale", 0.0)
                    component.set_editor_property("MaxDrawDistance", 3000.0)
                    component.set_editor_property("MaxDistanceFadeRange", 800.0)
                    component.set_editor_property("LightColor", unreal.Color(r=220, g=226, b=232, a=255))
                    component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
                except Exception as exc:  # noqa: BLE001
                    report.failed("bounce light", str(exc))
                _label(bounce, "Bounce_Light_{0}_F{1:02d}_{2}".format(area_name, floor_index, spawn_count))
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

    # Bounded to its own floor. Every floor is loaded at once, so sixteen
    # unbound volumes at one priority would fight over the whole building.
    # The brush is a 200 cm cube; it is scaled to the slab plus a margin past
    # the facade. The ground floor reaches out over the street the players
    # start on, and the top floor up over the open balcony.
    margin = 600.0
    extent_x, extent_y = size_x + margin, size_y + margin
    height = C.FLOOR_HEIGHT_CM
    if floor_index == 0:
        extent_x = extent_y = 30000.0
    if floor_index == C.FLOOR_COUNT:
        height = C.FLOOR_HEIGHT_CM * 6.0
    try:
        volume.set_actor_location(unreal.Vector(origin_x, origin_y, _floor_z(floor_index) + height / 2.0), False, False)
        volume.set_actor_scale3d(unreal.Vector(extent_x / 200.0, extent_y / 200.0, height / 200.0))
        volume.set_editor_property("bUnbound", False)
        volume.set_editor_property("BlendRadius", 150.0)
        volume.set_editor_property("BlendWeight", 1.0)
        volume.set_editor_property("Priority", 1.0)
    except Exception as exc:  # noqa: BLE001
        report.failed("post-process bounds", str(exc))

    try:
        settings = volume.settings

        # Every value needs its bOverride_ flag, or the volume silently keeps the
        # engine default - which is how this grade never applied before.
        def grade(name, value):
            settings.set_editor_property("bOverride_" + name, True)
            settings.set_editor_property(name, value)

        # Exposure: histogram auto-exposure clamped to an indoor range (EV100),
        # brighter on the open balcony, so a bright canteen does not blow out
        # and a dark plant room does not glow. The area's grade still biases it.
        outdoor = area_name in ("Skydeck", "WinterGarden")
        grade("AutoExposureMethod", unreal.AutoExposureMethod.AEM_HISTOGRAM)
        # Floor 15 is both an indoor canteen and an open balcony: a wide range
        # lets the eye adapt from one to the other through the gate.
        grade("AutoExposureMinBrightness", 6.0 if outdoor else 5.5)
        grade("AutoExposureMaxBrightness", 12.0 if outdoor else 9.5)
        grade("AutoExposureBias", float(max(-1.0, min(1.0, (min_brightness - 0.5) * 1.5))))
        grade("AutoExposureSpeedUp", 2.5)
        grade("AutoExposureSpeedDown", 1.2)

        # Grade: restrained. One Braham is a corporate building, not a film -
        # but a building at dusk with the lights on, seen by someone scared.
        grade("BloomIntensity", float(min(bloom, 0.6)))
        grade("BloomThreshold", 1.2)
        grade("FilmGrainIntensity", float(min(grain * 6.0, 0.18)))
        grade("VignetteIntensity", float(min(vignette, 0.35)))
        grade("LensFlareIntensity", 0.0)
        grade("MotionBlurAmount", 0.25)
        grade("SceneFringeIntensity", 0.15)
    except Exception as exc:  # noqa: BLE001
        report.failed("post-process grade", str(exc))

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

    The contrast is the point. Fourteen floors of sealed, over-lit corporate space
    and then one floor where the weather is real and the wind takes your voice
    away - which is why proximity voice is quieter up here and why the extraction
    announcement is the loudest cue in the game.
    """
    deck_z = _floor_z(15)

    # The photographed sky, and the sun aimed at the sun in it. Without the
    # fetched sky (SourceArt/), the procedural atmosphere and a fixed sun stand.
    from onebraham import build_district
    sky = C.guard(report, "sky", build_district.build_sky, report)

    # The dome is a 10 km sphere round the whole map, so any collision on it
    # encloses every player: each spawn's clearance test hit it. Turning
    # collision off is not enough - a BlockAll profile reapplies itself when the
    # map loads - so the profile itself is NoCollision.
    dome = sky.get("dome") if sky else None
    if dome is not None:
        try:
            dome.static_mesh_component.set_collision_profile_name("NoCollision")
        except Exception as exc:  # noqa: BLE001
            report.failed("Sky_Dome collision", str(exc))

    # The procedural atmosphere only stands in when there is no photograph: under
    # the photographed dome its aerial perspective darkened the horizon into a
    # navy band. The height fog carries the haze either way.
    if not sky:
        atmosphere_actor = _spawn(unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, deck_z))
        if atmosphere_actor is None:
            report.failed("SkyAtmosphere", "could not spawn")
        else:
            _label(atmosphere_actor, "Sky_OneBraham")
            # Past the mapped streets the planet's surface shows: city grey under
            # the haze, not the default near-black that read as a sea.
            try:
                atmosphere = atmosphere_actor.get_component_by_class(unreal.SkyAtmosphereComponent)
                atmosphere.set_editor_property("GroundAlbedo", unreal.Color(r=110, g=108, b=104, a=255))
            except Exception as exc:  # noqa: BLE001
                report.failed("sky ground albedo", str(exc))

    sun_pitch, sun_yaw = (sky["pitch"], sky["yaw"]) if sky else (-38.0, 145.0)
    sun = _spawn(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, deck_z + 2000.0),
                 unreal.Rotator(roll=0.0, pitch=sun_pitch, yaw=sun_yaw))  # Rotator() is (roll, pitch, yaw)
    if sun is not None:
        _label(sun, "Sun_London_Overcast")
        try:
            component = sun.light_component
            # An overcast London afternoon: wide, soft, low contrast. Sunlight
            # would make the office floors look like a Mediterranean resort.
            # Lux. 8,000 is overcast daylight; the 2.2 this used to be is
            # moonlight, which under EV100 exposure left the lobby and the
            # Skydeck black.
            component.set_editor_property("Intensity", 8000.0)
            colour = sky["colour"] if sky else (214, 222, 232)
            component.set_editor_property("LightColor", unreal.Color(r=colour[0], g=colour[1], b=colour[2], a=255))
            component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
        except Exception as exc:  # noqa: BLE001
            # This used to pass silently on a wrong accessor name and leave the
            # sun at the engine's default 10 lux: a building in permanent dusk.
            report.failed("sun", str(exc))

    # London haze from street level: pale, thin, starting a few streets out so
    # rooms and the plaza stay crisp and the City softens on the horizon.
    fog = _spawn(unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, _floor_z(0)))
    if fog is not None:
        _label(fog, "Fog_London")
        try:
            component = fog.component
            component.set_editor_property("FogDensity", 0.0035)
            component.set_editor_property("FogHeightFalloff", 0.04)
            component.set_editor_property("StartDistance", 4000.0)
            # Physical units: the haze is as bright as the sky at the horizon
            # (the dome's HazeColour x its 2500 luminance scale). The 0.55 this
            # used to be is near black at daylight exposure, and darkened the
            # distance instead of hazing it.
            component.set_editor_property("FogInscatteringLuminance", unreal.LinearColor(1900.0, 2050.0, 2250.0, 1.0))
        except Exception as exc:  # noqa: BLE001
            report.failed("fog", str(exc))

    skylight = _spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, deck_z + 400.0))
    if skylight is not None:
        _label(skylight, "Skylight_OneBraham")
        try:
            skylight.light_component.set_editor_property("Intensity", 0.8)
            skylight.light_component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
            # Capture the SkyAtmosphere live, so the sky's light matches the sky.
            skylight.light_component.set_editor_property("bRealTimeCapture", True)
        except Exception as exc:  # noqa: BLE001
            report.failed("skylight", str(exc))

    # The Purge's dusk: AOneBrahamSkyDirector blends these from the day they
    # are placed at to its DuskState. Wired explicitly so it never picks up
    # another floor's light; anything left None it finds in the world itself.
    # No replication of its own (it watches the replicated lockdown stage).
    director_class = getattr(unreal, "OneBrahamSkyDirector", None)
    director = _spawn(director_class, unreal.Vector(0.0, 0.0, deck_z + 600.0)) if director_class else None
    if director is None:
        report.failed("Sky_Director", "could not spawn OneBrahamSkyDirector - build the editor target")
    else:
        _label(director, "Sky_Director")
        try:
            for prop, actor in (("Sun", sun), ("SkyLight", skylight), ("HeightFog", fog),
                                ("SkyDome", sky.get("dome") if sky else None)):
                if actor is not None:
                    director.set_editor_property(prop, actor)
            if sky and sky.get("dusk_cube") is not None:
                director.set_editor_property("DuskSkyCube", sky["dusk_cube"])
        except Exception as exc:  # noqa: BLE001
            report.failed("Sky_Director", str(exc))

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
            # No exposure here: this project measures exposure in EV100
            # (ExtendDefaultLuminanceRange), and the 0.65-1.6 this used to set is
            # a pitch-dark range that pushed every lit floor six stops over. Each
            # floor's own volume owns exposure, and this one sits beneath them.
            ppv.set_editor_property("Priority", -1.0)
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

    # An existing floor is reopened and cleared, then rebuilt like a new one.
    # It used to be returned untouched as "updating", so every change to the
    # shell, lights or grade after a floor's first build silently never landed.
    existed = unreal.EditorAssetLibrary.does_asset_exist(level_path)
    if existed:
        if not level_subsystem.load_level(level_path):
            report.failed(level_path, u"could not open the existing streaming level")
            return None
        actors = _actor_subsystem()
        generated = _generated_actors(actors.get_all_level_actors())
        actors.destroy_actors(generated)
        C.log(u"  floor {0:02d}: reopened; {1} generated actors cleared".format(floor_index, len(generated)))
    else:
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

    if existed:
        report.updated_asset(level_path)
    else:
        report.created_asset(level_path)
    return level_path


def link_floor_levels(report, level_paths):
    """Attaches every floor level to the persistent level, always loaded.

    Without this the persistent map - the one the game opens - had the
    gameplay actors and the facade but none of the floors: no slabs, rooms,
    lights or grade were ever loaded in play. Sixteen small floors fit in memory
    together, and a building whose floors can be seen through stairwells and
    the atrium should not pop in, so they are always loaded rather than
    streamed by distance.
    """
    level_subsystem = _level_subsystem()
    persistent_path = "{0}/{1}".format(C.MAPS, C.MAP_PERSISTENT)
    if not level_subsystem.load_level(persistent_path):
        report.failed(persistent_path, u"could not reopen to attach the floors")
        return 0

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    present = set()
    for level in unreal.EditorLevelUtils.get_levels(world):
        present.add(level.get_path_name().split(":")[0].split(".")[0])

    added = 0
    for path in level_paths:
        if path in present:
            continue
        streaming = unreal.EditorLevelUtils.add_level_to_world(world, path, unreal.LevelStreamingAlwaysLoaded)
        if streaming is None:
            report.failed(path, u"could not attach to the persistent level")
            continue
        added += 1

    # Adding a level makes it the current one, so save_current_level would
    # save the last floor and drop the new streaming references.
    try:
        if not level_subsystem.save_all_dirty_levels():
            report.failed(persistent_path, u"could not save after attaching floors")
    except Exception as exc:  # noqa: BLE001
        report.failed(persistent_path, u"could not save after attaching floors: {0}".format(exc))
    C.log(u"  persistent level: {0} floors attached ({1} already present)".format(added, len(level_paths) - added))
    return added


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
        generated = _generated_actors(actors.get_all_level_actors())
        actors.destroy_actors(generated)
        C.log(u"  persistent level reopened; {0} generated actors cleared".format(len(generated)))
        report.updated_asset(persistent_path)
    else:
        if not level_subsystem.new_level(persistent_path):
            report.failed(persistent_path, u"could not create the persistent level")
            return False
        report.created_asset(persistent_path)
    return True


def build_facade(materials):
    """The curtain wall and its fins, stepped like the real crown.

    One Braham is glass on every face, with dense vertical aluminium fins on
    the east, south and west elevations (our +X, +Y and -X; the entrance face,
    -Y onto Braham Street, has none). The tower runs full-plate to the
    floor-15 slab; above it the crown - floors 16 and 17 - stands over the
    canteen half and the core stalk, the Skydeck balcony round it behind glass
    balustrades, and the roof terrace on 17 steps back again.

    The entrance face starts at floor 1: the ground floor's glazed base and its
    revolving doors are floor 0's own.
    """
    glass = materials.get("BallisticGlass") or materials.get("Glass")
    fin = _district_material("Aluminium") or materials.get("Metal")
    rail = materials.get("Glass") or glass
    fx, fy = 2215.0, 1815.0
    z_ground, z1 = _floor_z(0), _floor_z(1)
    z15, z16, z17, z_roof = _floor_z(15), _floor_z(16), _floor_z(17), _floor_z(18)
    sx, ny = CROWN_STALK_X + 15.0, CROWN_NORTH_Y + 15.0
    screen = SKYDECK_SCREEN_Y
    terrace_x = TERRACE_17[0]

    def pane(label, x0, y0, x1, y1, za, zb, material=glass):
        if zb - za < 1.0:
            return
        if abs(x1 - x0) >= abs(y1 - y0):
            size = (abs(x1 - x0) + 30.0, 30.0, zb - za)
        else:
            size = (30.0, abs(y1 - y0) + 30.0, zb - za)
        _box(((x0 + x1) / 2.0, (y0 + y1) / 2.0, (za + zb) / 2.0), size, material, "Facade_" + label)

    def balustrade(label, x0, y0, x1, y1, z):
        pane(label, x0, y0, x1, y1, z, z + 110.0, rail)

    def fins(label, x0, y0, x1, y1, za, zb, out):
        """Fins every metre along a face, standing `out` cm proud of it."""
        length = math.hypot(x1 - x0, y1 - y0)
        count = max(1, int(length // 100.0))
        ux, uy = (x1 - x0) / length, (y1 - y0) / length
        nx, ny_ = -uy * out, ux * out
        # Always outward: away from the tower's centre.
        if (x0 + x1) * nx + (y0 + y1) * ny_ < 0.0:
            nx, ny_ = -nx, -ny_
        for i in range(count + 1):
            t = i * length / count
            px, py = x0 + ux * t + nx, y0 + uy * t + ny_
            size = (6.0, 38.0, zb - za) if abs(ux) > abs(uy) else (38.0, 6.0, zb - za)
            _box((px, py, (za + zb) / 2.0), size, fin, "Facade_Fin_{0}_{1:03d}".format(label, i))

    r = SOFT_CORNER_R

    def arc(label, cx_, cy_, a0, za, zb, with_fins):
        """A quarter-round glass corner of six segments, centre (cx_, cy_),
        from angle a0 for ninety degrees, with fins wrapping it."""
        segments = 6
        chord = 2.0 * r * math.sin(math.radians(90.0 / segments / 2.0))
        for i in range(segments):
            mid = math.radians(a0 + (i + 0.5) * 90.0 / segments)
            piece = _box((cx_ + r * math.cos(mid), cy_ + r * math.sin(mid), (za + zb) / 2.0),
                         (chord + 4.0, 30.0, zb - za), glass, "Facade_Corner_{0}_{1}".format(label, i + 1))
            if piece is not None:
                piece.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=math.degrees(mid) + 90.0), False)
            if with_fins and i % 2 == 0:
                out = r + 22.0
                blade = _box((cx_ + out * math.cos(mid), cy_ + out * math.sin(mid), (za + zb) / 2.0),
                             (38.0, 6.0, zb - za), fin, "Facade_Fin_Corner_{0}_{1}".format(label, i + 1))
                if blade is not None:
                    blade.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=math.degrees(mid)), False)

    # --- The ground floor: the square glazed base ---------------------------
    pane("North_Base", -fx, fy, fx, fy, z_ground, z1)
    pane("West_Base", -fx, -fy, -fx, fy, z_ground, z1)
    pane("East_Base", fx, -fy, fx, fy, z_ground, z1)
    fins("North_Base", fx, fy, -fx, fy, z_ground, z1, 22.0)
    fins("West_Base", -fx, fy, -fx, -fy, z_ground, z1, 22.0)
    fins("East_Base", fx, -fy, fx, fy, z_ground, z1, 22.0)

    # --- The tower, floor 1 to the floor-15 slab, corners rounded -----------
    pane("South", -fx + r, -fy, fx - r, -fy, z1, z15)
    pane("North", -fx + r, fy, fx - r, fy, z1, z15)
    pane("West", -fx, -fy + r, -fx, fy - r, z1, z15)
    pane("East", fx, -fy + r, fx, fy - r, z1, z15)
    fins("North", fx - r, fy, -fx + r, fy, z1, z15, 22.0)
    fins("West", -fx, fy - r, -fx, -fy + r, z1, z15, 22.0)
    fins("East", fx, -fy + r, fx, fy - r, z1, z15, 22.0)
    arc("NE", fx - r, fy - r, 0.0, z1, z15, True)
    arc("NW", -fx + r, fy - r, 90.0, z1, z15, True)
    # The south corners carry on up past the canteen into the crown.
    arc("SW", -fx + r, -fy + r, 180.0, z1, z_roof, True)
    arc("SE", fx - r, -fy + r, 270.0, z1, z17, True)

    # --- Floor 15: the canteen glazed, the balcony behind balustrades -------
    pane("Canteen_South", -fx + r, -fy, fx - r, -fy, z15, z16)
    pane("Canteen_West", -fx, -fy + r, -fx, screen, z15, z16)
    pane("Canteen_East", fx, -fy + r, fx, screen, z15, z16)
    balustrade("Skydeck_Rail_West", -fx, screen, -fx, fy, z15)
    balustrade("Skydeck_Rail_East", fx, screen, fx, fy, z15)

    # --- The crown: 16 and 17 over the canteen block and the core stalk -----
    pane("Crown_South_West", -fx + r, -fy, terrace_x, -fy, z16, z_roof)
    pane("Crown_South_East", terrace_x, -fy, fx - r, -fy, z16, z17)
    pane("Crown_West", -fx, -fy + r, -fx, screen, z16, z_roof)
    pane("Crown_East", fx, -fy + r, fx, screen, z16, z17)
    pane("Crown_North_West", -fx, screen, -sx, screen, z16, z_roof)
    pane("Crown_North_East", sx, screen, fx, screen, z16, z17)
    pane("Stalk_West", -sx, screen, -sx, ny, z16, z_roof)
    pane("Stalk_East", sx, screen, sx, ny, z16, z_roof)
    pane("Stalk_North", -sx, ny, sx, ny, z16, z_roof)
    balustrade("Terrace_Rail_South", terrace_x, -fy, fx - r, -fy, z17)
    balustrade("Terrace_Rail_East", fx, -fy + r, fx, screen, z17)
    arc("SE_TerraceRail", fx - r, -fy + r, 270.0, z17, z17 + 110.0, False)
    fins("Crown_West", -fx, screen, -fx, -fy + r, z15, z_roof, 22.0)
    fins("Crown_East", fx, -fy + r, fx, screen, z15, z17, 22.0)
    fins("Stalk_West", -sx, ny, -sx, screen, z16, z_roof, 22.0)
    fins("Stalk_East", sx, screen, sx, ny, z16, z_roof, 22.0)
    fins("Stalk_North", sx, ny, -sx, ny, z16, z_roof, 22.0)
    _note_surface("BallisticGlass" if materials.get("BallisticGlass") is not None else "Glass", "Facade")


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
    # (There was a 4 m solid "exterior slab" here, z -420 to -20, from when the
    # ground floor stood at z 0. It enclosed the whole of floor 0 - the lobby
    # was inside a block. The district's paving is the ground now.)

    # --- Ground: the plaza and Aldgate ------------------------------------
    # Asphalt, kerbs, a road with markings, and the low buildings opposite:
    # the outside of One Braham has to look like Aldgate, because the first thing
    # the player does is walk in the front door.
    # The real streets, from OpenStreetMap: see build_district. A flat apron
    # stands in only if the district could not be built.
    from onebraham import build_district
    district = C.guard(report, "district", build_district.build_district, report)
    if district:
        manifest["district"] = district
    else:
        _box((0.0, 0.0, -410.0), (26000.0, 26000.0, 20.0), materials.get("Concrete"), "Aldgate_Apron")

    # The curtain wall comes after the district, whose build authors the
    # aluminium the fins are made of.
    build_facade(materials)

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
    manifest["lifts"] = C.guard(report, "lifts", build_lifts, report, materials) or []
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
    C.guard(report, "floor streaming", link_floor_levels, report, [f["level"] for f in floors])

    # With every floor attached, the spawn anchors can be tested against what
    # the floors actually put round them.
    C.guard(report, "spawn clearance", settle_spawn_points, report, manifest["spawn_anchors"])

    # Last of all on the tower: nothing after this may move its geometry.
    C.guard(report, "navigation", build_navigation, report, "{0}/{1}".format(C.MAPS, C.MAP_PERSISTENT),
            [f["index"] for f in floors])

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

    # Reopened and cleared when it exists, as the tower floors are: returning
    # it untouched meant no change to the arena ever reached it.
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if not _level_subsystem().load_level(path):
            report.failed(path, u"could not open the existing arena")
            return None
        actors = _actor_subsystem()
        actors.destroy_actors(_generated_actors(actors.get_all_level_actors()))
    else:
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
    # The roof runs over all three points: its underside at 280 leaves 240 cm
    # over a 40 cm platform, room for a standing player (176 cm) and the nav
    # agent. At 190 neither could stand on a point, and its top sat low enough
    # under the ceiling to grow an unreachable strip of nav mesh.
    _box((0.0, 0.0, 290.0), (600.0, 7000.0, 20.0), concrete, "Corridor_Roof")

    # Team spawn rooms at each end.
    for team, sy in (("Staff", -3200.0), ("Contractors", 3200.0)):
        _box((0.0, sy, 60.0), (3600.0, 500.0, 120.0), carpet, "SpawnRoom_{0}".format(team))

    # Lighting: same ceiling grid language as the tower, brighter, colder.
    for ix in range(-2, 3):
        for iy in range(-3, 4):
            light = _spawn(unreal.RectLight, unreal.Vector(ix * 1000.0, iy * 1000.0, 380.0),
                           unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0))  # facing the floor
            if light is None:
                continue
            try:
                component = light.rect_light_component
                # One fitting per 100 m2, so roughly twice the tower's lumens.
                component.set_editor_property("IntensityUnits", unreal.LightUnits.LUMENS)
                component.set_editor_property("Intensity", 30000.0)
                component.set_editor_property("AttenuationRadius", 2000.0)
                component.set_editor_property("SourceWidth", 120.0)
                component.set_editor_property("SourceHeight", 80.0)
                component.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
            except Exception:  # noqa: BLE001
                pass

    # The bounds brush is a 200 cm cube: scaled to the whole floor, from under
    # the slab to the ceiling's underside (not its top, or the roof gets a nav
    # mesh). Unscaled it hung in mid-air over B and covered nothing.
    bounds = _spawn(unreal.NavMeshBoundsVolume, unreal.Vector(0.0, 0.0, 175.0))
    if bounds is not None:
        _label(bounds, "NavBounds_Arena")
        bounds.set_actor_scale3d(unreal.Vector(2500.0 / 100.0, 3500.0 / 100.0, 225.0 / 100.0))
    _spawn(unreal.SkyLight, unreal.Vector(0.0, 0.0, 600.0))

    try:
        _level_subsystem().save_current_level()
    except Exception as exc:  # noqa: BLE001
        report.failed(path, u"could not save: {0}".format(exc))
        return None
    build_navigation(report, path, points=[(u"ControlPoint_" + name, cx, cy, 40.0)
                                           for name, cx, cy in points])

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
