# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_building_layout
==============================

Checks that the game's description of the building matches the building.

WHY THIS EXISTS
    The map is built from a table in `build_environment.py`. The game describes
    that map from a table in `OneBrahamBuildingLayoutAsset.cpp` - which floor is
    the server floor, which floors the lockdown may seal, what the lift says when
    it arrives. Two tables, two languages, one building.

    When they disagree the failure is invisible and specific: the floor plan
    tells a player they are on the executive floor while they are standing in the
    server hall, the lockdown seals floor 11 believing it is a comms room, and
    the audio director scores a boardroom with a data-centre hum. Nothing errors,
    nothing crashes, and no test fails, because both tables are internally
    consistent and only the pair is wrong.

    The first draft of the layout asset had the executive wing on floor 11 and
    the server floor on 10, which is exactly this bug, written by hand, twenty
    minutes after the map was built.

WHAT IT COMPARES
    * The area name and floor range of every area in `common.AREAS` against the
      floor table in the layout asset.
    * The breachable floors in `BREACHABLE_WALLS` against the asset's
      `bBreachable` flags.
    * The floor count and the extraction floor.
    * The builder's coordinate frames: every spawn anchor inside its area's
      footprint, the lift core inside every floor, the pad on the Skydeck slab,
      and the vertical datum (lobby at Z 0) that the C++ also uses.

WHAT IT CANNOT DO
    It cannot know what the level actually looks like. It checks one hand-written
    table against another, which is the whole point: if the layout is going to be
    wrong, it should be wrong in the same way twice, or a human should have to
    say so on purpose.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_building_layout.py
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

python_root = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

LAYOUT_SOURCE = os.path.join(python_root, "Source", "OneBraham", "Data",
                             "OneBrahamBuildingLayoutAsset.cpp")

#: AddFloor(Index, "AreaId", NSLOCTEXT(...), NSLOCTEXT(...), bEndgame, bBreachable)
ADD_FLOOR = re.compile(
    r'AddFloor\(\s*(\d+)\s*,\s*"([^"]+)"\s*,'      # index, area id
    r'\s*NSLOCTEXT\([^)]*\)\s*,\s*NSLOCTEXT\([^)]*\)\s*,'  # two labels
    r'\s*(true|false)\s*,\s*(true|false)\s*\)',     # endgame, breachable
    re.DOTALL)

MAY_SEAL_ALL = "SHUTTER_FLOORS"


def builder_floors():
    """floor index -> area name, from the table the level is built from."""
    floors = {}

    for name, (first, last) in C.AREAS_AS_PAIRS():
        for index in range(first, last + 1):
            floors[index] = name

    return floors


def builder_breach_floors():
    from onebraham import build_environment as E

    return sorted({int(row[1]) for row in E.BREACHABLE_WALLS})


def layout_floors():
    """(floor index -> area id), (breachable floors), from the C++ table."""
    if not os.path.isfile(LAYOUT_SOURCE):
        return {}, [], 0

    text = io.open(LAYOUT_SOURCE, encoding="utf-8", errors="ignore").read()

    # Comments describe the table; only the calls build it.
    text = re.sub(r'//[^\n]*', "", text)

    areas = {}
    breachable = []
    count = 0

    for match in ADD_FLOOR.finditer(text):
        index = int(match.group(1))
        area_id = match.group(2)
        is_breachable = match.group(4) == "true"

        areas[index] = area_id
        count += 1

        if is_breachable:
            breachable.append(index)

    return areas, sorted(breachable), count


def run():
    """Returns (floors_checked, problems)."""
    problems = []

    if not os.path.isfile(LAYOUT_SOURCE):
        return 0, [(u"Data/OneBrahamBuildingLayoutAsset.cpp is missing", None)]

    expected = builder_floors()
    actual, actual_breach, count = layout_floors()

    if count == 0:
        return 0, [(u"no AddFloor rows were parsed out of the layout asset - the table "
                    u"was renamed or reformatted", None)]

    # --- Floor count -------------------------------------------------------
    if count != len(expected):
        problems.append((
            u"the layout asset describes {0} floors; the map builds {1}".format(count, len(expected)),
            None))

    # --- Area per floor ----------------------------------------------------
    for index in sorted(expected):
        expected_area = expected[index]

        if index not in actual:
            problems.append((
                u"floor {0} is {1} in the map and absent from the layout asset".format(
                    index, expected_area), None))
            continue

        # The builder names the area in snake case ("Ground_Plaza"), the layout
        # asset in Pascal case for the id. Compare case-insensitively on the
        # underscores removed, so a rename shows up and a style choice does not.
        normalise = lambda value: value.replace("_", "").lower()

        if normalise(actual[index]) != normalise(expected_area):
            problems.append((
                u"floor {0}: the map says '{1}', the layout asset says '{2}' - the plan "
                u"will name the wrong area".format(index, expected_area, actual[index]), None))

    for index in sorted(actual):
        if index not in expected:
            problems.append((
                u"floor {0} exists in the layout asset but the map does not build it".format(index),
                None))

    # --- Breachable walls --------------------------------------------------
    expected_breach = builder_breach_floors()

    if actual_breach != expected_breach:
        only_map = sorted(set(expected_breach) - set(actual_breach))
        only_asset = sorted(set(actual_breach) - set(expected_breach))

        if only_map:
            problems.append((
                u"floors {0} have a breachable wall in the map and not in the layout asset - "
                u"breach warnings will be missing".format(only_map), None))
        if only_asset:
            problems.append((
                u"floors {0} are marked breachable in the layout asset and have no wall in the "
                u"map - the warning will point at nothing".format(only_asset), None))

    problems.extend(frame_problems())

    return len(expected), problems


def frame_problems():
    """The builder's coordinate frames agree with each other.

    AREAS stores footprint corners; the builders place slabs, rooms, lights and
    volumes about the area centre, and the lift core, spawn anchors and pad
    about the world origin. When the two frames disagreed every floor was built
    half a footprint south-west of its own lifts and spawns, and nothing in the
    engine-free checks could see it. These are the invariants that would have.
    """
    from onebraham import build_environment as E

    found = []
    areas = {row[0]: C.area_centre(row) for row in C.AREAS}

    for area_name, anchors in sorted(E.ANCHORS.items()):
        area = areas.get(area_name)
        if area is None:
            found.append((u"spawn anchors for unknown area '{0}'".format(area_name), None))
            continue

        _name, _floors, cx, cy, size_x, size_y = area
        for anchor_name, ax, ay in anchors:
            if abs(ax - cx) > size_x / 2.0 - 50.0 or abs(ay - cy) > size_y / 2.0 - 50.0:
                found.append((u"spawn anchor {0} ({1:.0f}, {2:.0f}) is outside the {3} footprint "
                              u"centred on ({4:.0f}, {5:.0f}), {6:.0f} x {7:.0f}".format(
                                  anchor_name, ax, ay, area_name, cx, cy, size_x, size_y), None))

    found.extend(core_problems(E, areas))

    pad_x, pad_y, pad_z = C.EXTRACTION_PAD_LOCATION
    skydeck_z = E._floor_z(C.FLOOR_COUNT)
    if abs(pad_z - skydeck_z) > 1.0:
        found.append((u"the extraction pad is at Z {0:.0f} and the Skydeck slab at {1:.0f}".format(
            pad_z, skydeck_z), None))

    if E._floor_z(1) != 0.0 or E._floor_z(0) != -C.FLOOR_HEIGHT_CM:
        found.append((u"_floor_z no longer puts the lobby at Z 0 and the plaza a storey below "
                      u"(Docs/09 section 1, OneBrahamConstants::FloorZ)", None))

    found.extend(loot_capacity_problems())
    found.extend(rotator_problems())
    found.extend(post_process_problems())
    found.extend(control_point_problems())
    return found


#: `unreal.Rotator(a, b, c)` with plain positional numbers.
POSITIONAL_ROTATOR = re.compile(
    r'unreal\.Rotator\(\s*(-?[0-9.]+)\s*,\s*(-?[0-9.]+)\s*,\s*([^)]+?)\s*\)')


def rotator_problems():
    """No builder passes a pitch or a roll positionally.

    unreal.Rotator's positional order is (roll, pitch, yaw). Written the C++
    way round, (pitch, yaw, roll), a ceiling light aimed down shines sideways
    and nothing errors: four calls in build_environment were like that. Only a
    pure yaw, `Rotator(0.0, 0.0, yaw)`, is unambiguous enough to pass
    positionally; anything else uses keywords.
    """
    found = []
    tools = os.path.join(python_root, "Tools", "Python", "onebraham")
    for name in sorted(os.listdir(tools)):
        if not (name.startswith("build_") and name.endswith(".py")):
            continue
        text = io.open(os.path.join(tools, name), encoding="utf-8").read()
        for match in POSITIONAL_ROTATOR.finditer(text):
            if float(match.group(1)) != 0.0 or float(match.group(2)) != 0.0:
                line = text[:match.start()].count("\n") + 1
                found.append((u"{0}:{1} {2} passes roll/pitch positionally; unreal.Rotator is "
                              u"(roll, pitch, yaw) - use keywords".format(name, line, match.group(0)), None))
    return found


#: A struct read off an actor by attribute: `volume.settings`, `ppv.settings`.
STRUCT_COPY = re.compile(r'\b\w+\.settings\b(?!\s*=)')

#: Settings the player owns, which a volume value would override.
PLAYER_OWNED_PP = re.compile(r'(?i)motion_?blur|film_?grain')


#: The arena platforms: points = [("A_Lobby", 0.0, -2600.0), ...]
ARENA_POINT = re.compile(r'\("([A-Z]_[A-Za-z]+)",\s*(-?[0-9.]+),\s*(-?[0-9.]+)\)')

#: The control points the match settings define: AddPoint(..., FVector(x, y, z), ...)
SETTINGS_POINT = re.compile(r'AddPoint\(TEXT\("(CP_[A-Za-z]+)"\).*?FVector\((-?[0-9.]+)f,\s*(-?[0-9.]+)f')


def control_point_problems():
    """Every FIRE DRILL control point stands on an arena platform, and every
    platform has a point: the settings place them by coordinates, the builder
    lays the platforms by coordinates, and nothing else ties the two."""
    tools = os.path.join(python_root, "Tools", "Python", "onebraham", "build_environment.py")
    settings = os.path.join(python_root, "Source", "OneBraham", "Data", "OneBrahamMatchSettingsAsset.cpp")
    builder = io.open(tools, encoding="utf-8").read()
    if "def build_fire_drill_arena" not in builder:
        return [(u"build_fire_drill_arena not found in build_environment.py", None)]
    arena = builder[builder.index("def build_fire_drill_arena"):]
    arena = arena[:arena.index("\ndef ", 10)]
    platforms = set((float(x), float(y)) for _n, x, y in ARENA_POINT.findall(arena))
    points = set((float(x), float(y))
                 for _n, x, y in SETTINGS_POINT.findall(io.open(settings, encoding="utf-8").read()))
    found = []
    if not points:
        found.append((u"no control points found in OneBrahamMatchSettingsAsset.cpp", None))
    for point in sorted(points - platforms):
        found.append((u"a FIRE DRILL control point at {0} has no arena platform under it".format(point), None))
    for platform in sorted(platforms - points):
        found.append((u"the arena platform at {0} has no control point".format(platform), None))
    return found


def post_process_problems():
    """Post-process and volume settings go through _apply_post_process.

    In Unreal Python, `actor.settings` is a copy: editing it changes nothing,
    and every grade in the building was lost that way, behind `except: pass`.
    Motion blur and film grain belong to the settings menu; a volume value
    overrides the player's choice.
    """
    found = []
    path = os.path.join(python_root, "Tools", "Python", "onebraham", "build_environment.py")
    text = io.open(path, encoding="utf-8").read()
    # Docstrings blanked line for line, so they can describe the mistake and
    # line numbers still match the file.
    text = re.sub(r'"""[\s\S]*?"""', lambda m: "\n" * m.group(0).count("\n"), text)
    for number, line in enumerate(text.splitlines(), 1):
        code = line.split("#", 1)[0]
        if code.strip().startswith(("u\"", "\"")):
            continue
        if STRUCT_COPY.search(code):
            found.append((u"build_environment.py:{0} reads an actor's settings struct by attribute, "
                          u"which is a copy; use _apply_post_process".format(number), None))
        if "set_editor_property" in code and PLAYER_OWNED_PP.search(code):
            found.append((u"build_environment.py:{0} sets motion blur or film grain on a volume; "
                          u"the player's settings own those".format(number), None))
    return found


def _rect(cx, cy, sx, sy):
    return (cx - sx / 2.0, cx + sx / 2.0, cy - sy / 2.0, cy + sy / 2.0)


def _overlaps(a, b):
    return a[0] < b[1] - 1.0 and b[0] < a[1] - 1.0 and a[2] < b[3] - 1.0 and b[2] < a[3] - 1.0


def _inside(point, rect):
    return rect[0] < point[0] < rect[1] and rect[2] < point[1] < rect[3]


def core_problems(E, areas):
    """The service core works on every floor and nothing is built in its way.

    The first layout put a 12 m core in the middle of every floor under the
    boardroom, the canteen and the helipad, gave the stairs solid landings and
    the lifts solid doors, and cut no slab: there was no way from one floor to
    the next. These are the rules that make a climb possible.
    """
    found = []
    band = C.CORE_BAND
    corridor = C.CORE_CORRIDOR
    floor_area = {}

    for name, (first, last), cx, cy, size_x, size_y in areas.values():
        footprint = _rect(cx, cy, size_x, size_y)
        for floor in range(first, last + 1):
            floor_area[floor] = (name, footprint)

        if not (footprint[0] <= band[0] and band[1] <= footprint[1]
                and footprint[2] <= corridor[2] and band[3] <= footprint[3]):
            found.append((u"the core band and its corridor do not fit inside the {0} footprint".format(name), None))

        rooms = E.ROOM_PROGRAMMES.get(name, [])
        walled = [r for r in rooms if r[0] not in E.OPEN_AIR_ROOMS]
        for room in rooms:
            rect = _rect(room[1], room[2], room[3], room[4])
            if room[0] in ("Edge_Rail_North", "Edge_Rail_South"):
                continue
            if rect[0] < footprint[0] or rect[1] > footprint[1] or rect[2] < footprint[2] or rect[3] > footprint[3]:
                found.append((u"{0}/{1} is outside the floor".format(name, room[0]), None))
            if _overlaps(rect, band) or _overlaps(rect, corridor):
                found.append((u"{0}/{1} is built into the service core or its corridor".format(name, room[0]), None))

        for index, room in enumerate(rooms):
            for other in rooms[index + 1:]:
                if _overlaps(_rect(*room[1:]), _rect(*other[1:])):
                    found.append((u"{0}: {1} and {2} overlap".format(name, room[0], other[0]), None))

        # Every walled room's doorway is in the middle of its south wall; the
        # floor in front of it has to be floor, not another room or the core.
        for room in walled:
            door = (room[1] - C.DOOR_WIDTH_CM / 2.0, room[1] + C.DOOR_WIDTH_CM / 2.0,
                    room[2] - room[4] / 2.0 - 90.0, room[2] - room[4] / 2.0 - 5.0)
            blocked = door[2] < footprint[2] or _overlaps(door, band)
            blocked = blocked or any(_overlaps(door, _rect(*other[1:])) for other in walled if other is not room)
            if blocked:
                found.append((u"{0}/{1}: its doorway opens onto a wall".format(name, room[0]), None))

        for anchor, ax, ay in E.ANCHORS.get(name, []):
            if _inside((ax, ay), band) or _inside((ax, ay), corridor):
                found.append((u"spawn {0} is in the service core".format(anchor), None))
            for room in walled:
                if _inside((ax, ay), _rect(*room[1:])):
                    found.append((u"spawn {0} is inside {1}".format(anchor, room[0]), None))

    placed = [(row[0], row[1], row[3], row[4]) for row in E.ACCESS_ZONES]
    placed += [(row[0], row[1], row[2], row[3]) for row in E.OVERRIDE_TERMINALS]
    placed += [(row[0], row[1], row[2], row[3]) for row in E.BREACHABLE_WALLS]
    for ident, floor, x, y in placed:
        if floor not in floor_area:
            found.append((u"{0} is on floor {1}, which the map does not build".format(ident, floor), None))
            continue
        name, footprint = floor_area[floor]
        if not _inside((x, y), footprint):
            found.append((u"{0} is outside the {1} footprint".format(ident, name), None))
        if _inside((x, y), band):
            found.append((u"{0} is inside the service core".format(ident), None))

    for floor, (name, footprint) in sorted(floor_area.items()):
        for label, _kind, x, y in E.destructible_props_for_floor(floor):
            if not _inside((x, y), footprint):
                found.append((u"{0} is outside the {1} footprint".format(label, name), None))
            if _inside((x, y), band):
                found.append((u"{0} is inside the service core".format(label), None))

    # The stairs climb a storey, flight by flight, with steps a person can use.
    rise = (C.FLOOR_HEIGHT_CM / 2.0) / C.STAIR_RISERS_PER_FLIGHT
    if not (15.0 <= rise <= 20.0 and 25.0 <= C.STAIR_TREAD_CM <= 32.0):
        found.append((u"stair risers are {0:.1f} cm and treads {1:.1f} cm".format(rise, C.STAIR_TREAD_CM), None))
    if C.CORE_BAND[2] + C.STAIR_LANDING_DEPTH_CM + C.STAIR_RISERS_PER_FLIGHT * C.STAIR_TREAD_CM \
            > C.CORE_BAND[3] - 150.0:
        found.append((u"the stair flights leave less than 150 cm for the mid-landing", None))
    for floor in range(0, C.FLOOR_COUNT):
        boxes = E.stair_flights(floor)
        for stair, _cx in C.STAIRWELLS:
            tops = [b[3] + b[6] / 2.0 for b in boxes if b[0].startswith("Stair_{0}_F{1:02d}_Up2".format(stair, floor))]
            if not tops or abs(max(tops) - E._floor_z(floor + 1)) > 0.5:
                found.append((u"stair {0} from floor {1} does not arrive at floor {2}".format(stair, floor, floor + 1), None))
        holes = C.core_holes(floor + 1)
        if len([h for h in holes if h[1] - h[0] == C.STAIRWELL_SIZE_CM]) != len(C.STAIRWELLS):
            found.append((u"floor {0}'s slab is not cut for the stairs from below".format(floor + 1), None))

    return found


MATCH_SETTINGS_SOURCE = os.path.join(python_root, "Source", "OneBraham", "Data",
                                     "OneBrahamMatchSettingsAsset.cpp")

#: `if (Floor == 0) { ... }`, `else if (Floor <= 8) { ... }`, `else { ... }`
LOOT_BRANCH = re.compile(r'(if\s*\(\s*Floor\s*==\s*0\s*\)|else\s+if\s*\(\s*Floor\s*<=\s*(\d+)\s*\)|else)\s*\{([^{}]*)\}')
LOOT_FIELD = re.compile(r'Profile\.(WeaponSpawns|AmmoSpawns|UtilitySpawns|MedkitSpawns|KeycardSpawns)\s*=\s*(\d+)')


def loot_demand_per_floor():
    """floor -> (items asked for, keycards asked for), from the C++ default profiles."""
    try:
        text = io.open(MATCH_SETTINGS_SOURCE, encoding="utf-8").read()
    except IOError:
        return {}

    start = text.find("FloorLootProfiles.Num() == 0")
    if start < 0:
        return {}

    demand = {}
    previous_last = -1
    for match in LOOT_BRANCH.finditer(text, start):
        fields = dict((name, int(value)) for name, value in LOOT_FIELD.findall(match.group(3)))
        if not fields:
            continue

        if match.group(1).startswith("if"):
            last = 0
        elif match.group(2):
            last = int(match.group(2))
        else:
            last = C.FLOOR_COUNT

        for floor in range(previous_last + 1, last + 1):
            demand[floor] = (sum(fields.values()), fields.get("KeycardSpawns", 0))
        previous_last = last

        if last >= C.FLOOR_COUNT:
            break

    return demand


def loot_capacity_problems():
    """Every floor has at least as many loot points as its default profile asks for.

    The runtime places what it can and logs the rest as unplaced, so a floor
    short of points is a quieter floor rather than an error: exactly the kind
    of thing that is only noticed when a playtester says the mezzanine is empty.
    """
    from onebraham import build_environment as E

    demand = loot_demand_per_floor()
    if not demand:
        return [(u"the default loot profiles were not found in Data/OneBrahamMatchSettingsAsset.cpp", None)]

    points = E.loot_points_per_floor()
    keycard_points = {}
    for area in C.AREAS:
        name, (first, last), cx, cy, _sx, _sy = C.area_centre(area)
        cards = sum(1 for room in E.ROOM_PROGRAMMES.get(name, [])
                    for _x, _y, card in E.loot_points_for_room(room, cx, cy) if card)
        for floor in range(first, last + 1):
            keycard_points[floor] = cards

    found = []
    for floor, (items, cards) in sorted(demand.items()):
        if points.get(floor, 0) < items:
            found.append((u"floor {0} asks for {1} loot items and the map has {2} loot points".format(
                floor, items, points.get(floor, 0)), None))
        if keycard_points.get(floor, 0) < cards:
            found.append((u"floor {0} asks for {1} keycards and has {2} points that may hold one".format(
                floor, cards, keycard_points.get(floor, 0)), None))

    return found


class _LayoutReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} floors, {1} mismatch ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} floors, map and layout agree".format(self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()

    C.log(u"")
    C.log(u"  floors compared between the map and the game's layout table: {0}".format(checked))

    if not problems:
        C.log(u"  the building the map builds and the building the game describes are the same "
              u"building. OK")
        return _LayoutReport(checked, problems)

    C.log(u"  MISMATCH: {0}".format(len(problems)))
    for message, _ in problems:
        C.log(u"      {0}".format(message))

    return _LayoutReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - building layout: the map against the game's own table")
    C.log(u"=" * 70)
    C.log(u"  floors compared: {0}".format(checked))

    if not problems:
        C.log(u"  agree. OK")
        return 0

    C.log(u"  MISMATCH: {0}".format(len(problems)))
    for message, _ in problems:
        C.log(u"  {0}".format(message))

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
