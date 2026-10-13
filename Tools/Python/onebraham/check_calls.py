# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_calls
=====================

Finds call sites whose argument count cannot match the declaration.

WHY THIS EXISTS
    A call with the wrong number of arguments is the single most common
    compile error in a large hand-written C++ codebase, and in this project it
    has never been compiled. Every one of them is a build that stops, and a
    build that stops is a measurement the project cannot currently make.

    The compiler catches these in the same pass that catches a thousand other
    things, which sounds like enough until the build takes twenty minutes and
    the error queue is four hundred long. This finds them in a fifth of a
    second, on a machine with no engine on it, and it names the call site.

HOW IT AVOIDS LYING
    It only checks a call when the answer is unambiguous:

      * The function name must be declared in exactly ONE project class. A name
        that appears in two classes could be either of them at a call site, and
        guessing produces noise.
      * The function must not be overloaded within its class. Two declarations
        of the same name mean two valid arities, and the check takes the union.
      * Engine functions are not in this module's headers and are therefore
        never considered - `GetWorld()`, `GetName()` and friends are invisible
        to it.
      * Default arguments are honoured: a call is legal when its argument count
        is between (parameters - defaulted parameters) and (parameters).
      * A call whose arguments contain a braced initialiser (`{...}`) is skipped,
        because counting commas through a brace-init list is where a naive
        version of this check starts inventing errors.
      * Calls through an object (`GS->GetMatchSettings()`, `TeamScores.Init(...)`)
        are skipped, because this file does not know what type the receiver is.
        Only unqualified calls and `this->` are checked: calls to the enclosing
        class's own methods, which is where a wrong arity is unambiguous.
      * Variadic declarations are skipped entirely.

    When it is not sure, it says nothing. A check that cries wolf is a check
    that gets deleted, and the point of this file is to be believed.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_calls.py
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

SOURCE_ROOT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "Source", "OneBraham"))

#: Names that are macros, engine functions or language keywords rather than
#: project methods. Declaring a project method with one of these names would be
#: a bad idea anyway.
IGNORED_NAMES = {
    "check", "ensure", "verify", "checkf", "ensureMsgf", "UE_LOG", "TEXT", "LOCTEXT",
    "NSLOCTEXT", "if", "for", "while", "switch", "return", "sizeof", "new", "delete",
    "static_cast", "reinterpret_cast", "const_cast", "dynamic_cast", "catch",
}


def strip_comments_and_strings(text):
    """Removes comments and string literals, preserving line count."""
    text = re.sub(r'/\*.*?\*/', lambda m: "\n" * m.group(0).count("\n"), text, flags=re.DOTALL)
    text = re.sub(r'//[^\n]*', "", text)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    return text


def class_bodies(text):
    """The shared class-body brace matcher, which lives in common.py."""
    return C.class_bodies(text)


def split_statements(body):
    """The shared class-body statement splitter, which lives in common.py.

    It returns (macros, text) pairs; this checker only cares about the text, but
    it needs the macro handling because `UFUNCTION()` on its own line would
    otherwise be glued to the declaration below it and read as a call named
    UFUNCTION.
    """
    return [text for _macros, text in C.split_class_statements(body)]


def _extract_parameters(declaration):
    """The parameter list text of a declaration, or None."""
    open_index = declaration.find("(")
    if open_index == -1:
        return None

    depth = 0
    index = open_index

    while index < len(declaration):
        if declaration[index] == "(":
            depth += 1
        elif declaration[index] == ")":
            depth -= 1
            if depth == 0:
                return declaration[open_index + 1:index]
        index += 1

    return None


def _split_arguments(text):
    """Top-level comma-separated arguments, or None if it cannot be trusted."""
    if not text.strip():
        return []

    if "{" in text or "}" in text:
        return None           # braced initialiser: do not count commas through it

    arguments = []
    current = []
    depth_paren = 0
    depth_bracket = 0
    depth_angle = 0

    for char in text:
        if char == "(":
            depth_paren += 1
        elif char == ")":
            depth_paren -= 1
        elif char == "[":
            depth_bracket += 1
        elif char == "]":
            depth_bracket -= 1
        elif char == "<":
            depth_angle += 1
        elif char == ">":
            depth_angle = max(0, depth_angle - 1)

        if char == "," and depth_paren == 0 and depth_bracket == 0 and depth_angle == 0:
            arguments.append("".join(current).strip())
            current = []
        else:
            current.append(char)

    tail = "".join(current).strip()
    if tail:
        arguments.append(tail)

    return [a for a in arguments if a]


def declarations_by_name(source_root=None):
    """name -> set of (required_min, maximum) arities, for unique names only."""
    root = source_root or SOURCE_ROOT

    if not os.path.isdir(root):
        return {}

    # name -> {class: set of (min, max)}
    seen = {}

    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith(".h"):
                continue

            text = strip_comments_and_strings(
                io.open(os.path.join(dirpath, name), encoding="utf-8", errors="ignore").read())

            for class_name, body, _is_interface in class_bodies(text):
                for statement in split_statements(body):
                    text_statement = " ".join(statement.split()).strip()

                    # A declaration ends with ";" and an inline definition ends
                    # with "}" - both declare an arity that a call site must
                    # match, and dropping the second kind is what made
                    # GetMatchSettings look unique to one class when three
                    # classes declare it.
                    if not text_statement.endswith((";", "}")):
                        continue
                    if "(" not in text_statement or "=" in text_statement.split("(", 1)[0]:
                        continue
                    if re.match(r'^(?:enum|class|struct|union|namespace)\b', text_statement):
                        continue

                    parameters = _extract_parameters(text_statement)
                    if parameters is None or "..." in parameters:
                        continue

                    head = text_statement[:text_statement.find("(")]
                    match = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*$', head)
                    if match is None:
                        continue

                    method = match.group(1)

                    if method in IGNORED_NAMES or method.startswith("DECLARE_"):
                        continue

                    arguments = _split_arguments(parameters)
                    if arguments is None:
                        continue

                    total = len(arguments)
                    defaulted = len([a for a in arguments if "=" in a])

                    seen.setdefault(method, {}).setdefault(class_name, set()).add(
                        (total - defaulted, total))

    # Only names that belong to exactly one class.
    unique = {}

    for method, classes in seen.items():
        if len(classes) != 1:
            continue

        arities = list(classes.values())[0]
        unique[method] = arities

    return unique


def run(source_root=None):
    """Returns (call_sites_checked, problems).

    problems is a list of (relative_path, line_number, message).
    """
    root = source_root or SOURCE_ROOT
    signatures = declarations_by_name(root)

    if not signatures:
        return 0, []

    checked = 0
    problems = []

    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith(".cpp"):
                continue

            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, root)

            raw = io.open(path, encoding="utf-8", errors="ignore").read()
            # Line numbers have to survive stripping, so keep the original for them.
            text = strip_comments_and_strings(raw)

            for match in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', text):
                method = match.group(1)

                if method not in signatures:
                    continue

                # Skip a declaration or definition of it.
                before = text[max(0, match.start() - 60):match.start()]
                if re.search(r'\b(class|struct|enum|namespace)\s+$', before):
                    continue

                # A call through an object - `TeamScores.Init(0, 2)`,
                # `Codec.Initialise()`, `Other->MarkUsed()` - cannot be checked
                # without knowing the receiver's type, and nothing in this file
                # knows it. `TeamScores.Init(0, 2)` is TArray::Init and has
                # nothing to do with UOneBrahamGameInstance::Init(); reporting it
                # would be the check inventing an error, which is the one thing it
                # is not allowed to do. So: unqualified calls and `this->` only.
                receiver = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:->|\.|::)\s*$', before)
                if receiver is not None and receiver.group(1) != "this":
                    continue

                # Find the matching close paren and count arguments.
                depth = 0
                index = match.end() - 1
                while index < len(text):
                    if text[index] == "(":
                        depth += 1
                    elif text[index] == ")":
                        depth -= 1
                        if depth == 0:
                            break
                    index += 1

                if depth != 0:
                    continue

                arguments = _split_arguments(text[match.end():index])
                if arguments is None:
                    continue

                checked += 1
                count = len(arguments)

                if any(minimum <= count <= maximum for minimum, maximum in signatures[method]):
                    continue

                line_number = text[:match.start()].count("\n") + 1
                expected = ", ".join("{0}..{1}".format(a, b) for a, b in sorted(signatures[method]))

                problems.append((rel, line_number,
                                 u"{0}(...) called with {1} argument(s); the declaration takes {2}".format(
                                     method, count, expected)))

    return checked, problems


class _CallReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} call sites, {1} suspect ({2})".format(
                self.checked, len(self.problems), self.problems[0][2][:60])
        return u"{0} call sites, all arities plausible".format(self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()

    C.log(u"")
    C.log(u"  call sites checked against unique project signatures: {0}".format(checked))

    if not problems:
        C.log(u"  no call site's argument count contradicts its declaration. OK")
        return _CallReport(checked, problems)

    C.log(u"  SUSPECT: {0}".format(len(problems)))
    for rel, line_number, message in problems:
        C.log(u"      {0}:{1} - {2}".format(rel, line_number, message))

    return _CallReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - call arity against project declarations")
    C.log(u"=" * 70)
    C.log(u"  call sites checked: {0}".format(checked))

    if not problems:
        C.log(u"  all plausible. OK")
        return 0

    C.log(u"  SUSPECT: {0}".format(len(problems)))
    for rel, line_number, message in problems:
        C.log(u"  {0}:{1} - {2}".format(rel, line_number, message))

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
