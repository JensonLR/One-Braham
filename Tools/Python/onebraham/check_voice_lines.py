# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_voice_lines
==========================

Checks that every voice line the code asks for exists, and that every line that
exists is in the script.

WHY THIS EXISTS
    A voice line id is a string. `UOneBrahamVoiceSubsystem::SpeakContextualLine`
    looks it up in the line table, does not find it, logs at Verbose and returns
    false. Nothing is said, nothing is shown, and no test fails - the character
    simply has nothing to say, in a game where the character having something to
    say is most of the characterisation.

    This nearly shipped. The comms wheel's first draft named eight line ids,
    seven of which had been invented on the spot: `V_PUSHING`, `V_HOLDING`,
    `V_RELOADING_COVER`, `V_MOVING_UP`, `V_MAN_DOWN_ONE`, `V_GOING_QUIET` and
    `V_NEVER_MIND`. The script has `V_PUSH_NOW`, `V_HOLD_POSITION`, `V_RELOADING`,
    `V_PING_MOVE`, `V_QUIET` and `V_ACK_NEGATIVE`, and it did not have a
    bystander's man-down callout at all. Seven invented ids would have produced a
    wheel where seven of the eight segments did nothing, and it would have looked
    correct in every screenshot.

WHAT IT CHECKS
    1. Every `V_` id referenced in the C++ outside the line table is declared in
       the line table. This is the run time failure above.
    2. Every id declared in the line table appears in `Docs/14_Voice_Script.md`.
       The document is the recording brief: a line that exists in code and not in
       the brief is a line nobody will ever record.
    3. No id is declared twice, which would make the second variant unreachable
       when the first is picked.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_voice_lines.py
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

python_root = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

SOURCE_ROOT = os.path.join(python_root, "Source", "OneBraham")
LINE_TABLE = os.path.join(SOURCE_ROOT, "Voice", "OneBrahamVoiceLineSet.cpp")
VOICE_DOC = os.path.join(python_root, "Docs", "14_Voice_Script.md")

#: A line declared in the table: Line(TEXT("V_ID"), ...)
DECLARED = re.compile(r'\bLine\(\s*TEXT\("(V_[A-Z0-9_]+)"\)')

#: An archetype variant row: LineFor(TEXT("V_ID"), OneBrahamTags::Character_Archetype_X, ...)
VARIANT = re.compile(
    r'\bLineFor\(\s*TEXT\("(V_[A-Z0-9_]+)"\)\s*,\s*'
    r'OneBrahamTags::(Character_Archetype_[A-Za-z0-9_]+)\s*,')

#: The six archetype tags the character system declares. A filter naming anything
#: else never matches, so the variant silently falls back to the generic
#: recording - the same failure as an invented line id, one level down.
ARCHETYPE_TAGS = {
    "Character_Archetype_BusinessAnalyst",
    "Character_Archetype_Graduate",
    "Character_Archetype_FinanceBroker",
    "Character_Archetype_ITOperations",
    "Character_Archetype_FacilitiesSecurity",
    "Character_Archetype_Consultant",
}

#: How many variant rows the recording brief promises.
DOCUMENTED_VARIANTS = 66

#: Any line id anywhere: TEXT("V_ID") or "V_ID" inside a comparison.
REFERENCED = re.compile(r'"(V_[A-Z0-9_]+)"')


def line_table_path(source_root=None):
    """The line table, inside whatever tree the caller pointed at.

    Every reader here goes through this. Twice during development a reader used
    the module's own path instead, so a sabotage test copied the tree, broke a
    line id and the check reported nothing - a test that passes because it is
    looking at the wrong file is worse than no test.
    """
    root = source_root or SOURCE_ROOT

    return os.path.join(root, "Voice", "OneBrahamVoiceLineSet.cpp")


def declared_lines(source_root=None):
    """id -> number of declarations, from the table."""
    path = line_table_path(source_root)

    if not os.path.isfile(path):
        return {}

    text = io.open(path, encoding="utf-8", errors="ignore").read()
    text = re.sub(r'//[^\n]*', "", text)

    counted = {}

    for line_id in DECLARED.findall(text):
        counted[line_id] = counted.get(line_id, 0) + 1

    return counted


def variant_rows(source_root=None):
    """[(line id, archetype constant)] for every archetype variant row."""
    path = line_table_path(source_root)

    if not os.path.isfile(path):
        return []

    text = io.open(path, encoding="utf-8", errors="ignore").read()
    text = re.sub(r'//[^\n]*', "", text)

    return VARIANT.findall(text)


def referenced_lines(source_root=None):
    """(file, line number, id) for every reference outside the line table."""
    root = source_root or SOURCE_ROOT

    if not os.path.isdir(root):
        return []

    references = []

    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith((".cpp", ".h")):
                continue

            path = os.path.join(dirpath, name)

            # The table is where lines are declared; every other file is asking.
            if os.path.normpath(path) == os.path.normpath(line_table_path(source_root)):
                continue

            raw = io.open(path, encoding="utf-8", errors="ignore").read()

            # Block comments carry examples - `e.g. "V_BA_CONTACT_ENEMY_02"` in the
            # line struct's own documentation was the first thing this check
            # reported, and it is not a line anybody asks for. Blank them out
            # first, preserving line count so the numbers still mean something.
            raw = re.sub(r'/\*.*?\*/', lambda m: "\n" * m.group(0).count("\n"), raw, flags=re.DOTALL)

            rel = os.path.relpath(path, root)

            for number, line in enumerate(raw.splitlines(), 1):
                code = line.split("//", 1)[0]

                for line_id in REFERENCED.findall(code):
                    references.append((rel, number, line_id))

    return references


def documented_lines():
    """The ids the voice script document lists."""
    if not os.path.isfile(VOICE_DOC):
        return set()

    text = io.open(VOICE_DOC, encoding="utf-8", errors="ignore").read()

    return set(re.findall(r'\b(V_[A-Z0-9_]+)\b', text))


def run(source_root=None):
    """Returns (references_checked, problems).

    source_root exists so the check can be pointed at a copy of the tree, which
    is how it was tested: the first version of it read the real source no matter
    what the caller passed, and a sabotage test that had no effect is a sabotage
    test that proves nothing.
    """
    declared = declared_lines(source_root)

    if not declared:
        return 0, [(u"no voice lines were parsed out of OneBrahamVoiceLineSet.cpp - the "
                    u"table was renamed or reformatted", None)]

    references = referenced_lines(source_root)
    problems = []

    for rel, number, line_id in references:
        if line_id not in declared:
            problems.append((
                u"{0}:{1} - '{2}' is requested but the script has no such line - "
                u"the character says nothing, silently".format(rel, number, line_id),
                u"undefined line"))

    # Duplicates: two variants of one id are fine (that is how the archetype
    # filter works), but the same id declared twice with the same shape is a
    # copy-paste, and the second one is unreachable in practice.
    for line_id, count in sorted(declared.items()):
        if count > 1:
            problems.append((u"'{0}' is declared {1} times in the line table".format(line_id, count),
                             u"duplicate line"))

    # --- Archetype variants -------------------------------------------------
    # Eleven lines are recorded six times each, and each of those is a row in the
    # table with an archetype filter. A filter naming a tag that does not exist
    # never matches, so the variant falls back to the generic recording and
    # nobody notices that a paid-for performance is unreachable.
    variants = variant_rows(source_root)
    seen_pairs = {}

    for line_id, archetype in variants:
        if archetype not in ARCHETYPE_TAGS:
            problems.append((
                u"'{0}' has a variant recorded for '{1}', which is not one of the six archetype "
                u"tags - the filter can never match and the variant will never play".format(
                    line_id, archetype),
                u"unknown archetype"))
            continue

        seen_pairs.setdefault(line_id, set()).add(archetype)

    for line_id, archetypes in sorted(seen_pairs.items()):
        if len(archetypes) != len(ARCHETYPE_TAGS):
            missing = sorted(ARCHETYPE_TAGS - archetypes)
            problems.append((
                u"'{0}' has {1} of 6 archetype variants (missing {2}) - those voices fall back to "
                u"the generic recording".format(line_id, len(archetypes), ", ".join(missing)),
                u"incomplete variants"))

    if len(variants) != DOCUMENTED_VARIANTS:
        problems.append((
            u"the table has {0} archetype variant rows; Docs/14 section 4 promises {1}".format(
                len(variants), DOCUMENTED_VARIANTS),
            u"variant count"))

    # Documentation coverage: the script is the recording brief.
    documented = documented_lines()

    for line_id in sorted(declared):
        if line_id not in documented:
            problems.append((
                u"'{0}' exists in the line table and not in Docs/14_Voice_Script.md - "
                u"nobody will record it".format(line_id),
                u"undocumented line"))

    return len(references), problems


class _VoiceReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} line references, {1} problem ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} line references, every line declared and documented".format(self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()

    C.log(u"")
    C.log(u"  voice line references checked: {0}".format(checked))

    if not problems:
        C.log(u"  every line the code asks for is in the script, and every line in the "
              u"script is in the recording brief. OK")
        return _VoiceReport(checked, problems)

    C.log(u"  PROBLEM: {0}".format(len(problems)))
    for message, _kind in problems:
        C.log(u"      {0}".format(message))

    return _VoiceReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - voice lines: what the code says against what the script has")
    C.log(u"=" * 70)
    C.log(u"  references checked: {0}".format(checked))

    if not problems:
        C.log(u"  agree. OK")
        return 0

    C.log(u"  PROBLEM: {0}".format(len(problems)))
    for message, _kind in problems:
        C.log(u"  {0}".format(message))

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
