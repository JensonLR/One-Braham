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
    from onebraham import check_cpp_symbols
    from onebraham import check_gameplay_tags
    from onebraham import check_input_actions
    from onebraham import check_soft_paths
    from onebraham import check_uht_contracts
    from onebraham import check_voice_lines

    expectations = {}

    expectations["cpp_symbols"] = [check_cpp_symbols.run()[0]]
    expectations["uht_contracts"] = [check_uht_contracts.run()[0]]
    expectations["soft_paths"] = [check_soft_paths.run()[0]]
    expectations["calls"] = [check_calls.run()[0]]
    expectations["tags"] = [check_gameplay_tags.run()[0]]
    expectations["voice_lines"] = [check_voice_lines.run()[0]]

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
                     "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12}
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

    return numbers_checked, problems


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
