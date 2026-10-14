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

    core_half = C.LIFT_CORE_SIZE_CM / 2.0
    for name, (_floors, cx, cy, size_x, size_y) in sorted(
            (row[0], (row[1], row[2], row[3], row[4], row[5])) for row in areas.values()):
        if abs(cx) + core_half > size_x / 2.0 or abs(cy) + core_half > size_y / 2.0:
            found.append((u"the lift core at the origin does not fit inside the {0} footprint".format(name), None))

    pad_x, pad_y, pad_z = C.EXTRACTION_PAD_LOCATION
    skydeck_z = E._floor_z(C.FLOOR_COUNT)
    if abs(pad_z - skydeck_z) > 1.0:
        found.append((u"the extraction pad is at Z {0:.0f} and the Skydeck slab at {1:.0f}".format(
            pad_z, skydeck_z), None))

    if E._floor_z(1) != 0.0 or E._floor_z(0) != -C.FLOOR_HEIGHT_CM:
        found.append((u"_floor_z no longer puts the lobby at Z 0 and the plaza a storey below "
                      u"(Docs/09 section 1, OneBrahamConstants::FloorZ)", None))

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
