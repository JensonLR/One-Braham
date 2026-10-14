# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_doc_types
=========================

Checks that every One Braham type the documentation names is a type this project
declares.

WHY THIS EXISTS
    A manual tells a reader what to edit. When it names a type that does not
    exist, the reader does not find it - and then does the worst available
    thing, which is invent it. This project shipped several of those:

      * `EOneBrahamExtractionStage` - the enum is `EOneBrahamExtractionState`.
      * `EOneBrahamArchetype` - there is no archetype enum at all; archetypes
        are gameplay tags (`OB.Character.Archetype.*`) carried in
        `FOneBrahamArchetypeDefinition`.
      * `EOneBrahamGameMode` - the mode enum is `EOneBrahamMatchMode`, and
        `AOneBrahamGameMode` is a class.
      * `EOneBrahamDamageType` - no such enum; the death screen is worded from
        `EOneBrahamKillFeedVerb` and `FOneBrahamDamageReport`.
      * `UOneBrahamCharacter`, `UOneBrahamGameState` - both are actors
        (`AOneBrahamCharacter`, `AOneBrahamGameState`); a `U` prefix sends a
        reader looking for a `UObject` that was never written.
      * `UOneBrahamVoiceCodec` - it is `FOneBrahamVoiceCodec`, a plain class.

    Worse, the same table that named four of those also listed enumerator values
    that no enum has ever had (`Warmup, Deploy, Playing, Lockdown, Extraction,
    Results` for a five-value enum; `Head, Torso, Arms, Legs` for a nine-value
    one), and a health model with a 45 second bleed-out in a component whose
    `BleedOutSeconds` is 60. Documentation drifts by default, in the direction
    of what sounds reasonable.

WHAT IT CHECKS
    1. Every `[AUIFSE]OneBraham...` type name in `README.md` and `Docs/*.md` is
       declared in `Source/OneBraham/**` as a `class`, `struct` or `enum class`,
       or is a `FOneBraham...` struct/class.
    2. The names that are deliberately not code - the canonical test shape in
       Docs/18 is taught with a made-up example class - are listed in
       `INTENTIONAL_NAMES` below, with a reason. Keeping that list short and
       explicit is the point: adding a name to it is a decision, made in a
       review, rather than a fact nobody notices.

WHAT IT IGNORES
    Module and file names (`OneBraham.Build.cs`, `OneBraham.uproject`), include
    paths (a path is not a type), and `FOneBraham*` names that appear only in a
    fenced code block that is explicitly marked as an example. Everything else
    in prose and tables is fair game, because that is where a reader looks.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_doc_types.py
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
DOC_ROOTS = [os.path.join(python_root, "Docs"), python_root]
DOC_SUFFIXES = (".md",)

#: Type names this project declares. The prefix letter is the Unreal convention
#: and is load-bearing: `UOneBrahamCharacter` and `AOneBrahamCharacter` are
#: different things to a reader who is about to write code.
DECLARATION = re.compile(
    r'\b(?:class|struct|enum\s+class)\s+'
    r'(?:ONEBRAHAM_API\s+)?'
    r'([AUIEF]OneBraham[A-Za-z0-9_]*)')

#: A type name as the documentation writes it. Bounded by non-identifier
#: characters so `OneBraham.Build.cs` and `OB_...` macros are not matched.
NAMED_TYPE = re.compile(r'(?<![A-Za-z0-9_.])([AUIEF]OneBraham[A-Za-z0-9_]*)')

#: Names the documentation is allowed to use without a declaration, each with
#: the reason it is allowed. Every entry is a deliberate exception.
INTENTIONAL_NAMES = {
    "FOneBrahamCoreTest": (
        "Docs/18 section 2.1 teaches the canonical automation-test shape with a "
        "made-up example class. An example that is also missing, or that quietly "
        "became a real test, stops teaching anything - so it stays absent, and "
        "stays listed here."),
}

#: A fenced block whose info string asks for a non-existent class (```example)
#: is a teaching block: the names inside it are illustrations, not references.
EXAMPLE_FENCE = re.compile(r'```[A-Za-z0-9_-]*\b(?:example|illustration)\b')


def declared_types(source_root=None):
    """Every type name declared anywhere under Source/."""
    root = source_root or SOURCE_ROOT
    found = {}

    if not os.path.isdir(root):
        return found

    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith((".h", ".cpp")):
                continue

            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, python_root)
            text = io.open(path, encoding="utf-8", errors="ignore").read()
            clean = C.strip_comments_and_strings(text)

            for type_name in DECLARATION.findall(clean):
                found.setdefault(type_name, rel)

    return found


def documented_types():
    """(relative_path, line_number, name, line) for every named type in the docs."""
    out = []

    for root in DOC_ROOTS:
        if not os.path.isdir(root):
            continue

        for dirpath, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs
                       if d not in (".git", "Source", "Tools", "Content", "Saved")]

            for name in sorted(files):
                if not name.endswith(DOC_SUFFIXES):
                    continue

                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, python_root)
                lines = io.open(path, encoding="utf-8", errors="ignore").read().splitlines()

                in_example = False

                for number, line in enumerate(lines, 1):
                    if line.strip().startswith("```"):
                        in_example = bool(EXAMPLE_FENCE.search(line.strip()))
                        continue

                    if in_example:
                        continue

                    for type_name in NAMED_TYPE.findall(line):
                        out.append((rel, number, type_name, line.strip()))

    return out


def run(source_root=None, doc_roots=None):
    """Returns (names_checked, problems, allowed)."""
    declared = declared_types(source_root)
    problems = []
    allowed = {}
    checked = 0
    seen = set()

    global DOC_ROOTS
    saved_roots = DOC_ROOTS
    if doc_roots is not None:
        DOC_ROOTS = list(doc_roots)

    try:
        documented = documented_types()
    finally:
        DOC_ROOTS = saved_roots

    for rel, number, type_name, line in documented:
        if type_name in seen:
            continue
        seen.add(type_name)
        checked += 1

        if type_name in declared:
            continue

        if type_name in INTENTIONAL_NAMES:
            allowed[type_name] = INTENTIONAL_NAMES[type_name]
            continue

        problems.append((rel, number,
                         u"'{0}' is named in the documentation but declared nowhere in "
                         u"Source/ - a reader looking for it will invent it".format(type_name)))

    # Every intentional name must still be used. An exception that outlives the
    # thing it excused is a licence to name a type that does not exist.
    documented_names = set(name for _rel, _n, name, _line in documented)
    for type_name in sorted(INTENTIONAL_NAMES):
        if type_name not in documented_names:
            problems.append(("Tools/Python/onebraham/check_doc_types.py", None,
                             u"'{0}' is listed as an intentional example but the documentation "
                             u"no longer uses it - remove the exception".format(type_name)))

    return checked, problems, allowed


class _DocTypeReport(object):
    def __init__(self, checked, problems, allowed):
        self.checked = checked
        self.problems = list(problems)
        self.allowed = dict(allowed)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} documented type names, {1} undeclared ({2})".format(
                self.checked, len(self.problems), self.problems[0][2][:60])
        return u"{0} documented type names, all declared".format(self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems, allowed = run()

    C.log(u"")
    C.log(u"  documented type names checked: {0}".format(checked))

    for type_name in sorted(allowed):
        C.log(u"  intentional (not a type): {0}".format(type_name))

    if not problems:
        C.log(u"  every type the documentation names is one this project declares. OK")
    else:
        for rel, number, message in problems:
            where = u"{0}:{1}".format(rel, number) if number else rel
            C.log(u"      {0} - {1}".format(where, message))

    return _DocTypeReport(checked, problems, allowed)


def main():
    checked, problems, allowed = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - documented type names")
    C.log(u"=" * 70)
    C.log(u"  names checked: {0}".format(checked))

    for type_name in sorted(allowed):
        C.log(u"  intentional: {0} - {1}".format(type_name, INTENTIONAL_NAMES[type_name]))

    if not problems:
        C.log(u"  all declared. OK")
        return 0

    C.log(u"  UNDECLARED: {0}".format(len(problems)))
    for rel, number, message in problems:
        where = u"{0}:{1}".format(rel, number) if number else rel
        C.log(u"  {0} - {1}".format(where, message))

    return 1


if __name__ == "__main__":
    sys.exit(main())
