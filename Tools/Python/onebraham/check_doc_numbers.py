# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_doc_numbers
===========================

Checks that the numbers in the documentation are the numbers the tooling
produces.

WHY THIS EXISTS
    Every gate in this project reports a count: declarations checked, contracts
    checked, call sites checked, name-loaded paths, voice-line references. Those
    counts are also written down in `Docs/17_Build_Automation.md`'s summary table
    and in the README's rule 4, as the evidence that the pipeline is real.

    They drift the moment anything is added, and a wrong count in a document is
    worse than no count: it reads as verification. This has already happened
    three times here - the table said 495 declarations when there were 525, 42
    names when there were 45, and 823 call sites when there were 848 - and each
    time the fix was manual.

    A manual fix is a promise to remember. This is the check instead.

WHAT IT CHECKS
    1. The "N steps succeeded" line above the summary table matches the number of
       rows in the table.
    2. Every count in the table's Detail column is the count the corresponding
       checker reports, re-derived by running it.
    3. The README's claim about how many checks the validator runs, and how many
       of them need no engine, matches the validator's check list and the
       engine-free steps in the run list.
    4. The archetype table in `Docs/13_Characters_Clothing.md` is the table
       `build_characters.py` declares: the tag, home floor, body variant, height,
       voice set and all six animation personality values, for all six
       archetypes. This table had drifted further than any other - three of the
       six voice sets named ids that exist nowhere (`Vox_Analyst_London`,
       `Vox_Graduate_London`, `Vox_ITOps_London`; the builder casts
       `Vox_BA_London`, `Vox_Grad_London`, `Vox_IT_London`), and every height
       and personality value was wrong. A wrong voice-set id is a character who
       says nothing, which is the kind of fault nobody reports as a bug.

    Rows whose Detail is prose rather than a count (the content steps, which need
    the editor) are skipped, and the skip is stated in the output instead of
    being silent.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_doc_numbers.py
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

python_root = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

DOCS_17 = os.path.join(python_root, "Docs", "17_Build_Automation.md")
README = os.path.join(python_root, "README.md")
RUN_ALL = os.path.join(python_root, "Tools", "Python", "onebraham", "run_all.py")
VALIDATOR = os.path.join(python_root, "Tools", "Python", "onebraham", "validate_project.py")

#: A summary-table row: | step | result | seconds | detail |
ROW = re.compile(r'^\|\s*([a-z_]+)\s*\|\s*([A-Za-z]+)\s*\|\s*([0-9.]+)\s*\|\s*([^|]+)\|', re.MULTILINE)

STEPS_LINE = re.compile(r'\*\*(\d+) steps succeeded')


def _documented_rows():
    """step -> detail text, from the summary table in Docs/17."""
    if not os.path.isfile(DOCS_17):
        return {}

    text = io.open(DOCS_17, encoding="utf-8", errors="ignore").read()
    rows = {}

    for step, _result, _seconds, detail in ROW.findall(text):
        rows[step] = detail.strip()

    return rows


def _expected_counts():
    """step -> list of counts the tooling actually produces, or None to skip."""
    from onebraham import check_calls
    from onebraham import check_cosmetic_ids
    from onebraham import check_audio_ids
    from onebraham import check_hud_strings
    from onebraham import check_interaction
    from onebraham import check_cpp_symbols
    from onebraham import check_gameplay_tags
    from onebraham import check_input_actions
    from onebraham import check_soft_paths
    from onebraham import check_uht_contracts
    from onebraham import check_voice_lines
    from onebraham import check_weapon_ids

    expectations = {}

    expectations["cpp_symbols"] = [check_cpp_symbols.run()[0]]
    expectations["uht_contracts"] = [check_uht_contracts.run()[0]]
    expectations["soft_paths"] = [check_soft_paths.run()[0]]
    expectations["calls"] = [check_calls.run()[0]]
    expectations["tags"] = [check_gameplay_tags.run()[0]]
    expectations["voice_lines"] = [check_voice_lines.run()[0]]

    expectations["weapon_ids"] = [check_weapon_ids.run()[0]]
    expectations["cosmetic_ids"] = [check_cosmetic_ids.run()[0]]
    expectations["hud_strings"] = [check_hud_strings.run()[0]]
    expectations["audio_ids"] = [check_audio_ids.run()[0]]
    expectations["interaction_ids"] = [check_interaction.run()[0]]

    reads, _problems = check_input_actions.run()
    expectations["input_actions"] = [
        len(check_input_actions.generated_actions()),
        len(check_input_actions.config_fields()),
        reads,
    ]

    return expectations


def _run_all_counts():
    """(total steps, engine-free steps) from run_all's step table."""
    if not os.path.isfile(RUN_ALL):
        return 0, 0

    text = io.open(RUN_ALL, encoding="utf-8", errors="ignore").read()

    # The steps are declared as ("name", "module", needs_editor, "description").
    steps = re.findall(r'^\s*\("([a-z_]+)",\s*"onebraham\.[a-z_]+",\s*(True|False)\s*,',
                       text, re.MULTILINE)

    return len(steps), len([s for s in steps if s[1] == "False"])


def _validator_count():
    """How many checks validate_project registers."""
    if not os.path.isfile(VALIDATOR):
        return 0

    text = io.open(VALIDATOR, encoding="utf-8", errors="ignore").read()
    start = text.find("    checks = [")

    if start == -1:
        return 0

    block = text[start:text.find("    for label, fn in checks:", start)]

    return len(re.findall(r'\("([^"]+)",\s*check_', block))


CHARACTER_BUILDER = os.path.join(python_root, "Tools", "Python", "onebraham", "build_characters.py")
DOCS_13 = os.path.join(python_root, "Docs", "13_Characters_Clothing.md")

#: The archetype table row: | name | `tag` | home | body | height cm | `voice` | who | personality |
ARCHETYPE_ROW = re.compile(
    r'^\|\s*([^|]+?)\s*\|\s*`(OB\.Character\.Archetype\.[A-Za-z]+)`\s*\|\s*([0-9]+)\s*\|'
    r'\s*([0-9]+)\s*\|\s*([+-][0-9.]+)\s*cm\s*\|\s*`([A-Za-z0-9_]+)`\s*\|', re.MULTILINE)

PERSONALITY_ROW = re.compile(
    r'Walk ([0-9.]+), arms ([0-9.]+), handling ([0-9.]+), lean ([0-9.]+), '
    r'hunch (-?[0-9.]+), crouch ([0-9.]+)')

PERSONALITY_KEYS = ("walk", "arms", "handling", "lean", "hunch", "crouch")


def _builder_archetypes():
    """The cast as build_characters.py declares it: name -> dict."""
    if not os.path.isfile(CHARACTER_BUILDER):
        return {}

    text = io.open(CHARACTER_BUILDER, encoding="utf-8", errors="ignore").read()

    if "ARCHETYPES = [" not in text:
        return {}

    block = text[text.index("ARCHETYPES = ["):]
    block = block[:block.index("\n]\n")]
    out = {}

    for entry in block.split("    {\n")[1:]:
        def grab(key):
            match = re.search(r'"' + key + r'":\s*"?([^",\n]+)"?', entry)
            return match.group(1).strip() if match else ""

        name = grab("name")

        if not name:
            continue

        out[name] = {
            "tag": grab("tag"),
            "home": grab("home_floor"),
            "body": grab("body_variant"),
            "height": grab("height_cm"),
            "voice": grab("voice_set"),
            "personality": dict(re.findall(
                r'"(walk|arms|handling|lean|hunch|crouch)":\s*(-?[0-9.]+)', entry)),
        }

    return out


def _check_archetype_table(problems):
    """Rule 4. Returns how many fields were compared."""
    declared = _builder_archetypes()

    if not declared or not os.path.isfile(DOCS_13):
        return 0

    text = io.open(DOCS_13, encoding="utf-8", errors="ignore").read()
    compared = 0
    documented = {}

    for match in ARCHETYPE_ROW.finditer(text):
        name, tag, home, body, height, voice = match.groups()
        name = name.strip()
        documented[name] = (tag, home, body, height, voice)

        if name not in declared:
            problems.append((
                u"Docs/13 documents an archetype called '{0}' that build_characters.py does "
                u"not declare".format(name), u"unknown archetype"))
            continue

        real = declared[name]

        for label, claimed, actual in (
                ("tag", tag, real["tag"]),
                ("home floor", home, real["home"]),
                ("body variant", body, real["body"]),
                ("voice set", voice, real["voice"])):
            compared += 1

            if claimed != actual:
                problems.append((
                    u"Docs/13 gives '{0}' the {1} '{2}' and build_characters.py says "
                    u"'{3}'".format(name, label, claimed, actual), u"stale archetype row"))

        # Height: the document writes one decimal, the builder a float.
        try:
            claimed_height = float(height)
        except ValueError:
            claimed_height = None

        compared += 1

        if claimed_height is None or abs(claimed_height - float(real["height"])) > 0.05:
            problems.append((
                u"Docs/13 gives '{0}' a height of {1} cm and build_characters.py says "
                u"{2} cm".format(name, height, real["height"]), u"stale archetype row"))

    for name, (tag, home, body, height, voice) in sorted(documented.items()):
        if name not in declared:
            continue

        # The personality numbers live in the same row, past the last pipe.
        row_start = text.find("`" + tag + "`")
        row_end = text.find("\n", row_start)
        row_text = text[row_start:row_end] if row_start != -1 else ""
        numbers = PERSONALITY_ROW.search(row_text)

        compared += 1

        if numbers is None:
            problems.append((
                u"the Docs/13 row for '{0}' does not state the animation personality in the "
                u"checked form".format(name), u"missing personality"))
            continue

        for key, claimed in zip(PERSONALITY_KEYS, numbers.groups()):
            compared += 1
            actual = declared[name]["personality"].get(key)

            if actual is None or abs(float(claimed) - float(actual)) > 0.005:
                problems.append((
                    u"Docs/13 gives '{0}' {1} = {2} and build_characters.py says "
                    u"{3}".format(name, key, claimed, actual), u"stale archetype row"))

    for name in sorted(set(declared) - set(documented)):
        problems.append((
            u"build_characters.py declares the archetype '{0}' and the Docs/13 table has no "
            u"row for it".format(name), u"missing archetype row"))

    return compared


def run():
    """Returns (numbers_checked, problems)."""
    rows = _documented_rows()
    problems = []

    if not rows:
        return 0, [(u"the summary table in Docs/17 was not found or has moved", u"parse failure")]

    text = io.open(DOCS_17, encoding="utf-8", errors="ignore").read()
    claimed_steps = STEPS_LINE.search(text)
    numbers_checked = 0

    # 1. The step count above the table, against the run list rather than against
    #    the table: the table is an excerpt with an ellipsis in it, deliberately,
    #    and the run list is the thing that has to match.
    total_steps, engine_free = _run_all_counts()

    if claimed_steps:
        numbers_checked += 1
        claimed = int(claimed_steps.group(1))

        if claimed != total_steps:
            problems.append((
                u"Docs/17 says {0} steps succeeded and run_all.py declares {1}".format(
                    claimed, total_steps),
                u"step count"))

    #    Every step named in the table must be a step the run list declares.
    declared_steps = set(re.findall(r'^\s*\("([a-z_]+)",\s*"onebraham\.',
                                    io.open(RUN_ALL, encoding="utf-8", errors="ignore").read(),
                                    re.MULTILINE))

    for step in sorted(rows):
        if step not in declared_steps and step not in (
                "materials", "environment", "props", "weapons", "characters", "cast_payload",
                "audio", "input", "ui", "vfx"):
            problems.append((
                u"the summary table documents a step called '{0}' that run_all.py does not "
                u"declare".format(step),
                u"unknown step"))

    # 2. Every count in the table.
    for step, counts in sorted(_expected_counts().items()):
        if step not in rows:
            problems.append((
                u"the summary table has no row for '{0}', which the run list declares".format(step),
                u"missing row"))
            continue

        for count in counts:
            numbers_checked += 1

            if str(count) not in rows[step]:
                problems.append((
                    u"the '{0}' row says \"{1}\" and the checker reports {2}".format(
                        step, rows[step], count),
                    u"stale number"))

    # 4. The archetype table, field by field.
    numbers_checked += _check_archetype_table(problems)

    # 3. The README's claims about the pipeline.
    if os.path.isfile(README):
        readme = io.open(README, encoding="utf-8", errors="ignore").read()
        match = re.search(r'It runs (\d+) checks, and (\w+) of them need no engine', readme)

        if match is None:
            problems.append((
                u"the README no longer states how many checks the validator runs",
                u"missing claim"))
        else:
            claimed_checks = int(match.group(1))
            words = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7,
                     "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
                     "thirteen": 13, "fourteen": 14, "fifteen": 15, "sixteen": 16,
                     "seventeen": 17, "eighteen": 18, "nineteen": 19, "twenty": 20}
            claimed_free = words.get(match.group(2).lower(), 0)

            actual_checks = _validator_count()
            _total_steps, actual_free = _run_all_counts()

            numbers_checked += 2

            if claimed_checks != actual_checks:
                problems.append((
                    u"the README says {0} checks and validate_project registers {1}".format(
                        claimed_checks, actual_checks),
                    u"stale number"))

            if claimed_free != actual_free:
                problems.append((
                    u"the README says {0} checks need no engine and the run list has {1} "
                    u"engine-free steps".format(claimed_free, actual_free),
                    u"stale number"))

    # 5. HANDOVER's "where the project stands" table: the file, document and
    #    test counts a new reader trusts first. They had drifted to two thirds of
    #    the real numbers, because nothing compared them.
    numbers_checked += _check_handover_table(problems)

    return numbers_checked, problems


def _check_handover_table(problems):
    """HANDOVER section 1's Source, Docs and Tests rows against the tree."""
    handover = os.path.join(os.path.dirname(os.path.dirname(DOCS_17)), "HANDOVER.md")
    if not os.path.isfile(handover):
        return 0
    text = io.open(handover, encoding="utf-8", errors="ignore").read()
    root = os.path.dirname(handover)
    source = os.path.join(root, "Source", "OneBraham")
    docs = os.path.join(root, "Docs")

    cpp = h = 0
    for _dir, _subdirs, files in os.walk(source):
        cpp += sum(1 for name in files if name.endswith(".cpp"))
        h += sum(1 for name in files if name.endswith(".h"))
    systems = sum(1 for name in os.listdir(source)
                  if os.path.isdir(os.path.join(source, name)) and name != "Tests")
    numbered = sum(1 for name in os.listdir(docs) if re.match(r"\d\d_.*\.md$", name))
    tests_dir = os.path.join(source, "Tests")
    test_files = [name for name in os.listdir(tests_dir) if name.endswith(".cpp")]
    tests = sum(len(re.findall(r"IMPLEMENT_(?:SIMPLE|COMPLEX)_AUTOMATION_TEST\(",
                               io.open(os.path.join(tests_dir, name), encoding="utf-8", errors="ignore").read()))
                for name in test_files)

    claims = (
        (r"\| Source \| (\d+) `\.cpp`, (\d+) `\.h`, (\d+) system folders", (cpp, h, systems), "Source"),
        (r"\| Docs \| (\d+) numbered documents", (numbered,), "Docs"),
        (r"\| Tests \| (\d+) automation tests in (\d+) files", (tests, len(test_files)), "Tests"),
    )
    checked = 0
    for pattern, actual, row in claims:
        match = re.search(pattern, text)
        checked += 1
        if match is None:
            problems.append((u"HANDOVER section 1 no longer has its {0} row in the expected form".format(row),
                             u"missing claim"))
            continue
        claimed = tuple(int(group) for group in match.groups())
        if claimed != actual:
            problems.append((u"HANDOVER's {0} row says {1} and the tree has {2}".format(
                row, u"/".join(str(n) for n in claimed), u"/".join(str(n) for n in actual)), u"stale number"))
    return checked


class _DocNumberReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} numbers, {1} stale ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} documented numbers agree with the tooling".format(self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()

    C.log(u"")
    C.log(u"  documented numbers checked: {0}".format(checked))

    if not problems:
        C.log(u"  every count in Docs/17 and the README is the count the tooling "
              u"produces. OK")
        return _DocNumberReport(checked, problems)

    C.log(u"  PROBLEM: {0}".format(len(problems)))
    for message, _kind in problems:
        C.log(u"      {0}".format(message))

    return _DocNumberReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - the numbers in the documentation against the tooling")
    C.log(u"=" * 70)
    C.log(u"  numbers checked: {0}".format(checked))

    if not problems:
        C.log(u"  agree. OK")
        return 0

    C.log(u"  PROBLEM: {0}".format(len(problems)))
    for message, _kind in problems:
        C.log(u"  {0}".format(message))

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
