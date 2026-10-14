# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_builders_dry_run
================================

Runs every content builder end to end against a stand-in for the editor's
`unreal` module, and fails on mistakes in the builders' own Python.

WHY THIS EXISTS
    The builders only ever run inside the editor, so until somebody has an
    engine, nothing executes them. An ordinary Python mistake in one (a
    misspelled local, a report method that does not exist, a function called
    with the wrong arguments) waits for the first content build, and most of
    the builders turn an exception into a report line, so the build goes on
    and the mistake reads like an asset problem. The same dry run also runs
    the builders' pure rules: it is how two opposing Skydeck spawns 17.5 m
    apart were found.

HOW
    The stand-in answers every attribute and call with a permissive mock, so
    the engine side always "works" and only the builders' own logic can fail.
    The builders run in a child process, in a scratch copy of the tree, so the
    mock never meets the other checks and nothing the builders write lands in
    the checkout.

WHAT COUNTS AS A FAILURE
    A builder that crashes outside its own guards, and any reported failure
    that is a Python-level error rather than a consequence of the mock: a name
    that is not defined, a wrong argument count or keyword, an attribute
    missing on a non-mock object, an import that fails. Everything else (a
    mock that unpacks to nothing, a verification that finds no assets) is the
    stand-in's limit, not the builder's, and is ignored.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_builders_dry_run.py
"""

from __future__ import print_function

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

from onebraham import common as C

python_root = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

#: The builders run_all.py runs in the editor, in its order.
BUILDERS = ("build_materials", "build_environment", "build_props", "build_weapons",
            "build_characters", "apply_cast_payload", "build_audio", "build_input",
            "build_ui", "build_vfx")

#: The parts of the tree the builders read.
COPIED = ("Tools", "Config", "Docs", "Source", "Content", "OneBraham.uproject")

FAKE_UNREAL = '''
from unittest import mock
_m = mock.MagicMock(name="unreal")
def __getattr__(name):
    return getattr(_m, name)
def log(msg): pass
def log_warning(msg): pass
def log_error(msg): pass
'''

CHILD = r'''
import io, json, contextlib, importlib, sys, traceback
sys.path.insert(0, sys.argv[1]); sys.path.insert(0, "Tools/Python")
results = {}
for name in sys.argv[2:]:
    entry = {"crash": None, "errors": []}
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            module = importlib.import_module("onebraham." + name)
            if name == "build_environment":
                report = module.build_all(write_manifest=False)
            else:
                report = module.build_all()
        entry["errors"] = [[str(what), str(why)] for what, why in getattr(report, "errors", [])]
    except Exception:
        entry["crash"] = traceback.format_exc()
    results[name] = entry
print(json.dumps(results))
'''

#: Messages that are the builder's own Python going wrong, whatever the mock does.
PYTHON_ERROR = re.compile(
    r"NameError|UnboundLocalError|ImportError|ModuleNotFoundError|SyntaxError"
    r"|name '\w+' is not defined"
    r"|takes \d+ positional argument|got an unexpected keyword argument"
    r"|missing \d+ required positional argument"
    r"|'(?!MagicMock|Mock)\w+' object has no attribute"
    r"|has no attribute '\w+'(?![^\n]*Mock)")


def _python_errors(entry):
    problems = []
    if entry.get("crash"):
        problems.append(entry["crash"].strip().splitlines()[-1])
    for what, why in entry.get("errors", []):
        if PYTHON_ERROR.search(why) and "Mock" not in why.splitlines()[-1]:
            problems.append(u"{0}: {1}".format(what, why.strip().splitlines()[-1]))
    return problems


def run():
    """Returns (builders_run, problems), where problems are (message, kind)."""
    scratch = tempfile.mkdtemp(prefix="ob_dryrun_")
    try:
        tree = os.path.join(scratch, "tree")
        fake = os.path.join(scratch, "fake")
        os.makedirs(tree)
        os.makedirs(fake)
        for name in COPIED:
            source = os.path.join(python_root, name)
            if os.path.isdir(source):
                shutil.copytree(source, os.path.join(tree, name),
                                ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
            elif os.path.isfile(source):
                shutil.copy2(source, os.path.join(tree, name))
        with open(os.path.join(fake, "unreal.py"), "w") as handle:
            handle.write(FAKE_UNREAL)

        child = subprocess.run([sys.executable, "-c", CHILD, fake] + list(BUILDERS),
                               cwd=tree, capture_output=True, text=True, timeout=600)
        if child.returncode != 0 or not child.stdout.strip():
            return 0, [(u"the dry run did not finish: {0}".format(
                (child.stderr or u"no output").strip().splitlines()[-1:]), u"harness")]
        results = json.loads(child.stdout.strip().splitlines()[-1])
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    problems = []
    for name in BUILDERS:
        for message in _python_errors(results.get(name, {})):
            problems.append((u"{0}: {1}".format(name, message), u"python"))
    # The builders' pure rules that the stand-in cannot satisfy by accident.
    for name, entry in sorted(results.items()):
        for what, why in entry.get("errors", []):
            if what == "spawn safety":
                problems.append((u"{0}: spawn safety: {1}".format(name, why), u"design"))
    return len(BUILDERS), problems


class _DryRunReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} builders, {1} problems ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} builders ran end to end with no Python error".format(self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()
    C.log(u"")
    C.log(u"  builders dry run: {0} builders against a stand-in unreal module".format(checked))
    if not problems:
        C.log(u"  every builder's own Python ran end to end. OK")
    else:
        for message, kind in problems:
            C.log(u"      [{0}] {1}".format(kind, message))
    return _DryRunReport(checked, problems)


def main():
    checked, problems = run()
    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - content builders, dry run")
    C.log(u"=" * 70)
    C.log(u"  builders run: {0}".format(checked))
    if not problems:
        C.log(u"  every builder's own Python ran end to end. OK")
        return 0
    C.log(u"  PROBLEMS: {0}".format(len(problems)))
    for message, kind in problems:
        C.log(u"  [{0}] {1}".format(kind, message))
    return 1


if __name__ == "__main__":
    sys.exit(main())
