# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_cpp_symbols
===========================

Finds C++ member functions that are declared in a class body and defined nowhere
in the module.

WHY THIS EXISTS
    Nothing in this project has been compiled by a machine with an Unreal Engine
    install behind it yet, so "it will link" is a belief rather than a fact. The
    two most common ways a large hand-written C++ module fails to link are also
    the two most mechanical:

      1. A method declared in a header and defined in no .cpp at all. The header
         looks complete, the .cpp looks complete, and the linker is the first
         thing to disagree.
      2. A method whose definition has a different signature from its declaration
         - usually a missing `const`, or a parameter that was a reference in the
         header and a value in the source.

    Both are found by comparing declarations against definitions textually, and
    neither is found by a code review of 107 headers.

HOW IT PARSES
    Not with a regex over the whole file, which is what the first version of this
    tool did and why it reported 177 "missing" functions of which 170 were macro
    invocations and default arguments. It now walks class bodies: find `class X {`,
    find the matching brace, split the body into statements at brace depth zero,
    and keep the statements that are a plain declaration ending in `;` with no
    initialiser and no body. That is the shape of a declaration and of nothing
    else.

WHAT IT DOES NOT DO
    It is not a compiler. It does not resolve overloads, does not understand
    templates and skips anything it is not sure about. A file it cannot parse
    produces noise at worst, and the known-external list below absorbs the
    declarations that UnrealHeaderTool generates.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_cpp_symbols.py
    (runs without the editor; used by run_all --text-only and by validate_project)
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

SOURCE_ROOT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "Source", "OneBraham"))

#: Definitions that UnrealHeaderTool generates, or that the engine provides.
#: Anything here is deliberate; anything not here and not defined is a true
#: "declared but never defined".
KNOWN_EXTERNAL = {
    # -- UHT generated ------------------------------------------------------
    "GetLifetimeReplicatedProps",
    "GetReplicatedCustomConditionState",
    "GetSubobjectReplicationList",
    "StaticClass",
    "GetPrivateStaticClass",

    # -- BlueprintNativeEvent: UHT writes the thunk, the project writes
    #    *_Implementation. The _Implementation is what is checked instead.
    "GetInteractionPrompt",
    "GetInteractionTag",
    "CanInteract",
    "RequiresHold",
    "GetHoldDuration",
    "GetInteractionRange",
    "OnInteractionStart",
    "OnInteractionHold",
    "OnInteractionEnd",
    "OnInteractionCancelled",
    "SetInteractionHighlight",
    "ApplyBreachDamage",
    "IsBreached",
    "GetBreachResistance",
    "GetBreachIntegrity01",
    "GetBreachFloor",

    # -- Inline in a base class, redeclared for documentation --------------
    "IsAlive",
    "GetHealth",
    "HasAuthority",
}

MACRO_LINE = re.compile(r'^[A-Z][A-Z0-9_]*\s*[\(;]')

#: A declaration: `Type Name(args) tail;` where Name is the identifier directly
#: before the first top-level parenthesis.
DECL_LINE = re.compile(
    r'^(?P<return>(?:[\w:<>,*&\[\]\s]|::)+?)\s*'      # return type
    r'(?P<name>~?[A-Za-z_][A-Za-z0-9_]*)\s*'          # method name
    r'\((?P<params>.*)\)\s*'                          # parameters
    r'(?P<tail>(?:const\b|override\b|final\b|noexcept\b|virtual\b|\s)*)$',
    re.DOTALL)


def strip_comments_and_strings(text):
    """Removes comments and string literals, preserving line count.

    The implementation lives in common.py: it used to be four regexes here, in
    five other checkers, and the order of those regexes could delete real code
    from the analysis. See common.strip_comments_and_strings.
    """
    return C.strip_comments_and_strings(text)


def class_bodies(text):
    """The shared class-body brace matcher, which lives in common.py.

    It used to be a copy of this, with the same `[AUIE]` prefix set that left
    every F struct invisible - so a method declared in a struct was never checked
    for a definition at all. One implementation, in one place.
    """
    return C.class_bodies(text)


def split_statements(body):
    """Top-level brace-depth-zero statements of a class body.

    Statements are separated by `;` at depth zero of ()/[]; a statement that
    opens a brace at depth zero (an inline definition or a nested type) is
    returned as its own chunk so the caller can discard it.
    """
    statements = []
    current = []
    depth_paren = 0
    depth_brace = 0

    index = 0
    while index < len(body):
        char = body[index]

        if char in "([":
            depth_paren += 1
        elif char in ")]":
            depth_paren -= 1
        elif char == "{":
            depth_brace += 1
        elif char == "}":
            depth_brace -= 1

        if char == ";" and depth_paren == 0 and depth_brace == 0:
            current.append(char)
            statements.append("".join(current))
            current = []
        elif char == "}" and depth_brace == 0 and depth_paren == 0:
            current.append(char)
            statements.append("".join(current))
            current = []
        else:
            current.append(char)

        index += 1

    if current:
        statements.append("".join(current))

    return statements


def declarations_in_body(body):
    """Method names declared (not defined) in a class body."""
    names = []

    for statement in split_statements(body):
        text = " ".join(statement.split()).strip()

        if not text.endswith(";"):
            continue

        # Access specifiers, macros, UHT blocks, static_assert and friends.
        if text.endswith(":"):
            continue
        if MACRO_LINE.match(text):
            continue
        if text.startswith("//") or text.startswith("#"):
            continue
        if "{" in text or "}" in text:
            continue

        # Properties and enum values have an initialiser; declarations do not.
        if "=" in text.split("(", 1)[0]:
            continue

        match = DECL_LINE.match(text[:-1].strip())
        if match is None:
            continue

        name = match.group("name")

        # A parameter list containing another call at depth one means the regex
        # latched onto an inner call rather than the declaration itself.
        if "(" in match.group("params") and ")" in match.group("params"):
            continue

        if name in ("if", "for", "while", "switch", "return", "sizeof", "new", "delete",
                    "static_assert", "check", "ensure", "TEXT", "LOCTEXT", "NSLOCTEXT",
                    "FVector", "FVector2D", "FRotator", "FLinearColor", "FTransform",
                    "FName", "FString", "FText", "FGameplayTag", "TArray", "TMap", "TSet"):
            continue

        if name.startswith("DECLARE_") or name.startswith("GENERATED_"):
            continue

        names.append(name)

    return names


def defined_symbols(text):
    """Every `Class::Method` seen in the module's sources."""
    found = set()
    for match in re.finditer(r'([A-Za-z_][A-Za-z0-9_]*)::(~?[A-Za-z_][A-Za-z0-9_]*)\s*\(', text):
        found.add((match.group(1), match.group(2)))
    return found


def run(report=None):
    """Returns (declarations_checked, missing) where missing is a list of tuples."""
    if not os.path.isdir(SOURCE_ROOT):
        if report is not None:
            report.failed("cpp symbols", u"no source root at {0}".format(SOURCE_ROOT))
        return 0, []

    headers = []
    source_files = []

    for dirpath, _dirs, files in os.walk(SOURCE_ROOT):
        for name in files:
            path = os.path.join(dirpath, name)
            if name.endswith(".h"):
                headers.append(path)
            elif name.endswith(".cpp"):
                source_files.append(path)

    all_text = "\n".join(
        strip_comments_and_strings(io.open(p, encoding="utf-8", errors="ignore").read())
        for p in headers + source_files)
    defined = defined_symbols(all_text)

    checked = 0
    missing = []

    for path in sorted(headers):
        raw = io.open(path, encoding="utf-8", errors="ignore").read()
        clean = strip_comments_and_strings(raw)
        rel = os.path.relpath(path, SOURCE_ROOT)

        parsed_any = False

        for class_name, body, is_interface in class_bodies(clean):
            parsed_any = True

            for name in declarations_in_body(body):
                checked += 1

                if name in KNOWN_EXTERNAL:
                    continue

                # Constructors and destructors are declared with the class name.
                if name == class_name or name == "~" + class_name:
                    if (class_name, class_name) in defined or (class_name, "~" + class_name) in defined:
                        continue
                    if class_name.startswith("U") or class_name.startswith("A") or class_name.startswith("F"):
                        # A struct/actor with no explicit constructor is fine: the
                        # compiler generates one.
                        continue

                if (class_name, name) in defined:
                    continue

                # An interface's BlueprintNativeEvent defaults are defined by each
                # implementer; the interface itself legitimately has no definition.
                if is_interface or class_name in ("IOneBrahamInteractionInterface",
                                                 "IOneBrahamBreachableInterface"):
                    continue

                missing.append((rel, class_name, name))

        if not parsed_any:
            continue

    return checked, missing


class _SymbolReport(object):
    """Report object with the shape run_all expects (summary + ok())."""

    def __init__(self, checked, missing, limit_note=None):
        self.checked = checked
        self.missing = list(missing)

    def ok(self):
        return not self.missing

    def summary(self):
        if self.missing:
            first = ", ".join(sorted(set("{0}::{1}".format(c, n) for _f, c, n in self.missing))[:3])
            return u"{0} declared, {1} undefined ({2})".format(
                self.checked, len(self.missing), first)
        return u"{0} declarations, all defined".format(self.checked)


def build_all():
    """Entry point used by run_all.py. Prints the detail, returns a report."""
    checked, missing = run()

    C.log(u"")
    C.log(u"  declarations checked: {0}".format(checked))

    if missing:
        C.log(u"  MISSING: {0} - each one is a link error".format(len(missing)))
        for rel, cls, name in missing:
            C.log(u"      {0}: {1}::{2}".format(rel, cls, name))
    else:
        C.log(u"  every declared member function has a definition. OK")

    return _SymbolReport(checked, missing)


def main():
    checked, missing = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - declared-but-undefined member functions")
    C.log(u"=" * 70)
    C.log(u"  declarations checked: {0}".format(checked))

    if not missing:
        C.log(u"  every declared member function has a definition. OK")
        return 0

    C.log(u"  MISSING: {0} - each one is a link error".format(len(missing)))
    C.log(u"")

    by_file = {}
    for rel, cls, name in missing:
        by_file.setdefault(rel, []).append(u"{0}::{1}".format(cls, name))

    for rel in sorted(by_file):
        C.log(u"  {0}".format(rel))
        for symbol in sorted(set(by_file[rel])):
            C.log(u"      {0}".format(symbol))

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
