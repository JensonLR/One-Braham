# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_gameplay_tags
=============================

Checks that every gameplay tag this project asks for is one this project declares.

WHY THIS EXISTS
    Gameplay tags fail silently. `FGameplayTag::RequestGameplayTag` and
    `RequestGameplayTag(FName, false)` return an invalid tag when the name is not
    registered, and an invalid tag is not an error - it is a tag that no query
    matches, so every branch keyed on it takes its fallback path, quietly, at
    runtime, forever.

    This is not hypothetical. The bot controller asked for
    "OB.Character.Archetype.Intern". The archetype is called Graduate, in this
    project's own registry and its own documentation, and it has been called
    Graduate since the first chapter was written. Because the request passed
    ErrorIfNotFound = false, a name that has never existed produced no warning,
    no log line and no failing test - it produced bots whose PlayerState had an
    invalid archetype, which the badging code handled by printing the company
    name instead of a job title.

    A wrong tag is a wrong string, and a wrong string is exactly what a machine
    can check.

WHAT IT CHECKS
    1. A tag requested at runtime is declared, either natively
       (UE_DEFINE_GAMEPLAY_TAG in Core/OneBrahamGameplayTags.cpp) or in
       Config/DefaultGame.ini's +GameplayTagList.
    2. A natively declared tag is also listed in the ini, so a designer can find
       it in the tag picker. This is a warning, not a failure: the code is
       correct, and the editor is simply less useful than it should be.
    3. A tag is not declared twice with the same name.
    4. A tag the documentation names is declared. The registry reference in
       Docs/03 lists all 147 in a brace form (`OB.Access.Tier.{Public,Staff}`)
       which is expanded here before checking, so the table is verified rather
       than trusted. This rule earned its place the day the table was read: it
       listed an `OB.Area.*` namespace that has never existed, an
       `OB.Character.Archetype.{Analyst,Graduate,...}` set missing the word
       Business, an `OB.Weapon.Category.*` family and four more - all of them
       things a designer would search the tag picker for, and not find.
    5. A declared tag is documented somewhere. This is a warning: the code is
       right and the reference is incomplete.

WHAT IT IGNORES
    Editor-only metadata (`meta = (Categories = "OB.Access")`), which is a
    filter string rather than a tag; commented-out lines; and `/Script/` engine
    tags, which are not this project's business.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_gameplay_tags.py
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
NATIVE_HEADER = os.path.join(SOURCE_ROOT, "Core", "OneBrahamGameplayTags.h")
NATIVE_SOURCE = os.path.join(SOURCE_ROOT, "Core", "OneBrahamGameplayTags.cpp")
GAME_INI = os.path.join(python_root, "Config", "DefaultGame.ini")

NATIVE_DECL = re.compile(r'UE_DEFINE_GAMEPLAY_TAG(?:_COMMENT)?\s*\(\s*([A-Za-z0-9_]+)\s*,\s*"([^"]+)"')
INI_DECL = re.compile(r'\+GameplayTagList\s*=\s*\(\s*Tag\s*=\s*"([^"]+)"')

#: A tag requested through the API. The closing quote lets a `%s` format string
#: through, and those are skipped: an assembled tag cannot be checked from text.
REQUEST = re.compile(r'RequestGameplayTag(?:Checked)?\s*\(\s*(?:FName\s*\(\s*)?(?:TEXT\s*\(\s*)?"([^"]+)"')

#: Anything that looks like one of this project's tags.
TAG_SHAPE = re.compile(r'^OB\.[A-Za-z][A-Za-z0-9_.]*$')


#: The documentation lives in these files, and game rules are written in them.
DOC_ROOTS = (os.path.join(python_root, "Docs"), python_root)
DOC_SUFFIXES = (".md",)

#: A documented tag, possibly in brace form. The brace form may wrap over lines:
#: `OB.Audio.State.{Floor.Lobby, Floor.Mezzanine,\n Rain.Heavy}` is one entry.
DOC_TOKEN = re.compile(r'OB\.[A-Za-z0-9_.{},\- ]*?(?:\}|(?=[`\s|,)]|$))')

#: A tag assembled at runtime: Printf(TEXT("OB.Character.Archetype.%s"), <expr>)
ASSEMBLED = re.compile(r'Printf\s*\(\s*(?:TEXT\s*\(\s*)?"((?:OB\.[A-Za-z0-9_.]*)%s)"')

#: A local string table, which is where the suffixes of an assembled tag come
#: from: static const TCHAR* DefaultArchetypes[] = { TEXT("Graduate"), ... };
STRING_TABLE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*\]\s*=\s*\{(.*?)\}', re.DOTALL)


def string_tables(text):
    """name -> [suffixes] for every local array of string literals."""
    tables = {}

    for match in STRING_TABLE.finditer(text):
        values = re.findall(r'TEXT\s*\(\s*"([^"]*)"', match.group(2))
        if values:
            tables[match.group(1)] = values

    return tables


def assembled_tags(text):
    """(line_number, prefix, suffix_list_or_None) for every assembled tag.

    `suffix_list_or_None` is None when the suffix cannot be resolved from text - a
    travel option, a profile string - and the caller skips those. When the suffix
    comes from a literal or from a local string table, every candidate is
    checkable, and that is the case that matters: the archetype names live in a
    table like that, and one of them was wrong for the whole life of the project.
    """
    tables = string_tables(text)
    out = []

    for match in ASSEMBLED.finditer(text):
        prefix = match.group(1)
        line_number = text[:match.start()].count("\n") + 1

        # Walk the whole Printf(...) call and take the arguments after the format
        # string. Scanning from the format string's closing quote does not work:
        # the first character there is the paren that closes TEXT(, and a naive
        # paren counter stops on it.
        depth = 0
        index = match.start() + len("Printf")
        arguments_start = None
        arguments_end = None

        while index < len(text):
            char = text[index]

            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    arguments_end = index
                    break
            elif char == "," and depth == 1 and arguments_start is None:
                # The comma that ends the format string argument, and only that
                # one: depth 1 means we are still inside Printf(.
                after = text[match.end():index]
                if '"' in after or ")" in after:
                    arguments_start = index + 1

            index += 1

        if arguments_start is None or arguments_end is None:
            out.append((line_number, prefix, None))
            continue

        arguments = text[arguments_start:arguments_end]
        literal = re.search(r'TEXT\s*\(\s*"([^"]*)"', arguments)

        if literal is not None:
            out.append((line_number, prefix, [literal.group(1)]))
            continue

        indexed = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*\[', arguments)

        if indexed is not None and indexed.group(1) in tables:
            out.append((line_number, prefix, tables[indexed.group(1)]))
            continue

        out.append((line_number, prefix, None))

    return out


def strip_comments(text):
    """Comments and string literals removed. The implementation is shared:
    stripping block comments before line comments meant a `/*` inside a `//`
    comment or inside a string literal could swallow real code. See
    common.strip_comments_and_strings."""
    return C.strip_comments(text)


def native_tags():
    """name -> tag string, from the one file allowed to define them."""
    if not os.path.isfile(NATIVE_SOURCE):
        return {}

    text = strip_comments(io.open(NATIVE_SOURCE, encoding="utf-8", errors="ignore").read())
    return {constant: tag for constant, tag in NATIVE_DECL.findall(text)}


def ini_tags():
    """The set of tags listed for designers in Config/DefaultGame.ini."""
    if not os.path.isfile(GAME_INI):
        return set()

    text = io.open(GAME_INI, encoding="utf-8", errors="ignore").read()
    return set(INI_DECL.findall(text))


def requested_tags(source_root=None):
    """(requests, skipped) - every tag the code asks for, and the ones it cannot.

    Covers two shapes: a literal passed straight to RequestGameplayTag, and a tag
    assembled with Printf from a prefix and a resolvable suffix. The second shape
    is where the interesting bugs are - it is how the archetype is chosen, and it
    is why "Intern" could sit in the code for months next to a registry that
    calls that archetype Graduate.
    """
    root = source_root or SOURCE_ROOT

    if not os.path.isdir(root):
        return [], 0

    requests = []
    skipped = 0

    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith((".cpp", ".h")):
                continue

            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, root)
            raw = io.open(path, encoding="utf-8", errors="ignore").read()

            for line_number, line in enumerate(raw.splitlines(), 1):
                code = line.split("//", 1)[0]

                for tag in REQUEST.findall(code):
                    if tag not in ("%s", "%s%s"):
                        requests.append((rel, line_number, tag))

            clean = strip_comments(raw)

            for line_number, prefix, suffixes in assembled_tags(clean):
                if suffixes is None:
                    skipped += 1     # a suffix from a travel option: not text-checkable
                    continue

                for suffix in suffixes:
                    # The prefix carries the conversion: substituting the suffix
                    # is what turns "OB.Character.Archetype.%s" + "Graduate" into
                    # a tag that can be looked up. Without this the assembled
                    # requests were silently dropped by the shape filter below,
                    # which made the whole check pass by doing nothing.
                    requests.append((rel, line_number, prefix.replace("%s", suffix)))

    return requests, skipped


def expand_documented(text):
    """(offset, tag) for every tag a document names, brace forms expanded.

    Two shapes count as a pattern rather than a tag and are skipped, with the
    count kept so the caller can report it: a wildcard (`OB.Character.Archetype.*`,
    which stands for a family) and a namespace with a trailing dot
    (`OB.Match.Phase.`, which stands for "everything under here").

    A brace form runs to its closing brace and may wrap over lines, which is why
    this takes the whole file rather than a line: `OB.Audio.State.{Floor.Lobby,\n
    Rain.Heavy}` is one entry, and a line-at-a-time reader would take the first
    half for a tag called `Floor.Lobby` and the second half for nothing.
    """
    out = []
    patterns = 0
    index = 0

    while index < len(text):
        found = text.find("OB.", index)

        if found == -1:
            break

        before = text[found - 1] if found > 0 else ""
        index = found + 3

        if before.isalnum() or before == "_":
            continue                      # part of a longer identifier

        end = found
        depth = 0

        while end < len(text):
            char = text[end]

            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    end += 1
                    break
            elif depth == 0 and (char in " \t\n`|,)(" or char == "*" or char == "%"):
                break

            end += 1

        token = text[found:end].strip()
        index = max(end, found + 3)

        if "*" in token or "%" in token or token.endswith("."):
            patterns += 1
            continue

        if "{" in token and token.endswith("}"):
            prefix, _, members = token.partition(".{")
            for member in members[:-1].split(","):
                member = member.strip()
                if member:
                    out.append((found, prefix + "." + member))
            continue

        if "{" in token:
            patterns += 1                  # an unclosed brace: not text-checkable
            continue

        out.append((found, token))

    return out, patterns


def documented_tags(doc_roots=None):
    """(relative_path, line_number, tag) for every tag the documentation names."""
    out = []
    patterns = 0
    roots = doc_roots if doc_roots is not None else DOC_ROOTS
    visited = set()

    for root in roots:
        if not os.path.isdir(root):
            continue

        for dirpath, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs
                       if d not in (".git", "Source", "Tools", "Content", "Saved", "Scripts")]

            for name in sorted(files):
                if not name.endswith(DOC_SUFFIXES):
                    continue

                path = os.path.normpath(os.path.join(dirpath, name))

                # The docs are reachable from two of the roots (the project root
                # walks into Docs/); a file read twice reports its faults twice,
                # and a fault reported twice looks like two faults.
                if path in visited:
                    continue

                visited.add(path)

                rel = os.path.relpath(path, python_root)
                text = io.open(path, encoding="utf-8", errors="ignore").read()

                found, skipped = expand_documented(text)
                patterns += skipped

                for offset, tag in found:
                    out.append((rel, text.count("\n", 0, offset) + 1, tag))

    return out, patterns


def run(source_root=None):
    """Returns (requests_checked, problems, warnings).

    problems is a list of (relative_path_or_blank, line_or_none, message).
    """
    declared = set(native_tags().values()) | ini_tags()
    requests, skipped = requested_tags(source_root)

    problems = []
    warnings = []

    for rel, line_number, tag in requests:
        if not TAG_SHAPE.match(tag):
            continue                       # an engine tag or a template, not ours

        if tag not in declared:
            problems.append((rel, line_number,
                             u"'{0}' is requested but never declared - the request returns an "
                             u"invalid tag and every query on it falls through".format(tag)))

    native = native_tags()
    listed = ini_tags()

    # Duplicate native declarations: the same name defined twice is a copy-paste
    # that compiles and silently keeps the first.
    seen = {}
    for constant, tag in native.items():
        seen.setdefault(tag, []).append(constant)

    for tag, constants in sorted(seen.items()):
        if len(constants) > 1:
            problems.append(("Core/OneBrahamGameplayTags.cpp", None,
                             u"'{0}' is declared {1} times ({2})".format(
                                 tag, len(constants), ", ".join(sorted(constants)))))

    for tag in sorted(set(native.values()) - listed):
        warnings.append((u"{0} is declared in C++ but not listed in DefaultGame.ini - "
                         u"designers cannot pick it, and the tag picker is how they are "
                         u"supposed to find it".format(tag)))

    # 4. Documented tags. A document that names a tag the project does not
    #    declare sends a reader into the tag picker to look for something that is
    #    not there, which ends one of two ways: they invent it, or they change a
    #    working tag to match the document.
    documented, doc_patterns = documented_tags()
    documented_names = set(tag for _rel, _line, tag in documented)

    for rel, line_number, tag in documented:
        if not TAG_SHAPE.match(tag):
            continue                       # an engine tag quoted in the docs

        if tag not in declared:
            problems.append((rel, line_number,
                             u"the documentation names '{0}' and this project never declares "
                             u"it".format(tag)))

    for tag in sorted(declared - documented_names):
        if TAG_SHAPE.match(tag):
            warnings.append((u"'{0}' is declared but appears in no document - the registry "
                             u"reference in Docs/03 is meant to list every tag".format(tag)))

    return len(requests), problems, warnings, skipped, len(documented_names), doc_patterns


class _TagReport(object):
    def __init__(self, checked, problems, warnings, skipped=0, documented=0):
        self.checked = checked
        self.problems = list(problems)
        self.warnings = list(warnings)
        self.skipped = skipped
        self.documented = documented

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} tag requests, {1} undeclared ({2})".format(
                self.checked, len(self.problems), self.problems[0][2][:60])
        return u"{0} tag requests, all declared".format(self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems, warnings, skipped, documented, doc_patterns = run()

    C.log(u"")
    C.log(u"  gameplay tag requests checked: {0}".format(checked))
    C.log(u"  tags named in the documentation checked: {0}".format(documented))

    if doc_patterns:
        C.log(u"  {0} documented tag pattern(s) skipped (a wildcard or a namespace: a family "
              u"cannot be looked up)".format(doc_patterns))

    if skipped:
        C.log(u"  {0} assembled request(s) not resolvable from text (a suffix from a travel "
              u"option or a profile). Skipped, and said so rather than counted as checked."
              .format(skipped))

    if not problems:
        C.log(u"  every tag this code asks for is one this project declares. OK")
    else:
        for rel, line_number, message in problems:
            where = u"{0}:{1}".format(rel, line_number) if line_number else rel
            C.log(u"      {0} - {1}".format(where, message))

    for message in warnings[:20]:
        C.log(u"      note: {0}".format(message))

    if len(warnings) > 20:
        C.log(u"      ... and {0} more note(s); see the report".format(len(warnings) - 20))

    return _TagReport(checked, problems, warnings, skipped, documented)


def main():
    checked, problems, warnings, skipped, documented, doc_patterns = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - gameplay tags")
    C.log(u"=" * 70)
    C.log(u"  requests checked: {0}".format(checked))
    C.log(u"  documented tags checked: {0}".format(documented))

    if doc_patterns:
        C.log(u"  documented patterns skipped: {0}".format(doc_patterns))

    if skipped:
        C.log(u"  skipped (assembled from a value only known at runtime): {0}".format(skipped))

    if not problems:
        C.log(u"  all declared. OK")
    else:
        C.log(u"  UNDECLARED: {0}".format(len(problems)))
        for rel, line_number, message in problems:
            where = u"{0}:{1}".format(rel, line_number) if line_number else rel
            C.log(u"  {0} - {1}".format(where, message))

    for message in warnings:
        C.log(u"  note: {0}".format(message))

    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
