# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_cosmetic_ids
============================

Checks that the wardrobe is one wardrobe.

WHY THIS EXISTS
    Cosmetics are the entire monetisation model of this game, and the ids are
    passed around as `FName`s, which means a wrong one is not an error: it is a
    string that matches nothing. Three places describe the wardrobe:

      * `build_characters.py`: `COSMETIC_SLOTS` (twelve slots, with the bone and
        offset each attaches at) and `COSMETIC_OPTIONS` (the 51 options), which
        is what the registry asset is generated from and what Docs/13 documents.
      * The compiled-in default in `UOneBrahamCharacterDataAsset`, which is what
        a fresh clone with no built content runs on. It carried **five** of the
        twelve slots, so `ApplyCosmetics` - which drops any pair whose slot it
        cannot find - silently applied the lanyard and the badge and threw away
        the jacket, trousers, shoes, fleece, hi-vis, bag tag, gloves and watch.
      * The starter outfit in `UOneBrahamGameInstance`, which carried seven ids
        (`Head_ShortBackAndSides`, `Outerwear_Softshell_Navy`, ...) in a naming
        scheme no wardrobe has ever contained, so a fresh profile unlocked
        nothing a player could wear.

    None of it failed anything. A cosmetic that resolves to nothing is a player
    wearing exactly what they spawned in.

WHAT IT CHECKS
    1. Every cosmetic option id in `Source/` is an option the wardrobe declares.
       The families (`Jacket_`, `Lanyard_`, `Badge_`, `Hat_`, ...) are the tell.
    2. Every `SlotName = TEXT("...")` in `Source/` is a slot the wardrobe
       declares, because a slot name is a key in `EquippedCosmetics`.
    3. The compiled-in `CosmeticSlots` default is the builder's twelve slots -
       same names, same order, same parent bones, same offsets, same
       `bHiddenWhenArmed`.
    4. The starter outfit is made of real options, includes the lanyard and the
       badge (the two the game's own systems read: the lobby reads the tier off
       the badge), and has no duplicates.
    5. `Docs/13_Characters_Clothing.md` section 3 lists the same twelve slots
       with the same option counts, because that table is what a reader plans
       the wardrobe against.

WHAT IT IGNORES
    Option ids inside comments, mesh paths and the `Vox_*` voice-set ids.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_cosmetic_ids.py
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
BUILDER = os.path.join(python_root, "Tools", "Python", "onebraham", "build_characters.py")
CHARACTER_ASSET = os.path.join(python_root, "Source", "OneBraham", "Data",
                               "OneBrahamCharacterDataAsset.cpp")
GAME_INSTANCE = os.path.join(python_root, "Source", "OneBraham", "Core",
                             "OneBrahamGameInstance.cpp")
DOCS_13 = os.path.join(python_root, "Docs", "13_Characters_Clothing.md")
CATALOGUE = os.path.join(python_root, "Source", "OneBraham", "Data",
                         "OneBrahamCosmeticDataAsset.cpp")
STEAM = os.path.join(python_root, "Source", "OneBraham", "Online",
                     "OneBrahamSteamSubsystem.cpp")
GAME_INI = os.path.join(python_root, "Config", "DefaultGame.ini")

#: A catalogue row: AddUnlock(TEXT("Jacket_Bomber"), TEXT("OB.Cosmetic.Unlock..."), ...
CATALOGUE_ROW = re.compile(r'AddUnlock\(TEXT\("([A-Za-z0-9_]+)"\),\s*TEXT\("([A-Za-z0-9_.]+)"\)')

#: An archetype's default outfit in build_characters.py.
ARCHETYPE_OUTFIT = re.compile(r'"cosmetics":\s*\{(.*?)\}', re.S)

#: Every cosmetic unlock tag a source names.
UNLOCK_TAG = re.compile(r'"(OB\.Cosmetic\.Unlock\.[A-Za-z0-9_.]+)"')

#: A cosmetic option id: Family_Thing. The families are the wardrobe's slots'
#: own id prefixes, and `Hat_` is deliberately here because the Headwear slot
#: stores `Hat_*` ids.
OPTION_ID = re.compile(
    r'\b((?:Jacket|Fleece|HiVis|Trousers|Shoes|Lanyard|Badge|Bag|BagTag|Hat|Gloves|Watch)_[A-Za-z0-9_]+)\b')

#: The families in order, so a mismatch can name the slot that owns it.
FAMILY_SLOT = {
    "Jacket": "Jacket",
    "Fleece": "Fleece",
    "HiVis": "HiVis",
    "Trousers": "Trousers",
    "Shoes": "Shoes",
    "Lanyard": "Lanyard",
    "Badge": "Badge",
    "Bag": "Bag",
    "BagTag": "BagTag",
    "Hat": "Headwear",
    "Gloves": "Gloves",
    "Watch": "Watch",
}

SLOT_ROW = re.compile(
    r'\(\s*"([A-Za-z]+)",\s*"([^"]+)",\s*"([A-Za-z_0-9]+)",\s*'
    r'\(\s*(-?[0-9.]+)\s*,\s*(-?[0-9.]+)\s*,\s*(-?[0-9.]+)\s*\)\s*,\s*(True|False)\s*\)')

OPTION_LIST = re.compile(r'"([A-Za-z]+)":\s*\[(.*?)\]', re.S)

#: The C++ side of one slot in the compiled-in default.
CPP_SLOT = re.compile(
    r'(\w+)\.SlotName = TEXT\("([A-Za-z]+)"\);\s*'
    r'\s*\1\.DisplayName = NSLOCTEXT\([^,]+,\s*"[^"]+",\s*"([^"]+)"\);\s*'
    r'\s*\1\.ParentBone = TEXT\("([A-Za-z_0-9]+)"\);\s*'
    r'\s*\1\.RelativeOffset = FVector\((-?[0-9.]+)f,\s*(-?[0-9.]+)f,\s*(-?[0-9.]+)f\);\s*'
    r'\s*\1\.bHiddenWhenArmed = (true|false);', re.S)

#: The starter outfit: the initialiser list after `StarterCosmetics`.
STARTER = re.compile(r'const TArray<FName> StarterCosmetics = \{(.*?)\};', re.S)

#: The wardrobe section of Docs/13. Scanning the whole document for
#: "| name | number |" rows finds the cloth budget table too, which is how the
#: first version of this check reported "cloth assets simulating at once" as a
#: cosmetic slot.
DOC_SECTION = re.compile(r'## 3\. Cosmetic slots and options(.*?)(?=\n## )', re.S)

#: A Docs/13 slot row: | Jacket | 8 | ... |
DOC_SLOT_ROW = re.compile(r'^\|\s*([A-Za-z][A-Za-z -]*?)\s*\|\s*([0-9]+)\s*\|', re.MULTILINE)

#: The document writes these names for those slots.
DOC_SLOT_NAMES = {
    "HiVis": "Hi-vis",
    "BagTag": "Bag tag",
}


def read(path):
    if not os.path.isfile(path):
        return ""
    return io.open(path, encoding="utf-8", errors="ignore").read()


def wardrobe():
    """(slots, options) from build_characters.py.

    slots: list of (name, display, bone, x, y, z, hidden)
    options: dict slot_name -> [option id]
    """
    text = read(BUILDER)

    if "COSMETIC_SLOTS = [" not in text or "COSMETIC_OPTIONS" not in text:
        return [], {}

    slot_block = text[text.index("COSMETIC_SLOTS = ["):]
    slot_block = slot_block[:slot_block.index("\n]\n")]

    slots = SLOT_ROW.findall(slot_block)

    option_block = text[text.index("COSMETIC_OPTIONS"):]
    option_block = option_block[:option_block.index("\n}\n")]

    options = {}

    for match in OPTION_LIST.finditer(option_block):
        options[match.group(1)] = re.findall(r'"([A-Za-z0-9_]+)"', match.group(2))

    return slots, options


def source_option_ids(source_root=None):
    """(relative_path, line, id) for every cosmetic option id in Source/."""
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

            # strip_comments, not strip_comments_and_strings: the ids are
            # literals, and blanking literals is how a check like this passes by
            # finding nothing.
            clean = C.strip_comments(read(path))

            for number, line in enumerate(clean.splitlines(), 1):
                for option in OPTION_ID.findall(line):
                    out.append((rel, number, option))

    return out


def source_slot_names(source_root=None):
    """(relative_path, line, slot) for every SlotName literal in Source/."""
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
            clean = C.strip_comments(read(path))

            for number, line in enumerate(clean.splitlines(), 1):
                match = re.search(r'SlotName\s*=\s*TEXT\("([A-Za-z]+)"\)', line)

                if match:
                    out.append((rel, number, match.group(1)))

    return out


def cpp_slots():
    """[(name, display, bone, x, y, z, hidden)] from the compiled-in default."""
    return [(m.group(2), m.group(3), m.group(4),
             float(m.group(5)), float(m.group(6)), float(m.group(7)),
             m.group(8) == "true")
            for m in CPP_SLOT.finditer(read(CHARACTER_ASSET))]


def starter_cosmetics():
    """[id] from the fresh-profile starter outfit."""
    match = STARTER.search(read(GAME_INSTANCE))

    if match is None:
        return None

    return re.findall(r'TEXT\("([A-Za-z0-9_]+)"\)', match.group(1))


def run(source_root=None):
    """Returns (checks, problems)."""
    slots, options = wardrobe()
    problems = []
    checks = 0

    if not slots or not options:
        return 0, [(u"the cosmetic tables in build_characters.py were not found or have "
                    u"moved", u"parse failure")]

    all_options = set()
    option_slot = {}

    for slot, ids in options.items():
        for option in ids:
            all_options.add(option)
            option_slot[option] = slot

    slot_names = [s[0] for s in slots]

    # 1. Every option id the source uses exists.
    for rel, line, option in source_option_ids(source_root):
        checks += 1

        if option not in all_options:
            family = option.split("_")[0]
            owner = FAMILY_SLOT.get(family, u"no slot")
            problems.append((
                u"{0}:{1} - '{2}' is not in the wardrobe (the {3} slot's options are in "
                u"build_characters.py COSMETIC_OPTIONS, and a cosmetic that resolves to "
                u"nothing is a player wearing what they spawned in)".format(
                    rel, line, option, owner), u"unknown cosmetic"))

    # 2. Every slot name in the source exists.
    for rel, line, slot in source_slot_names(source_root):
        checks += 1

        if slot not in slot_names:
            problems.append((
                u"{0}:{1} - '{2}' is not a cosmetic slot".format(rel, line, slot),
                u"unknown slot"))

    # 3. The compiled-in default is the builder's twelve.
    compiled = cpp_slots()
    checks += 1

    if len(compiled) != len(slots):
        problems.append((
            u"the compiled-in CosmeticSlots default has {0} slots and the wardrobe has "
            u"{1} - ApplyCosmetics drops a pair whose slot it cannot find, so the extra "
            u"slots' options are worn by nobody".format(len(compiled), len(slots)),
            u"compiled-in slots"))
    else:
        for index, (cpp, builder_slot) in enumerate(zip(compiled, slots)):
            name, display, bone, x, y, z, hidden = builder_slot
            cpp_name, cpp_display, cpp_bone, cx, cy, cz, cpp_hidden = cpp
            checks += 1

            if cpp_name != name:
                problems.append((
                    u"compiled-in slot {0} is '{1}' and the wardrobe's is '{2}'".format(
                        index + 1, cpp_name, name), u"compiled-in slots"))
                continue

            for label, cpp_value, builder_value in (
                    ("display name", cpp_display, display),
                    ("parent bone", cpp_bone, bone),
                    ("bHiddenWhenArmed", cpp_hidden, hidden == "True")):
                checks += 1

                if cpp_value != builder_value:
                    problems.append((
                        u"the compiled-in '{0}' slot has {1} '{2}' and the wardrobe says "
                        u"'{3}'".format(name, label, cpp_value, builder_value),
                        u"compiled-in slots"))

            for axis, cpp_value, builder_value in (
                    ("X", cx, float(x)), ("Y", cy, float(y)), ("Z", cz, float(z))):
                checks += 1

                if abs(cpp_value - builder_value) > 0.001:
                    problems.append((
                        u"the compiled-in '{0}' slot attaches at {1} = {2} and the wardrobe "
                        u"says {3}".format(name, axis, cpp_value, builder_value),
                        u"compiled-in slots"))

    # 4. The starter outfit.
    starter = starter_cosmetics()
    checks += 1

    if starter is None:
        problems.append((u"the starter cosmetic outfit was not found in "
                         u"UOneBrahamGameInstance - a fresh profile unlocks nothing",
                         u"starter outfit"))
    else:
        for option in starter:
            checks += 1

            if option not in all_options:
                problems.append((
                    u"the starter outfit unlocks '{0}', which is not in the wardrobe "
                    u"- a fresh profile unlocks nothing a player can wear".format(option),
                    u"starter outfit"))

        if len(set(starter)) != len(starter):
            problems.append((u"the starter outfit lists a cosmetic twice", u"starter outfit"))

        starter_slots = set(option_slot.get(option) for option in starter)

        for required, why in (("Lanyard", u"the lobby reads identity off it"),
                              ("Badge", u"the lobby reads the access tier off it")):
            checks += 1

            if required not in starter_slots:
                problems.append((
                    u"the starter outfit unlocks nothing in the {0} slot and {1}".format(
                        required, why), u"starter outfit"))

    # 5. The document's slot table.
    # 6. The catalogue of earned cosmetics: every locked id is a wardrobe
    #    option that nobody is issued (not a starter item, not an archetype's
    #    default outfit, or locking it would leave a slot bare), every tag is a
    #    registered tag, and every tag an achievement grants opens something.
    catalogue = CATALOGUE_ROW.findall(read(CATALOGUE))
    checks += 1
    if not catalogue:
        problems.append((u"no AddUnlock rows were found in OneBrahamCosmeticDataAsset.cpp",
                         u"catalogue"))
    issued = set(starter or [])
    for outfit in ARCHETYPE_OUTFIT.findall(read(BUILDER)):
        issued |= set(re.findall(r'"[A-Za-z]+":\s*"([A-Za-z0-9_]+)"', outfit))
    registered = set(re.findall(r'Tag="(OB\.Cosmetic\.Unlock\.[A-Za-z0-9_.]+)"', read(GAME_INI)))
    seen_ids = set()
    for cosmetic_id, tag in catalogue:
        checks += 1
        if cosmetic_id not in all_options:
            problems.append((u"the catalogue locks '{0}', which is not a wardrobe option"
                             .format(cosmetic_id), u"catalogue"))
        if cosmetic_id in issued:
            problems.append((u"the catalogue locks '{0}', which a starter outfit or an "
                             u"archetype default issues - locking it leaves that slot bare"
                             .format(cosmetic_id), u"catalogue"))
        if cosmetic_id in seen_ids:
            problems.append((u"the catalogue locks '{0}' twice".format(cosmetic_id), u"catalogue"))
        seen_ids.add(cosmetic_id)
        if tag not in registered:
            problems.append((u"the catalogue's tag '{0}' is not registered in "
                             u"Config/DefaultGame.ini".format(tag), u"catalogue"))
    catalogue_tags = set(tag for _id, tag in catalogue)
    for tag in sorted(set(UNLOCK_TAG.findall(read(STEAM)))):
        checks += 1
        if tag not in catalogue_tags:
            problems.append((u"an achievement grants '{0}' and no catalogue row opens "
                             u"anything with it".format(tag), u"catalogue"))

    section = DOC_SECTION.search(read(DOCS_13))
    doc_rows = DOC_SLOT_ROW.findall(section.group(1) if section else u"")
    expected = [(DOC_SLOT_NAMES.get(name, name), len(options.get(name, [])))
                for name in slot_names]

    checks += 1

    documented = dict((name.strip().lower(), int(count)) for name, count in doc_rows)
    missing = [name for name, _count in expected if name.lower() not in documented]
    unknown = [name for name in documented
               if name not in [n.lower() for n, _c in expected]]

    if missing or unknown:
        problems.append((
            u"Docs/13 section 3 lists {0} and the wardrobe declares {1}{2}".format(
                u", ".join(sorted(documented)) or u"nothing",
                u", ".join(name for name, _count in expected),
                u"" if not missing else u" (missing: {0})".format(u", ".join(missing))),
            u"documented slots"))
    else:
        for name, count in expected:
            checks += 1

            if documented[name.lower()] != count:
                problems.append((
                    u"Docs/13 gives the {0} slot {1} options and the wardrobe declares "
                    u"{2}".format(name, documented[name.lower()], count),
                    u"documented slots"))

    return checks, problems


class _CosmeticReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} cosmetic checks, {1} problems ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} cosmetic checks, one wardrobe".format(self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()
    slots, options = wardrobe()
    total = sum(len(ids) for ids in options.values())

    C.log(u"")
    C.log(u"  cosmetic checks: {0} ({1} slots, {2} options, {3} of 12 slots in the "
          u"compiled-in default)".format(checked, len(slots), total, len(cpp_slots())))

    if not problems:
        C.log(u"  the builder, the compiled-in default, the starter outfit and Docs/13 "
              u"are one wardrobe. OK")
    else:
        for message, kind in problems:
            C.log(u"      [{0}] {1}".format(kind, message))

    return _CosmeticReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - the wardrobe")
    C.log(u"=" * 70)
    C.log(u"  checks: {0}".format(checked))

    if not problems:
        C.log(u"  every slot and option agrees. OK")
        return 0

    C.log(u"  PROBLEMS: {0}".format(len(problems)))
    for message, kind in problems:
        C.log(u"  [{0}] {1}".format(kind, message))

    return 1


if __name__ == "__main__":
    sys.exit(main())
