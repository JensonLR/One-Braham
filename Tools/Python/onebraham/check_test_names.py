# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_test_names
==========================

Checks that the automation tests named in the documentation are the automation
tests that exist, and that the scripts that run them name tests this project has.

WHY THIS EXISTS
    `Docs/18_Testing_Performance.md` section 2 listed twelve test categories for
    most of this project's life, `Scripts/RunTests.bat` ran `OneBraham.*`, and
    `grep -rn IMPLEMENT_SIMPLE_AUTOMATION_TEST` over the entire repository matched
    one file: the document. Twelve suites of assertions that did not exist,
    quoted in tables, referred to by scripts, and reading - to anybody skimming -
    as verification.

    That is the same defect as a stale count or an invented voice line id, with
    more surface: a document that promises tests is a document that has to be
    compared against the source. The table now lists twenty tests across eight
    categories, all of them written, and this check is what keeps it honest.

WHAT IT CHECKS
    1. Every test the documentation lists exists in `Source/OneBraham/Tests/`,
       matched by category and test name.
    2. Every test in `Source/OneBraham/Tests/` is listed in the documentation -
       an undocumented test is a test nobody knows to run.
    3. Test names are unique, are under `OneBraham.`, and their category prefix
       is one the documentation lists.
    4. Every `OneBraham.*` name in `Scripts/` resolves: a full test name must
       exist, and a category name must have at least one test under it. This is
       what `RunTests.bat smoke` and the batch scripts' comments rely on.
    5. Every test file declares at least one test, and every file is named
       `OneBraham<Something>Tests.cpp`.
    6. Every `IMPLEMENT_SIMPLE_AUTOMATION_TEST` has a matching `RunTest` body.
       The macro's first argument is a class name it does not check; a typo there
       is a link error, or - worse - a test the framework never registers because
       its `RunTest` belongs to a class nobody instantiated.
    7. Every test file includes `Misc/AutomationTest.h`, is wrapped in
       `WITH_AUTOMATION_TESTS`, and gives its tests a context flag. A test with no
       context flag never runs, which looks exactly like a test that passes.

    Four more rules come from the same three failures seen from the compiler's
    side: a test whose `RunTest` body belongs to a differently spelled class (a
    link error, or a test the framework never registers), a file without
    `Misc/AutomationTest.h`, a file not wrapped in `WITH_AUTOMATION_TESTS`, and a
    declaration with no context flag. The last two fail silently in opposite
    directions - a suite that ships inside a shipping build, and a suite that never
    runs and therefore always passes.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_test_names.py
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

python_root = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

TESTS_DIR = os.path.join(python_root, "Source", "OneBraham", "Tests")
DOCS_18 = os.path.join(python_root, "Docs", "18_Testing_Performance.md")
SCRIPTS_DIR = os.path.join(python_root, "Scripts")

#: IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamCoreTest, "OneBraham.Core.Constants",
IMPLEMENTED = re.compile(
    r'IMPLEMENT_SIMPLE_AUTOMATION_TEST\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*,\s*"([^"]+)"')

#: The filter flags: UnrealHeaderTool's static_assert wants exactly one of these,
#: alongside exactly one context flag.
FILTER_FLAGS = ("EngineFilter", "ProductFilter", "SmokeFilter", "PerfFilter",
                "StressFilter", "NegativeFilter")

#: bool FOneBrahamCoreTest::RunTest(const FString& Parameters)
RUN_TEST = re.compile(r'bool\s+([A-Za-z_][A-Za-z0-9_]*)::RunTest\s*\(')

#: A "what runs today" table row: | `OneBraham.Core` | `A`, `B`, `C` | prose |
CATEGORY_ROW = re.compile(
    r'^\|\s*`(OneBraham\.[A-Za-z]+)`\s*\|\s*([^|]*?)\s*\|', re.MULTILINE)

#: Backticked identifiers, used to pull the test names out of that row's column.
BACKTICKED = re.compile(r'`([A-Za-z_][A-Za-z0-9_]*)`')

#: A line that actually runs tests: `Automation RunTests <filter>` from the
#: editor, or `RunTests.bat <filter>` / `RunTests.sh <filter>`.
SCRIPT_RUNS_TESTS = re.compile(r'(?:Automation\s+RunTests|RunTests\.(?:bat|sh))\s+(OneBraham[.A-Za-z]+)')

#: Test names end in a capitalised segment; `OneBraham.uproject` and
#: `OneBraham.exe` are files, and a check that reports those as missing tests is
#: a check somebody turns off.
SCRIPT_NAME = re.compile(r'^OneBraham(?:\.[A-Z][A-Za-z0-9_]*)+$')

FILE_NAME = re.compile(r'^OneBraham[A-Za-z0-9]*Tests\.cpp$')


#: IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClass, "Name", FLAGS)
#:
#: The flags argument holds no parentheses, so it can be captured up to the
#: closing bracket. There is deliberately no trailing semicolon in the pattern:
#: the macro does not take one, and requiring it made this matcher skip the
#: declaration and then match a `)` and `;` from a statement further down the
#: file - so it counted tests while reading the wrong text.
IMPLEMENTED_FULL = re.compile(
    r'IMPLEMENT_SIMPLE_AUTOMATION_TEST\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"([^"]+)"\s*,'
    r'\s*([^)]*)\)')

#: The flags argument of every declaration, for the context check.
IMPLEMENTED_FLAGS = re.compile(
    r'IMPLEMENT_SIMPLE_AUTOMATION_TEST\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*,\s*"[^"]+"\s*,'
    r'\s*([^)]*)\)')


def _class_and_name(raw):
    """[(class name, test name)] for one file's declarations."""
    return [(m.group(1), m.group(2)) for m in IMPLEMENTED_FULL.finditer(raw)]


def implemented_tests():
    """[(relative path, full test name)] for every test declared in Source."""
    found = []

    if not os.path.isdir(TESTS_DIR):
        return found

    for name in sorted(os.listdir(TESTS_DIR)):
        if not name.endswith(".cpp"):
            continue

        path = os.path.join(TESTS_DIR, name)
        raw = io.open(path, encoding="utf-8", errors="ignore").read()

        for _class_name, test in _class_and_name(raw):
            found.append((name, test))

    return found


def documented_categories():
    """category -> [test names], from Docs/18's table."""
    if not os.path.isfile(DOCS_18):
        return {}

    text = C.strip_comments(io.open(DOCS_18, encoding="utf-8", errors="ignore").read())
    categories = {}

    for category, tests_column in CATEGORY_ROW.findall(text):
        # The column holds one name per row for a single-test category, and a
        # comma-separated list of backticked names otherwise.
        names = [name for name in BACKTICKED.findall(tests_column)]
        categories.setdefault(category, [])
        for name in names:
            if name not in categories[category]:
                categories[category].append(name)

    return categories


def script_names():
    """[(script, OneBraham.* name)] for every name a script in Scripts/ mentions."""
    found = []

    if not os.path.isdir(SCRIPTS_DIR):
        return found

    for name in sorted(os.listdir(SCRIPTS_DIR)):
        if not name.endswith((".bat", ".sh")):
            continue

        text = io.open(os.path.join(SCRIPTS_DIR, name), encoding="utf-8",
                       errors="ignore").read()

        for mention in SCRIPT_RUNS_TESTS.findall(text):
            if SCRIPT_NAME.match(mention):
                found.append((name, mention))

    return found


def run(source_root=None, docs_root=None):
    """Returns (checked, problems), where problems are (message, kind)."""
    tests = implemented_tests()
    categories = documented_categories()

    if not tests:
        return 0, [(u"no automation tests found under Source/OneBraham/Tests - the "
                    u"file this check reads for its evidence is empty.", u"missing")]

    if not categories:
        return 0, [(u"no test table found in Docs/18 section 2 - this check reads the "
                    u"documentation for the list of suites and found none.", u"missing")]

    problems = []
    checked = 0

    # 1 and 3: every test is well formed, unique, and documented.
    documented = set()
    for category, names in categories.items():
        for name in names:
            documented.add(u"{0}.{1}".format(category, name))

    seen = {}
    for path, test in tests:
        checked += 1

        if not test.startswith("OneBraham."):
            problems.append((u"{0}: '{1}' is not under OneBraham.".format(path, test),
                             u"misnamed"))
            continue

        if test in seen:
            problems.append((u"'{0}' is declared twice: {1} and {2}".format(
                test, seen[test], path), u"duplicate"))
            continue

        seen[test] = path

        category = test.rsplit(".", 1)[0]
        if category not in categories:
            problems.append((u"{0}: '{1}' has category '{2}', which Docs/18 does not "
                             u"list as a suite.".format(path, test, category),
                             u"undocumented"))
        elif test not in documented:
            problems.append((u"{0}: '{1}' exists but Docs/18's {2} row does not list "
                             u"it - an undocumented test is one nobody knows to run."
                             .format(path, test, category), u"undocumented"))

    # 2: every documented test exists.
    for test in sorted(documented):
        if test not in seen:
            problems.append((u"Docs/18 lists '{0}' and no source file implements it - "
                             u"the suite is a table row.".format(test), u"missing"))

    # 5: one test per file, and the file is named for its category.
    by_file = {}
    for path, test in tests:
        by_file.setdefault(path, []).append(test)

    for path, names in sorted(by_file.items()):
        if not FILE_NAME.match(path):
            problems.append((u"{0}: test files are named OneBraham<Category>Tests.cpp."
                             .format(path), u"misnamed"))

    for name in sorted(os.listdir(TESTS_DIR)):
        if name.endswith(".cpp") and name not in by_file:
            problems.append((u"Tests/{0} declares no automation test.".format(name),
                             u"empty"))

    # 6 and 7: every test has a body, and the file can actually be compiled out.
    for path in sorted(os.listdir(TESTS_DIR)):
        if not path.endswith(".cpp"):
            continue

        raw = io.open(os.path.join(TESTS_DIR, path), encoding="utf-8", errors="ignore").read()
        code = C.strip_comments(raw)

        declared_here = IMPLEMENTED.findall(raw)
        bodies_here = set(RUN_TEST.findall(code))

        if not declared_here:
            continue

        if '#include "Misc/AutomationTest.h"' not in raw:
            problems.append((u"Tests/{0}: no Misc/AutomationTest.h include - the macro "
                             u"and Test* helpers come from there.".format(path),
                             u"include"))

        if "WITH_AUTOMATION_TESTS" not in code:
            problems.append((u"Tests/{0}: not wrapped in WITH_AUTOMATION_TESTS - the "
                             u"suite would be compiled into shipping builds.".format(path),
                             u"wrapper"))

        for class_name, test_name in _class_and_name(raw):
            if class_name in bodies_here:
                continue

            # The class may be defined in the same file under a different macro
            # spelling, so look for the name at all before reporting.
            if u"::RunTest" in code:
                problems.append((u"Tests/{0}: '{1}' is implemented for class '{2}', which "
                                 u"has no RunTest body in this file - a link error if the "
                                 u"class is used, a silently unregistered test if it is "
                                 u"not.".format(path, test_name, class_name),
                                 u"body"))
            else:
                problems.append((u"Tests/{0}: '{1}' has no RunTest body at all.".format(
                    path, test_name), u"body"))

        # A test with no context flag never runs, and never running looks exactly
        # like passing. A test with no filter flag fails UnrealHeaderTool's own
        # static_assert - the framework insists on exactly one of each - so the
        # context half is the one worth checking here.
        for flags in IMPLEMENTED_FLAGS.findall(raw):
            compact = " ".join(flags.split())

            if "Context" not in compact:
                problems.append((u"Tests/{0}: a test is declared with '{1}', which names no "
                                 u"context - it would never run.".format(path, compact),
                                 u"flags"))

            if not any(filter_name in compact for filter_name in FILTER_FLAGS):
                problems.append((u"Tests/{0}: a test is declared with '{1}', which names no "
                                 u"filter (EngineFilter, ProductFilter, SmokeFilter, ...). "
                                 u"UnrealHeaderTool asserts on this.".format(path, compact),
                                 u"flags"))

    # 4: the scripts name tests and categories that exist.
    for script, mention in script_names():
        if mention in seen:
            continue

        # A bare category, or a category prefix of a full name, is fine.
        if any(test.startswith(mention + ".") for test in seen):
            continue

        if mention in categories:
            continue

        problems.append((u"Scripts/{0} names '{1}', which is neither a test nor a "
                         u"category this project declares.".format(script, mention),
                         u"script"))

    return checked, problems


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()

    C.log(u"")
    C.log(u"  automation tests checked: {0}".format(checked))

    if not problems:
        C.log(u"  every test the documentation lists exists, every test that exists is "
              u"listed, and every name the scripts run resolves. OK")
        return _TestNameReport(checked, problems)

    C.log(u"  PROBLEM: {0}".format(len(problems)))
    for message, _kind in problems:
        C.log(u"      {0}".format(message))

    return _TestNameReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - the automation tests in the documentation against the source")
    C.log(u"=" * 70)
    C.log(u"  automation tests checked: {0}".format(checked))

    if not problems:
        C.log(u"  agree. OK")
        return 0

    C.log(u"  PROBLEM: {0}".format(len(problems)))
    for message, _kind in problems:
        C.log(u"  {0}".format(message))

    return 1


class _TestNameReport(object):
    """The shape run_all.py expects from a step: ok(), summary(), and the detail."""

    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} automation tests, {1} problem ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} automation tests, every one documented and resolvable".format(self.checked)


if __name__ == "__main__":
    raise SystemExit(main())
