# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_doc_files
=========================

Checks that the files the documentation tells people to run are files that exist,
and that the files in the repository are documented.

WHY THIS EXISTS
    `Docs/18_Testing_Performance.md` said the full validator runs in
    `Scripts\\RunAll.bat`. There was no `Scripts\\RunAll.bat`. The instruction read
    perfectly - it was the kind of line a reader skips past - and following it did
    nothing at all. It is the same defect as a stale count or an invented voice
    line id, in the form that wastes somebody's afternoon rather than their
    review: a command, in a document, that cannot be typed.

    It was found by a sweep, which is not a way to keep finding them.

WHAT IT CHECKS
    1. Every `Scripts/...` file the documentation names exists on disk.
    2. Every `.bat`, `.sh` and `.vdf` in `Scripts/` is named in
       `Scripts/README.md` - the script index is the only place a reader can
       discover that the Linux twins exist at all, and an undocumented script is
       one nobody runs, including the author in six months.
    3. Every `Tools/Python/...py` path the documentation names exists.
    4. Every module in `Tools/Python/onebraham/` is named in
       `Docs/17_Build_Automation.md`, so the pipeline table stays a complete index
       of the pipeline.

    What it deliberately does not check: whether a documented command's switches
    are real. That is the scripts' own README and, where it matters,
    `check_doc_numbers.py`'s business - matching switch strings would produce a
    rule that fires on prose.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_doc_files.py
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

python_root = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

SCRIPTS_DIR = os.path.join(python_root, "Scripts")
SCRIPTS_README = os.path.join(SCRIPTS_DIR, "README.md")
TOOLS_DIR = os.path.join(python_root, "Tools", "Python")
#: The package that holds the checkers and builders themselves.
TOOLS_PACKAGE = os.path.join(TOOLS_DIR, "onebraham")
DOCS_17 = os.path.join(python_root, "Docs", "17_Build_Automation.md")
DOCS_DIR = os.path.join(python_root, "Docs")

#: Every script extension the repository uses.
SCRIPT_SUFFIXES = (".bat", ".sh", ".vdf", ".ps1", ".psm1")

#: A path in the documentation: Scripts\RunAll.bat or Scripts/RunAll.sh
DOC_SCRIPT_PATH = re.compile(r'Scripts[\\/]([A-Za-z_][A-Za-z0-9_]*\.[A-Za-z0-9]+)')

#: A tool path in the documentation: Tools/Python/onebraham/check_calls.py
DOC_TOOL_PATH = re.compile(r'Tools[\\/]Python[\\/]([A-Za-z0-9_/]+\.py)')


def documentation_files():
    """Every markdown file that tells somebody to run something."""
    paths = []

    if os.path.isfile(SCRIPTS_README):
        paths.append(SCRIPTS_README)

    paths.append(os.path.join(python_root, "README.md"))

    if os.path.isdir(DOCS_DIR):
        for name in sorted(os.listdir(DOCS_DIR)):
            if name.endswith(".md"):
                paths.append(os.path.join(DOCS_DIR, name))

    return [p for p in paths if os.path.isfile(p)]


def run():
    """Returns (checked, problems), where problems are (message, kind)."""
    problems = []
    checked = 0

    if not os.path.isdir(SCRIPTS_DIR):
        return 0, [(u"no Scripts directory at {0}".format(SCRIPTS_DIR), u"missing")]

    docs = documentation_files()

    if not docs:
        return 0, [(u"no documentation to check - the Docs directory is empty.", u"missing")]

    # 1 and 3: every path the documentation names exists.
    for path in docs:
        text = io.open(path, encoding="utf-8", errors="ignore").read()
        relative = os.path.relpath(path, python_root)

        for name in set(DOC_SCRIPT_PATH.findall(text)):
            checked += 1

            if not os.path.isfile(os.path.join(SCRIPTS_DIR, name)):
                problems.append((
                    u"{0} tells the reader to run Scripts/{1}, which does not exist."
                    .format(relative, name), u"missing"))

        for name in set(DOC_TOOL_PATH.findall(text)):
            checked += 1

            if not os.path.isfile(os.path.join(TOOLS_DIR, name)):
                problems.append((
                    u"{0} names Tools/Python/{1}, which does not exist."
                    .format(relative, name), u"missing"))

    # 2: every script on disk is in the index.
    index = io.open(SCRIPTS_README, encoding="utf-8", errors="ignore").read() \
        if os.path.isfile(SCRIPTS_README) else u""

    for name in sorted(os.listdir(SCRIPTS_DIR)):
        if not name.endswith(SCRIPT_SUFFIXES):
            continue

        checked += 1

        if name not in index:
            problems.append((
                u"Scripts/{0} exists and Scripts/README.md never names it - an "
                u"undocumented script is one nobody runs.".format(name), u"undocumented"))

    # 4: every tooling module is in the pipeline table.
    docs_17 = io.open(DOCS_17, encoding="utf-8", errors="ignore").read() \
        if os.path.isfile(DOCS_17) else u""

    if os.path.isdir(TOOLS_PACKAGE):
        for name in sorted(os.listdir(TOOLS_PACKAGE)):
            if not name.endswith(".py") or name in ("__init__.py",):
                continue

            checked += 1

            if name not in docs_17:
                problems.append((
                    u"Tools/Python/onebraham/{0} is not named in "
                    u"Docs/17_Build_Automation.md - the pipeline table is the index "
                    u"of the pipeline.".format(name), u"undocumented"))

    return checked, problems


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()

    C.log(u"")
    C.log(u"  documented paths checked: {0}".format(checked))

    if not problems:
        C.log(u"  every script and tool the documentation names exists, every script "
              u"is in the script index, every module is in the pipeline table. OK")
        return _DocFileReport(checked, problems)

    C.log(u"  PROBLEM: {0}".format(len(problems)))
    for message, _kind in problems:
        C.log(u"      {0}".format(message))

    return _DocFileReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - the files the documentation names against the files on disk")
    C.log(u"=" * 70)
    C.log(u"  documented paths checked: {0}".format(checked))

    if not problems:
        C.log(u"  agree. OK")
        return 0

    C.log(u"  PROBLEM: {0}".format(len(problems)))
    for message, _kind in problems:
        C.log(u"  {0}".format(message))

    return 1


class _DocFileReport(object):
    """The shape run_all.py expects from a step: ok(), summary(), and the detail."""

    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} documented paths, {1} problem ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} documented paths, every one on disk".format(self.checked)


if __name__ == "__main__":
    raise SystemExit(main())
