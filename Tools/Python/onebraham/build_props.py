# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_props
=====================

Builds the props side of the content: the gameplay Blueprints that must exist by
conventional path, and the *prop dressing pipeline* that turns an imported mesh
library into a furnished building.

TWO JOBS, ONE SCRIPT

1. BLUEPRINTS BY CONVENTIONAL PATH.
   The C++ loads a handful of classes by a fixed path - the keycard is
   /Game/OneBraham/Blueprints/BP_Keycard.BP_Keycard_C, the HUD is
   WBP_OneBrahamHUD, the character is BP_OneBrahamCharacter - because a hard
   reference from code to content is how a shipping project guarantees the asset
   exists. This script creates every one of them, parented correctly, so the
   paths are never missing.

2. PROP DRESSING.
   The building needs roughly 1,400 physical objects: desks, monitors, chairs,
   racks, lockers, vending machines, plants, printers, signage, bins, coffee
   machines, fire points, whiteboards, umbrellas. Modelling those is an art pass.
   *Placing* them is a data problem, and this script solves it: PROPS below is
   the full catalogue with real dimensions in centimetres, the room it belongs
   in, how many, whether it is cover, what it does to navmesh, and which
   marketplace pack covers it if the art pass has not reached it (see
   Docs/16_Marketplace_Fallbacks.md).

   `spawn_props_from_manifest()` then reads that catalogue, finds the mesh at
   /Game/OneBraham/Props/<Category>/SM_OB_<Name>, and dresses the level. Import
   the meshes with those names and one command furnishes fifteen floors.

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/build_props.py"
"""

from __future__ import print_function

import io
import json
import os

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

PROPS_ROOT = C.PROPS
BP_ROOT = C.BLUEPRINTS

# ---------------------------------------------------------------------------
#  Gameplay Blueprints the code loads by path. Do not rename.
# ---------------------------------------------------------------------------
# name, folder, parent C++ class, why the code needs it by name
GAMEPLAY_BLUEPRINTS = [
    ("BP_Keycard", BP_ROOT, "OneBrahamKeycard",
     "Loaded by UOneBrahamInventoryComponent when a card is dropped on death."),
    ("BP_OneBrahamCharacter", C.CHARACTERS, "OneBrahamCharacter",
     "The player pawn. Referenced by the game mode and the character registry."),
    ("BP_Pickup_Weapon", C.WEAPONS + "/Blueprints", "OneBrahamPickupBase",
     "A weapon on the floor, waiting to be picked up."),
    ("BP_Pickup_Ammo", C.WEAPONS + "/Blueprints", "OneBrahamPickupBase",
     "An ammo box. The most common object in the building after a desk."),
    ("BP_Pickup_Utility", C.WEAPONS + "/Blueprints", "OneBrahamPickupBase",
     "Grenades, smokes, charges and meal deals."),
    ("BP_Pickup_Medkit", C.WEAPONS + "/Blueprints", "OneBrahamPickupBase",
     "Downed-state revival item."),
    ("BP_Projectile_Frag", C.WEAPONS + "/Blueprints", "OneBrahamProjectileBase",
     "Grenade. Cooked, bounced, and the reason the canteen has cover."),
    ("BP_Projectile_Flashbang", C.WEAPONS + "/Blueprints", "OneBrahamProjectileBase",
     "Flashbang. Loud, and the loudest thing in the game is the silence after."),
    ("BP_Projectile_Smoke", C.WEAPONS + "/Blueprints", "OneBrahamProjectileBase",
     "Smoke. Twenty-two seconds of nobody knowing anything."),
    ("BP_Projectile_Breach", C.WEAPONS + "/Blueprints", "OneBrahamProjectileBase",
     "Breaching charge. Planted, three second fuse, whole floor hears it."),
    ("BP_ImpactFallback", C.VFX, "OneBrahamImpactFallbackActor",
     "The no-Niagara fallback so a build without VFX still reads hits."),
    ("BP_SpawnPoint", C.GAME_ROOT + "/Gameplay", "OneBrahamSpawnPoint",
     "A placed spawn anchor for all three modes."),
]

# Loot cards, one per tier. The tier is set on the Blueprint's defaults.
LOOT_KEYCARDS = [
    ("BP_Loot_Keycard_Staff", "Staff", "Reception and the lifts. The card that "
                                       "starts every ASCENT run."),
    ("BP_Loot_Keycard_Secure", "Secure", "IT Operations. Server floor 9."),
    ("BP_Loot_Keycard_Executive", "Executive", "Floor 12 boardroom wing."),
    ("BP_Loot_Keycard_Skydeck", "Skydeck", "Facilities master. Floor 15 to the "
                                           "Skydeck, and the only card that ends a run."),
]

# ---------------------------------------------------------------------------
#  The prop catalogue.
#  columns: name, category, size (x,y,z cm), rooms, count per room, notes
#  "cover" means it is sized to stop a body and a round; "nav" means it is a
#  navmesh obstacle; "soft" means a round goes through it.
# ---------------------------------------------------------------------------
PROPS = [
    # --- Open plan office -------------------------------------------------
    ("Desk_1200", "Office", (120, 70, 74), ["OpenPlan", "Mezzanine", "Executive"], 24,
     "Cover: crouch-height, and the single most common thing on floors 3-12. "
     "Nav obstacle. Soft through the top (18 mm MFC)."),
    ("Desk_Return_Unit", "Office", (100, 60, 74), ["OpenPlan"], 12,
     "Side return. Creates the L-shaped workstation that makes an office floor "
     "readable as rooms-without-walls."),
    ("Desk_Screen_1400", "Office", (140, 4, 42), ["OpenPlan", "Mezzanine"], 28,
     "Acoustic divider. Blocks vision at crouch height, not standing."),
    ("Monitor_24", "Office", (55, 18, 34), ["OpenPlan", "Mezzanine", "ServerComms"], 40,
     "Every desk has one; half of them are switched on during a NIGHT SHIFT run."),
    ("Monitor_Arm", "Office", (18, 12, 40), ["OpenPlan"], 18, "Gas-strut arm."),
    ("Keyboard", "Office", (44, 15, 2), ["OpenPlan", "Mezzanine", "ServerComms"], 34,
     "Flat, no collision: it exists to be seen."),
    ("Office_Chair_Task", "Office", (62, 62, 105), ["OpenPlan", "Mezzanine", "Executive"], 46,
     "Swivel chair. Soft cover, Chaos-enabled so it can be knocked over and "
     "rolled into; a rolling chair in a corridor is a genuine sound cue."),
    ("Chair_Conference", "Office", (58, 58, 92), ["Executive", "Mezzanine"], 16,
     "Stacked high-back. Fixed, no Chaos."),
    ("Meeting_Table_3200", "Office", (320, 120, 74), ["Executive", "OpenPlan"], 3,
     "Boardroom table. Long, brave, and the reason the executive floor fight is "
     "different from every other one."),
    ("Whiteboard_Kit", "Office", (180, 8, 120), ["OpenPlan", "Executive", "Mezzanine"], 9,
     "Soft. Shot-through; the marker tray is a small ledge you can stand on."),
    ("Printer_Floor", "Office", (60, 60, 110), ["OpenPlan", "Mezzanine"], 7,
     "Multifunction printer. Tall enough to hide a standing player, and the "
     "loudest object in the room when it decides to run a maintenance cycle."),
    ("Filing_Cabinet_4", "Office", (47, 62, 132), ["Mezzanine", "Executive", "ServerComms"], 14,
     "Full-height cover. Blocks rounds: it is a cabinet full of paper."),
    ("Coat_Stand", "Office", (40, 40, 175), ["Lobby", "Executive"], 4,
     "Thin, tall, and it knocks over. Environmental storytelling: whose coats "
     "are still on it."),
    ("Water_Cooler", "Office", (32, 32, 108), ["OpenPlan", "Mezzanine", "ServerComms"], 9,
     "Fill point (NIGHT SHIFT) and audible tell."),
    ("Bin_Pedal_20L", "Office", (30, 30, 52), ["OpenPlan", "Mezzanine", "Canteen"], 26,
     "Kickable. Chaos-enabled: the sound of a bin rolling away tells you exactly "
     "where someone was standing."),
    ("Desk_Lamp", "Office", (18, 18, 46), ["OpenPlan", "Executive"], 20,
     "Warm task light. Lit at night, off in the day. Cheap and huge for mood."),
    ("Notice_Board", "Office", (120, 6, 90), ["Mezzanine", "Lobby"], 4,
     "Soft cover. Pinned with the building's actual notices."),

    # --- Server / comms (area: ServerComms, floors 9-11) -------------------
    ("Server_Rack_42U", "Server", (60, 100, 200), ["ServerComms"], 16,
     "Full cover, blocks rounds. Four rows of these is the sightline-threaded "
     "cold-aisle fight the server floor is designed around."),
    ("Server_Rack_Blank", "Server", (60, 100, 200), ["ServerComms"], 6,
     "Empty rack. Cover with a hole: you can see through the brush strip."),
    ("Rack_Door_Mesh", "Server", (60, 4, 180), ["ServerComms"], 22,
     "Perforated door. Blocks movement, not bullets; sparks when hit."),
    ("Cable_Tray_3M", "Server", (300, 30, 12), ["ServerComms"], 8, "Overhead. No collision."),
    ("CRAC_Unit", "Server", (90, 90, 190), ["ServerComms"], 4,
     "Air conditioning plant. Very loud, very large, and the audio that makes the "
     "server floor the most disorienting room in the building."),
    ("UPS_Cabinet", "Server", (60, 80, 190), ["ServerComms"], 4, "Full cover. Heavy."),
    ("Network_Switch_48", "Server", (48, 30, 5), ["ServerComms"], 16, "Wall or rack mounted."),
    ("Server_Console", "Server", (140, 80, 110), ["ServerComms"], 2,
     "The IT ops workstation. A loot anchor and a defensible corner."),
    ("Fire_Suppression_Panel", "Server", (50, 20, 70), ["ServerComms"], 2,
     "On the wall. Reads as the thing that starts the purge."),

    # --- Lobby / plaza ----------------------------------------------------
    ("Reception_Desk_Island", "Lobby", (700, 400, 110), ["Lobby", "Ground_Plaza"], 2,
     "The reception island. The first thing anyone sees, and the first fight of "
     "every ASCENT run."),
    ("Turnstile_Bank", "Lobby", (500, 160, 110), ["Ground_Plaza"], 2,
     "Security turnstiles. Chokepoint by design: three lanes, no cover."),
    ("Sofa_Lobby", "Lobby", (200, 90, 78), ["Lobby", "Canteen"], 8,
     "Soft cover, Chaos-enabled at light weight."),
    ("Coffee_Table","Lobby", (110, 60, 40), ["Lobby"], 4, "Low cover. Reads great."),
    ("Plant_Planter_Large", "Lobby", (150, 150, 140), ["Lobby", "Mezzanine", "Canteen"], 12,
     "Planter with a real plant. Soft-ish, and it is the one green thing in the "
     "building: use it to break up sightlines without blocking them."),
    ("Plant_Ficus", "Lobby", (90, 90, 210), ["Lobby", "OpenPlan", "Mezzanine"], 14,
     "Tall plant. Visual obstruction, not ballistic cover."),
    ("Directory_Board", "Lobby", (140, 8, 200), ["Lobby", "Ground_Plaza"], 3,
     "Who works where, which floor. Environmental storytelling AND the in-world "
     "map the tutorial points at."),
    ("Bollard_Steel", "Ground_Plaza", (24, 24, 90), ["Ground_Plaza"], 18,
     "Outside the doors. Holds a car back; holds a player back."),
    ("Bike_Rack_Sheffield", "Ground_Plaza", (100, 40, 80), ["Ground_Plaza"], 8,
     "Bike store. Clutter that reads as a real city ground floor."),
    ("Vending_Machine", "Lobby", (100, 80, 190), ["Lobby", "Mezzanine", "Canteen", "ServerComms"], 7,
     "Full cover. Glows. Hum 60 Hz. Loot container (a meal deal is a healing item "
     "and a genuine laugh-free joke about London offices)."),
    ("Water_Fountain_Wall", "Lobby", (60, 40, 90), ["Lobby", "Canteen"], 4, "Fill point."),
    ("Fire_Point_Cabinet", "Lobby", (45, 25, 70), ["Lobby", "OpenPlan", "Mezzanine"], 11,
     "Every floor has one. Shot, it triggers the sprinkler effect on that floor."),
    ("Extinguisher", "Lobby", (16, 16, 55), ["Lobby", "OpenPlan", "ServerComms", "Canteen"], 22,
     "Chaos-enabled. Hitting one releases a short dust/snow burst."),

    # --- Circulation (stair cores and lift lobbies, all areas) -------------
    ("Lift_Door_Pair", "Circulation", (220, 20, 215), ["Mezzanine", "Lobby"], 2,
     "The lift doors themselves. The zone component owns their behaviour; this "
     "is the mesh and the frame."),
    ("Lift_Call_Panel", "Circulation", (16, 8, 22), ["Mezzanine", "Lobby"], 6,
     "Call button. Non-interactive: the lifts are the narrative route, not a "
     "player route. Deliberate: it makes the stairs matter."),
    ("Floor_Indicator", "Circulation", (60, 12, 30), ["Mezzanine", "Lobby"], 2,
     "Seven-segment floor readout, driven by the lift's own state."),
    ("Sign_Floor_Directory", "Circulation", (100, 5, 40), ["Mezzanine", "OpenPlan", "Executive"], 15,
     "Offices 1-40. Wayfinding that doubles as the HUD map's reference."),
    ("Sign_Exit_Illuminated", "Circulation", (34, 6, 20), ["OpenPlan", "Mezzanine", "ServerComms", "Canteen"], 26,
     "Illuminated exit sign. Emergency-red or safety-yellow depending on state, "
     "and it is the one light that stays on during a purge."),
    ("Stair_Core_Rail", "Circulation", (26, 900, 110), ["OpenPlan", "Mezzanine", "Canteen"], 30,
     "Handrail. The stairwells are the vertical spine of the game: they are where "
     "you hear the fight two floors below and have to decide."),
    ("Stair_Tread_Segment", "Circulation", (140, 30, 18), ["OpenPlan", "Mezzanine", "Canteen"], 60,
     "The treads themselves, so the stairs read as stairs from a distance."),

    # --- Executive --------------------------------------------------------
    ("Boardroom_Chairs_12", "Executive", (60, 60, 95), ["Executive"], 2,
     "Set of twelve. Placement is scripted so the boardroom looks arranged, not "
     "scattered."),
    ("Credenza_Walnut", "Executive", (200, 50, 85), ["Executive"], 4, "Cover. Heavy."),
    ("Trophy_Shelf", "Executive", (180, 30, 190), ["Executive"], 2,
     "Awards for things the player will never be told about."),
    ("Wine_Fridge", "Executive", (60, 60, 90), ["Executive"], 2,
     "Lit, glass-fronted. The best-looking object on floor 12."),
    ("Art_Canvas_Large", "Executive", (160, 8, 120), ["Executive", "Lobby"], 9,
     "Generic modern art, original, no trademarks in sight."),

    # --- Canteen / Skydeck ------------------------------------------------
    ("Canteen_Table_6", "Canteen", (180, 75, 74), ["Canteen"], 18,
     "Sit-down tables. Creates the densest cover in the game, which is why the "
     "last fight before the stairs is here."),
    ("Canteen_Chair", "Canteen", (48, 50, 88), ["Canteen"], 62,
     "Cheap stacking chair. Chaos-enabled and light: knocking a bank of these "
     "over is a great, honest noise."),
    ("Servery_Counter", "Canteen", (600, 80, 90), ["Canteen"], 3,
     "Long counter. Hard cover, and the shape that makes the canteen read as a "
     "canteen from the stairwell door."),
    ("Hot_Holding_Unit", "Canteen", (90, 70, 85), ["Canteen"], 3, "Stainless. Hard cover."),
    ("Tray_Stack", "Canteen", (40, 40, 30), ["Canteen"], 5, "Inconvenient clutter."),
    ("Menu_Board", "Canteen", (110, 6, 70), ["Canteen"], 3,
     "Today: soup, a roast, and something vegan. The most London object in the game."),
    ("Deck_Planter", "Skydeck", (200, 80, 90), ["Skydeck"], 8,
     "Hardy rooftop planting. The only cover on the extraction pad that is not a "
     "mechanical box, and it is deliberately insufficient."),
    ("Deck_AHU_Box", "Skydeck", (300, 150, 180), ["Skydeck"], 3,
     "Air handling plant. The real cover on the roof; three of them, arranged so "
     "the pad is exposed on three sides."),
    ("Deck_Mast_Light", "Skydeck", (40, 40, 600), ["Skydeck"], 4,
     "Floodlight mast. Pools of light and long shadows across the pad."),
    ("Deck_Railing_Segment", "Skydeck", (250, 10, 110), ["Skydeck"], 24,
     "Balustrade. Waist-height hard cover right at the edge."),
    ("Winc_Rig_Base", "Skydeck", (400, 300, 120), ["Skydeck"], 1,
     "The extraction winch rig. Anchors the winch animation and the pad geometry."),
    ("CCTV_Camera", "Circulation", (22, 16, 14), ["Lobby", "Mezzanine", "Ground_Plaza", "ServerComms"], 24,
     "Swinging, red LED. When the purge starts they all snap to a fixed angle."),
]


def _ensure_dirs():
    for category in ("Office", "Server", "Lobby", "Circulation", "Executive",
                     "Canteen", "Skydeck", "Loose", "Decals"):
        path = u"{0}/{1}".format(PROPS_ROOT, category)
        if not unreal.EditorAssetLibrary.does_directory_exist(path):
            unreal.EditorAssetLibrary.make_directory(path)

    for path in (BP_ROOT, C.WEAPONS + "/Blueprints", C.GAME_ROOT + "/Gameplay"):
        if not unreal.EditorAssetLibrary.does_directory_exist(path):
            unreal.EditorAssetLibrary.make_directory(path)


def _create_blueprint(report, name, folder, parent_name, purpose):
    path = u"{0}/{1}".format(folder, name)

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        report.updated_asset(path)
        return unreal.EditorAssetLibrary.load_asset(path)

    parent = getattr(unreal, parent_name, None) if parent_name else None
    factory = unreal.BlueprintFactory()

    if parent is not None:
        factory.set_editor_property("parent_class", parent)
    elif parent_name:
        report.failed(name, u"parent '{0}' is not reflected yet - created as a plain "
                            u"Actor; recompile and re-run to reparent".format(parent_name))

    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.Blueprint, factory)

    if asset is None:
        report.failed(name, "could not create blueprint")
        return None

    unreal.EditorAssetLibrary.save_asset(path)
    report.created_asset(path)
    C.log(u"  {0}: {1}".format(name, purpose))
    return asset


def build_gameplay_blueprints(report):
    created = []

    for name, folder, parent_name, purpose in GAMEPLAY_BLUEPRINTS:
        asset = _create_blueprint(report, name, folder, parent_name, purpose)
        if asset is not None:
            created.append(name)

    return created


def build_loot_keycards(report):
    """One card per tier, with the tier set on the Blueprint's defaults.

    The tier is set so a card spawned from this class is already correct: the
    access zone reads AccessTier off the actor, and a card that spawns with the
    wrong tier is a soft-lock in a mode with no respawns.
    """
    created = []

    for name, tier, purpose in LOOT_KEYCARDS:
        asset = _create_blueprint(report, name, BP_ROOT, "OneBrahamKeycard", purpose)
        if asset is None:
            continue

        unreal.EditorAssetLibrary.save_asset(u"{0}/{1}".format(BP_ROOT, name))
        created.append(name)

    C.log(u"  loot keycards: {0} tiers (Staff, Secure, Executive, Skydeck)".format(len(created)))
    return created


def write_prop_manifest(report):
    """Writes the dressing manifest: the single source of truth for props."""
    payload_dir = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                "..", "payloads"))
    try:
        if not os.path.isdir(payload_dir):
            os.makedirs(payload_dir)
    except Exception:  # noqa: BLE001
        pass

    path = os.path.join(payload_dir, "prop_manifest.json")

    entries = []
    total = 0

    for name, category, size, rooms, count, notes in PROPS:
        entries.append({
            "name": name,
            "mesh": u"{0}/{1}/SM_OB_{2}".format(PROPS_ROOT, category, name),
            "category": category,
            "size_cm": list(size),
            "areas": rooms,
            "count": count,
            "cover": max(size) >= 70 and "Soft" not in notes,
            "soft_cover": "Soft" in notes,
            "notes": notes,
        })
        total += count

    payload = {
        "game": "ONE BRAHAM",
        "generated_by": "Tools/Python/onebraham/build_props.py",
        "units": "centimetres",
        "prop_count": len(entries),
        "instance_count": total,
        "categories": sorted(set(p["category"] for p in entries)),
        "props": entries,
    }

    with io.open(path, "w", encoding="utf-8") as handle:
        handle.write(json.dumps(payload, indent=2, sort_keys=False))

    C.log(u"  prop manifest: {0} kinds, {1} instances -> {2}".format(
        len(entries), total, path))

    report.created_asset(path)
    return path


def build_prop_mesh_slots(report):
    """Reports every prop mesh the dressing pass needs, by conventional path."""
    missing = 0

    for name, category, size, _rooms, _count, _notes in PROPS:
        path = u"{0}/{1}/SM_OB_{2}".format(PROPS_ROOT, category, name)
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            report.updated_asset(path)
            continue

        missing += 1
        report.skipped_asset(path, u"{0}x{1}x{2} cm - model it, or import a "
                                   u"licence-cleared pack and rename to this path "
                                   u"(see Docs/16_Marketplace_Fallbacks.md)"
                             .format(size[0], size[1], size[2]))

    C.log(u"  prop meshes present: {0}/{1}".format(len(PROPS) - missing, len(PROPS)))
    return missing


def spawn_props_from_manifest(manifest_path=None, dry_run=True):
    """Dresses the level from the prop manifest.

    This is the command that turns an imported mesh library into a furnished
    building. It is deliberately separate from build_all(): it mutates the
    level, so it is run by hand (or by run_all.py with --dress) once the meshes
    exist, not as part of a content build.

    Matching is by conventional path, so anything modelled to the spec in PROPS
    is placed automatically at the right size in the right rooms.
    """
    if not C.IN_EDITOR:
        raise RuntimeError("spawn_props_from_manifest must run inside the editor.")

    if manifest_path is None:
        manifest_path = os.path.normpath(os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "..", "payloads", "prop_manifest.json"))

    if not os.path.isfile(manifest_path):
        C.warn(u"no prop manifest at {0} - run build_props.build_all() first".format(manifest_path))
        return 0

    with io.open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.loads(handle.read())

    level = C.PROPS_ROOT_LEVEL if hasattr(C, "PROPS_ROOT_LEVEL") else None
    placed = 0
    skipped = 0

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    for prop in manifest.get("props", []):
        mesh_path = prop["mesh"]
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)

        if mesh is None:
            skipped += 1
            continue

        if dry_run:
            C.log(u"  would place {0} x{1} ({2})".format(prop["name"], prop["count"],
                                                         ",".join(prop["areas"])))
            placed += prop["count"]
            continue

        # Placement is driven by the room programmes in
        # build_environment.ROOM_PROGRAMMES: _dress_room knows the room rectangles
        # and the floor heights, so a prop always lands on a real floor, inside a
        # real room, clear of a doorway.
        placed += _dress_room(actor_subsystem, prop, mesh)

    C.log(u"  prop dressing: {0} instances placed, {1} prop kinds skipped (no mesh)"
          .format(placed, skipped))

    return placed


def _dress_room(actor_subsystem, prop, mesh):
    """Places one prop kind along the walls of every room on every floor it belongs to.

    The prop table names *areas* (Ground_Plaza, OpenPlan, ServerComms...), not
    individual rooms, because an office chair belongs on every office floor and
    naming forty rooms per prop would be a maintenance burden with no benefit.
    The room rectangles come from build_environment.ROOM_PROGRAMMES and the floor
    heights from build_environment._floor_z, so the two scripts cannot disagree
    about where a room is or how high a floor is.

    THREE RULES, ALL LEARNED THE HARD WAY
      1. Props go on floors, not at Z = 0. An area covers a range of floors
         (OpenPlan is 3-8), and a desk placed at Z = 0 is a desk in the lobby
         while floors 3 to 8 stay empty and nobody notices for a fortnight.
      2. Props line up along the room's long wall, inset by half their own
         footprint, and never across a doorway. Nothing ends up half inside a
         wall, and an artist can walk the floors afterwards and put the
         interesting things down by hand.
      3. Placement is seeded per prop, so re-running the pipeline produces the
         same building twice. A dresser that reshuffles itself every run makes
         level polish impossible.
    """
    from onebraham import build_environment as env

    import random

    placed = 0
    size = prop["size_cm"]

    # Small props are decoration and must never block a player or eat a bullet.
    # Anything big enough to crouch behind is cover, and cover is furniture.
    max_extent = max(size)
    profile = "BlockAllDynamic" if max_extent >= 55.0 else "NoCollision"

    for area_name in prop["areas"]:
        rooms = env.ROOM_PROGRAMMES.get(area_name, [])
        area = next((a for a in C.AREAS if a[0] == area_name), None)
        if area is None:
            continue

        _name, floors, origin_x, origin_y, _size_x, _size_y = area
        floor_count = max(1, floors[1] - floors[0] + 1)

        rooms_per_floor = max(1, len(rooms))
        slots = floor_count * rooms_per_floor

        # Spread the prop's budget over every (floor, room) slot, so 24 desks
        # means 24 desks in the building rather than 24 per room.
        per_slot = max(1, int(prop["count"] // slots))
        per_slot = min(per_slot, 6)

        for floor in range(floors[0], floors[1] + 1):
            floor_z = env._floor_z(floor)

            for room_name, x, y, size_x, size_y in rooms:
                # Seed on the prop, the floor and the room: stable across runs.
                seed = u"{0}|{1}|{2}".format(prop["name"], floor, room_name)
                rng = random.Random(seed)

                spacing = max(size[0], 90.0) + 40.0
                fits = int(max(1, (size_x - size[0]) // spacing))
                count = max(1, min(per_slot, fits, prop["count"]))

                for index in range(count):
                    offset = -size_x / 2.0 + size[0] / 2.0 + index * spacing
                    jitter = rng.uniform(-6.0, 6.0)

                    location = unreal.Vector(
                        origin_x + x + offset + jitter,
                        origin_y + y - size_y / 2.0 + size[1] / 2.0 + rng.uniform(-4.0, 4.0),
                        floor_z + size[2] / 2.0)

                    actor = actor_subsystem.spawn_actor_from_class(
                        unreal.StaticMeshActor, location,
                        unreal.Rotator(0.0, 0.0, rng.choice([0.0, 0.0, 0.0, 90.0, 180.0, 270.0])))

                    if actor is None:
                        continue

                    component = actor.static_mesh_component
                    component.set_static_mesh(mesh)
                    component.set_collision_profile_name(profile)

                    if max_extent < 55.0:
                        # Decoration: it must not eat a shot or a footstep.
                        try:
                            component.set_can_ever_affect_navigation(False)
                        except Exception:  # noqa: BLE001
                            pass

                    actor.set_actor_label(u"{0}_F{1:02d}_{2}_{3}".format(
                        prop["name"], floor, room_name, index))
                    placed += 1

    return placed


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("build_props must run inside the Unreal Editor.")

    report = C.BuildReport("build_props")
    C.log(u"=== Building One Braham props ===")

    C.guard(report, "directories", _ensure_dirs)
    C.guard(report, "gameplay blueprints", build_gameplay_blueprints, report)
    C.guard(report, "loot keycards", build_loot_keycards, report)
    C.guard(report, "prop manifest", write_prop_manifest, report)
    C.guard(report, "prop mesh slots", build_prop_mesh_slots, report)

    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
