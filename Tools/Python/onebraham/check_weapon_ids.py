# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_weapon_ids
==========================

Checks that the ten weapons are ten weapons, everywhere.

WHY THIS EXISTS
    There is one armoury and there were three descriptions of it, and they
    disagreed about the id of every single weapon:

      * `Tools/Python/onebraham/build_weapons.py` writes the definition assets,
        keyed `W_Pistol`, `W_SMG`, `W_Carbine556`, ... It is what the registry
        asset is generated from, and it is what the game loads.
      * `UOneBrahamMatchSettingsAsset`'s default `WeaponSpawnTable` - the table
        the floor loot actually rolls - listed `Pistol_Glock17`, `SMG_MP5A3`,
        `Carbine_L85A2`, ..., a naming scheme the registry has never contained.
        Every loot roll in the match asked the registry for a weapon that does
        not exist and got nothing back, and its floor gates (marksman from 9,
        SMG capped at 13) disagreed with the fine weapon system document, which
        says PDW from 3, MCX from 5, semi-auto 12 and marksman from 6.
      * The default loadout, the saved loadout and `ob.give` all named
        `Carbine_L85A2`, so every player's first weapon resolved to nothing.

    None of that failed anything. A missing weapon is not an error, it is an
    empty hand.

WHAT IT CHECKS
    1. Every weapon id that appears anywhere in `Source/` is one the armoury
       table declares. That covers the spawn table, the defaults, the console
       and the tests, because the id shape is the check.
    2. The `WeaponSpawnTable` default in `OneBrahamMatchSettingsAsset.cpp` lists
       the armoury's ids, in the armoury's order, with the armoury's weights and
       floor gates.
    3. The armoury table in `Docs/05_Weapon_System.md` section 1 lists the same
       ids, names, damage, pellet counts, RPM, magazine and reserve values, in
       the same order.
    4. The floor-gate table in section 4 lists the same ids, floors and loot
       weights.
    5. The definition class declares the fields the builder writes onto it
       (`weapon_id`, `loot_weight`, `min_floor`, `max_floor`), so a rename in the
       header cannot silently turn every generated asset's loot data into a
       no-op.

WHAT IT IGNORES
    Utility ids (`Utility_Frag`, `Access_OverrideTool`), which are handled by the
    inventory component and are not registry weapons; and any id inside a comment,
    because a comment is not a lookup.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_weapon_ids.py
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

python_root = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

SOURCE_ROOT = os.path.join(python_root, "Source")
BUILDER = os.path.join(python_root, "Tools", "Python", "onebraham", "build_weapons.py")
SPAWN_TABLE = os.path.join(python_root, "Source", "OneBraham", "Data",
                           "OneBrahamMatchSettingsAsset.cpp")
DEFINITION_HEADER = os.path.join(python_root, "Source", "OneBraham", "Data",
                                 "OneBrahamWeaponDataAsset.h")
DOCS_05 = os.path.join(python_root, "Docs", "05_Weapon_System.md")

#: An id literal, as C++ writes one. `TEXT("W_SMG")`.
ID_LITERAL = re.compile(r'TEXT\s*\(\s*"([A-Za-z][A-Za-z0-9_]*)"\s*\)')

#: What a weapon id looks like. The W_ prefix is the shipping scheme; the five
#: descriptive families are the scheme that was replaced, and are matched
#: deliberately so that re-introducing one is a failure rather than a rename.
ID_SHAPE = re.compile(
    r'^(?:W_[A-Za-z0-9]+|(?:Pistol|SMG|PDW|Shotgun|Carbine|Rifle|Marksman)_[A-Za-z0-9_]+)$')

#: One builder entry: dict(id="W_SMG", name="MP5 9mm", ... damage=25.0, ...).
BUILDER_ENTRY = re.compile(r'dict\(\s*id="([A-Za-z0-9_]+)"', re.MULTILINE)

#: The spawn-table rows: AddEntry(TEXT("W_SMG"), 1.1f, 0, 15, {...});
SPAWN_ROW = re.compile(
    r'AddEntry\(\s*TEXT\("([A-Za-z0-9_]+)"\)\s*,\s*([0-9.]+)f\s*,\s*(-?[0-9]+)\s*,\s*(-?[0-9]+)\s*,')

#: A Docs/05 armoury row: | 4 | `W_SMG` | MP5 9mm | SMG | Primary | 25.0 | 800 | 30 | 120 | ... |
DOC_ARMOURY_ROW = re.compile(
    r'^\|\s*([0-9]+)\s*\|\s*`(W_[A-Za-z0-9_]+)`\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|'
    r'\s*([0-9.]+)(?:\s*×\s*([0-9]+))?\s*\|\s*([0-9]+)\s*\|\s*([0-9]+)\s*\|\s*([0-9]+)\s*\|',
    re.MULTILINE)

#: A Docs/05 floor-gate row: | MP5 9mm | `W_SMG` | all | 1.1 |
#: The id cell may name several weapons that share a row and a fate (the three
#: sidearms do), so the cell is captured whole and expanded afterwards.
DOC_GATE_ROW = re.compile(
    r'^\|\s*[^|]+\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*([0-9.,\s]+)\s*\|',
    re.MULTILINE)

BACKTICKED_ID = re.compile(r'`(W_[A-Za-z0-9_]+)`')

#: field on the builder side -> field on the C++ side (the Python editor API
#: takes snake_case for a PascalCase UPROPERTY, so this is the real mapping).
BUILDER_FIELDS = {
    "weapon_id": "WeaponId",
    "loot_weight": "LootWeight",
    "min_floor": "MinFloor",
    "max_floor": "MaxFloor",
}


def _float(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


def read(path):
    if not os.path.isfile(path):
        return ""
    return io.open(path, encoding="utf-8", errors="ignore").read()


def armoury():
    """The ten weapons, in table order: list of dicts."""
    text = read(BUILDER)

    if "WEAPONS = [" not in text:
        return []

    block = text[text.index("WEAPONS = ["):]
    block = block[:block.index("\n]\n")]

    weapons = []

    for entry in block.split("    dict(\n")[1:]:
        def value(key, cast=str):
            match = re.search(key + r'=("?[^,\n]+?"?)(?:,|\n)', entry)
            if match is None:
                return None
            raw = match.group(1).strip().strip('"')
            return cast(raw) if cast is not str else raw

        weapon = {
            "id": value("id"),
            "name": value("name"),
            "category": value("category"),
            "slot": value("slot"),
            "damage": _float(value("damage")),
            "pellets": int(_float(value("pellets")) or 1),
            "rpm": _float(value("rpm")),
            "mag": int(_float(value("mag")) or 0),
            "reserve": int(_float(value("reserve")) or 0),
            "weight": _float(value("weight")),
        }

        floors = re.search(r'floors=\(([0-9]+),\s*([0-9]+)\)', entry)
        weapon["min_floor"] = int(floors.group(1)) if floors else None
        weapon["max_floor"] = int(floors.group(2)) if floors else None

        weapons.append(weapon)

    return weapons


def source_id_literals(source_root=None):
    """(relative_path, line, id) for every weapon-shaped id literal in Source/."""
    root = source_root or SOURCE_ROOT
    out = []

    if not os.path.isdir(root):
        return out

    for dirpath, _dirs, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith((".h", ".cpp")):
                continue

            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, python_root)

            # strip_comments, not strip_comments_and_strings: the literals ARE
            # the thing being checked here, and blanking them made this rule
            # find zero ids and pass by doing nothing. (Sabotage-tested since.)
            clean = C.strip_comments(read(path))

            for number, line in enumerate(clean.splitlines(), 1):
                for literal in ID_LITERAL.findall(line):
                    if ID_SHAPE.match(literal):
                        out.append((rel, number, literal))

    return out


def spawn_rows():
    """[(id, weight, min_floor, max_floor)] from the match settings default."""
    return [(m.group(1), _float(m.group(2)), int(m.group(3)), int(m.group(4)))
            for m in SPAWN_ROW.finditer(read(SPAWN_TABLE))]


def doc_armoury_rows():
    """[(id, name, damage, pellets, rpm, mag, reserve)] from Docs/05 section 1."""
    out = []

    for match in DOC_ARMOURY_ROW.finditer(read(DOCS_05)):
        number, weapon_id, name, _class, _slot, damage, pellets, rpm, mag, reserve = match.groups()
        out.append((weapon_id, name,
                    _float(damage), int(pellets) if pellets else 1,
                    _float(rpm), int(mag), int(reserve)))

    return out


def doc_gate_rows():
    """[(id, floors_text, weight)] from Docs/05 section 4, one per weapon."""
    out = []

    for match in DOC_GATE_ROW.finditer(read(DOCS_05)):
        ids = BACKTICKED_ID.findall(match.group(1))
        floors_text = match.group(2).strip()
        weights = [_float(part) for part in match.group(3).split(",")]

        if not ids:
            continue

        if len(weights) != len(ids):
            # A row with three ids and one weight is a row this check cannot
            # read; report it rather than skipping it quietly.
            out.append((ids[0], floors_text, None))
            continue

        for weapon_id, weight in zip(ids, weights):
            out.append((weapon_id, floors_text, weight))

    return out


def run(source_root=None):
    """Returns (checks, problems)."""
    weapons = armoury()
    problems = []
    checks = 0

    if not weapons:
        return 0, [(u"the armoury table in build_weapons.py was not found or has moved",
                    u"parse failure")]

    by_id = dict((w["id"], w) for w in weapons)

    # 1. Every id the source uses exists.
    for rel, line, literal in source_id_literals(source_root):
        checks += 1

        if literal not in by_id:
            problems.append((
                u"{0}:{1} - '{2}' is not one of the ten weapons build_weapons.py declares "
                u"(a weapon id that resolves to nothing is an empty hand, and it fails "
                u"nothing)".format(rel, line, literal), u"unknown weapon id"))

    # 2. The spawn table is the armoury, in order, with the same numbers.
    rows = spawn_rows()
    checks += 1

    if len(rows) != len(weapons):
        problems.append((
            u"the WeaponSpawnTable default lists {0} weapons and the armoury has "
            u"{1}".format(len(rows), len(weapons)), u"spawn table"))
    else:
        for index, (row, weapon) in enumerate(zip(rows, weapons)):
            row_id, weight, min_floor, max_floor = row
            checks += 1

            if row_id != weapon["id"]:
                problems.append((
                    u"WeaponSpawnTable row {0} is '{1}' and the armoury's is "
                    u"'{2}'".format(index + 1, row_id, weapon["id"]), u"spawn table"))
                continue

            if weight is None or abs(weight - weapon["weight"]) > 0.001:
                problems.append((
                    u"'{0}' has loot weight {1} in the spawn table and {2} in the "
                    u"armoury".format(row_id, weight, weapon["weight"]), u"spawn table"))

            if (min_floor, max_floor) != (weapon["min_floor"], weapon["max_floor"]):
                problems.append((
                    u"'{0}' is gated to {1}-{2} in the spawn table and {3}-{4} in the "
                    u"armoury".format(row_id, min_floor, max_floor,
                                      weapon["min_floor"], weapon["max_floor"]),
                    u"spawn table"))

    # 3. The weapon system document's armoury table.
    doc_rows = doc_armoury_rows()
    checks += 1

    if len(doc_rows) != len(weapons):
        problems.append((
            u"Docs/05 lists {0} weapons and the armoury has {1}".format(len(doc_rows), len(weapons)),
            u"documented armoury"))
    else:
        for index, (row, weapon) in enumerate(zip(doc_rows, weapons)):
            doc_id, doc_name, doc_damage, doc_pellets, doc_rpm, doc_mag, doc_reserve = row
            checks += 1

            if doc_id != weapon["id"]:
                problems.append((
                    u"Docs/05 row {0} is '{1}' and the armoury's is '{2}'".format(
                        index + 1, doc_id, weapon["id"]), u"documented armoury"))
                continue

            for label, documented, real in (
                    ("name", doc_name, weapon["name"]),
                    ("damage", doc_damage, weapon["damage"]),
                    ("pellet count", doc_pellets, weapon["pellets"]),
                    ("rpm", doc_rpm, weapon["rpm"]),
                    ("magazine", doc_mag, weapon["mag"]),
                    ("reserve", doc_reserve, weapon["reserve"])):
                checks += 1

                mismatch = (documented != real) if isinstance(real, str) else \
                    (documented is None or abs(documented - real) > 0.001)

                if mismatch:
                    problems.append((
                        u"Docs/05 gives '{0}' {1} = {2} and the armoury says {3}".format(
                            doc_id, label, documented, real), u"documented armoury"))

    # 4. The floor gates, which are the part a reader plans a route around.
    gates = doc_gate_rows()
    checks += 1

    if not gates:
        problems.append((
            u"Docs/05 section 4 no longer has the floor-gate table in the checked "
            u"form", u"documented gates"))
    else:
        for weapon_id, floors_text, weight in gates:
            checks += 1

            if weapon_id not in by_id:
                problems.append((
                    u"Docs/05 gates '{0}', which is not one of the ten".format(weapon_id),
                    u"documented gates"))
                continue

            weapon = by_id[weapon_id]

            if weight is None or abs(weight - weapon["weight"]) > 0.001:
                problems.append((
                    u"Docs/05 gives '{0}' a loot weight of {1} and the armoury says "
                    u"{2}".format(weapon_id, weight, weapon["weight"]), u"documented gates"))

            # "all" means floor 0 to the top; "from N" means N to the top.
            expected = u"all" if weapon["min_floor"] == 0 else u"from {0}".format(weapon["min_floor"])

            if floors_text.lower() != expected:
                problems.append((
                    u"Docs/05 gates '{0}' '{1}' and the armoury says '{2}'".format(
                        weapon_id, floors_text, expected), u"documented gates"))

    # 5. The fields the builder writes onto the definition assets.
    header = read(DEFINITION_HEADER)
    checks += 1

    if not header:
        problems.append((u"the weapon definition header was not found", u"missing file"))
    else:
        for builder_field, cpp_field in sorted(BUILDER_FIELDS.items()):
            checks += 1

            if not re.search(r'\b' + cpp_field + r'\b', header):
                problems.append((
                    u"build_weapons.py writes '{0}' onto every definition and "
                    u"UOneBrahamWeaponDefinition declares no {1} - the write is a silent "
                    u"no-op".format(builder_field, cpp_field), u"definition field"))

    return checks, problems


class _WeaponReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} weapon-id checks, {1} problems ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} weapon-id checks, the armoury, the spawn table and Docs/05 agree".format(
            self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()
    weapons = armoury()

    C.log(u"")
    C.log(u"  weapon-id checks: {0} ({1} weapons in the armoury)".format(checked, len(weapons)))

    if not problems:
        C.log(u"  the armoury, the spawn table, the defaults and Docs/05 are one list. OK")
    else:
        for message, kind in problems:
            C.log(u"      [{0}] {1}".format(kind, message))

    return _WeaponReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - the armoury")
    C.log(u"=" * 70)
    C.log(u"  checks: {0}".format(checked))

    if not problems:
        C.log(u"  every weapon id agrees. OK")
        return 0

    C.log(u"  PROBLEMS: {0}".format(len(problems)))
    for message, kind in problems:
        C.log(u"  [{0}] {1}".format(kind, message))

    return 1


if __name__ == "__main__":
    sys.exit(main())
