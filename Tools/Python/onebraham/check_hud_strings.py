# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_hud_strings
===========================

Checks that the HUD's copy exists, and that something shows every line of it.

WHY THIS EXISTS
    `build_ui.py` created `DT_HUDStrings` with its `struct` property set to
    `FOneBrahamHUDStringRow` - a type that did not exist anywhere in the project.
    It wrote no rows, and nothing in C++ read the table. Docs/08 said the table
    "holds every string the HUD shows so it can be localised and reviewed by a
    writer without opening a widget", which was true of the list in the Python
    file and of nothing else.

    The table, the row struct and the compiled-in default are all real now
    (UI/OneBrahamHUDStringTable.h), and the C++ HUD reads the keys it shows.
    What this checker adds is the property that makes a copy table worth having:
    the three lists cannot drift, and no row sits in the table without something
    that displays it.

WHAT IT CHECKS
    1. The builder's `HUD_STRINGS` and the compiled-in table are the same keys in
       the same order with the same text.
    2. No key appears twice, in either list.
    3. Every `HUD_*` key referenced anywhere in `Source/` exists in the table.
       This is the rule that catches a rename in one place.
    4. Every key in the table is either referenced from C++ or listed in the
       "shown by the Blueprint layer" table in Docs/08 - no orphan rows.
    5. The builder actually fills the table it creates (`fill_data_table_from_csv_string`)
       and the CSV columns match the row struct's fields.
    6. `FOneBrahamHUDStringRow` is a `FTableRowBase` with the fields the CSV uses.

WHAT IT IGNORES
    Keys inside comments, and the word HUD in any other context.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_hud_strings.py
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
BUILDER = os.path.join(python_root, "Tools", "Python", "onebraham", "build_ui.py")
TABLE_H = os.path.join(python_root, "Source", "OneBraham", "UI", "OneBrahamHUDStringTable.h")
TABLE_CPP = os.path.join(python_root, "Source", "OneBraham", "UI", "OneBrahamHUDStringTable.cpp")
DOCS_08 = os.path.join(python_root, "Docs", "08_UI.md")

#: A key as the code asks the table for it. Deliberately not a bare `HUD_*`
#: word: the C++ HUD also uses `HUD_*` as the *namespace key* of its own
#: NSLOCTEXT literals (`NSLOCTEXT("OneBraham", "HUD_Health", "HEALTH")`), which
#: are compile-time strings and have nothing to do with this table. The first
#: draft of this check matched those and reported twelve lookups of rows that do
#: not exist - the check was wrong, not the code.
LOOKUP = re.compile(
    r'(?:'
    r'OneBrahamHUDStrings::(?:Get|Has)\(\s*TEXT\("(HUD_[A-Za-z0-9_]+)"\)'
    r'|'
    # The HUD component's own wrappers, which are where its call sites put the key.
    r'Announce(?:Sub)?\(\s*TEXT\("(HUD_[A-Za-z0-9_]+)"\)'
    r')')

#: A bare key, used for reading the document and the two lists.
KEY = re.compile(r'\b(HUD_[A-Za-z0-9_]+)\b')

#: A row of the compiled-in table: { TEXT("HUD_X"), NSLOCTEXT("OneBraham", "HUD_X", "Text") },
CPP_ROW = re.compile(
    r'\{\s*TEXT\("(HUD_[A-Za-z0-9_]+)"\),\s*NSLOCTEXT\(\s*"[^"]*",\s*"[^"]*",\s*"([^"]*)"\)\s*\}')

#: One row of the builder's list: ("HUD_X", "Text"),
PY_ROW = re.compile(r'\(\s*"(HUD_[A-Za-z0-9_]+)"\s*,\s*"([^"]*)"\s*\)')

#: The row struct's fields, in declaration order.
STRUCT_FIELD = re.compile(r'UPROPERTY\([^)]*\)\s*(FName|FText|FString|int32|float)\s+(\w+)\s*(?:=|;)')

#: The builder's CSV header.
CSV_HEADER = re.compile(r'fill_data_table_from_csv_string\(\s*\w+\s*,\s*"([^"]+)\\n"')

#: The document's Blueprint-layer table rows: | `HUD_A`, `HUD_B` | shown by ... |
DOC_ROW = re.compile(r'^\|\s*((?:`HUD_[A-Za-z0-9_]+`[,\s]*)+)\s*\|', re.MULTILINE)


def read(path):
    if not os.path.isfile(path):
        return ""
    return io.open(path, encoding="utf-8", errors="ignore").read()


def builder_rows():
    """[(key, text)] from build_ui.py's HUD_STRINGS, in order."""
    text = read(BUILDER)

    if "HUD_STRINGS = [" not in text:
        return []

    block = text[text.index("HUD_STRINGS = ["):]
    block = block[:block.index("\n]\n")]

    return PY_ROW.findall(block)


def compiled_rows():
    """[(key, text)] from the compiled-in table, in order."""
    return CPP_ROW.findall(read(TABLE_CPP))


def cpp_referenced_keys(source_root=None):
    """{key: [(rel_path, line)]} for every key the code asks the table for."""
    root = source_root or SOURCE_ROOT
    out = {}

    if not os.path.isdir(root):
        return out

    for dirpath, _dirs, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith((".h", ".cpp")):
                continue

            path = os.path.join(dirpath, name)

            # strip_comments, not strip_comments_and_strings: the keys are
            # literals, and a check that blanks literals before matching them
            # finds nothing and passes.
            clean = C.strip_comments(read(path))
            rel = os.path.relpath(path, python_root)

            for number, line in enumerate(clean.splitlines(), 1):
                # The table's own definition names every key; it is the one file
                # where naming a key is not a reference to it.
                if rel.endswith("OneBrahamHUDStringTable.cpp") or rel.endswith("OneBrahamHUDStringTable.h"):
                    continue

                for match in LOOKUP.finditer(line):
                    key = next((group for group in match.groups() if group), None)

                    if key is not None:
                        out.setdefault(key, []).append((rel, number))

    return out


def documented_keys():
    """Keys Docs/08 attributes to the Blueprint layer."""
    text = read(DOCS_08)

    if "### `DT_HUDStrings`" not in text:
        return None

    section = text[text.index("### `DT_HUDStrings`"):]
    section = section[:section.index("\n## ")] if "\n## " in section else section

    keys = set()

    for row in DOC_ROW.findall(section):
        keys.update(KEY.findall(row))

    return keys


def run(source_root=None):
    """Returns (checks, problems)."""
    problems = []
    checks = 0

    built = builder_rows()
    compiled = compiled_rows()

    checks += 1

    if not built:
        problems.append((u"build_ui.py's HUD_STRINGS was not found or has moved",
                         u"builder"))
    if not compiled:
        problems.append((u"the compiled-in HUD string table was not found - the HUD "
                         u"would show nothing", u"compiled-in table"))

    if not built or not compiled:
        return checks, problems

    # 1. same keys, same order, same text
    checks += 1

    if len(built) != len(compiled):
        problems.append((
            u"the builder declares {0} HUD strings and the compiled-in table has "
            u"{1}".format(len(built), len(compiled)), u"table drift"))

    for index, (builder_row, compiled_row) in enumerate(zip(built, compiled)):
        checks += 1

        if builder_row[0] != compiled_row[0]:
            problems.append((
                u"row {0}: the builder has '{1}' and the compiled-in table has "
                u"'{2}'".format(index + 1, builder_row[0], compiled_row[0]),
                u"table drift"))
            continue

        if builder_row[1] != compiled_row[1]:
            problems.append((
                u"{0}: the builder's text is \"{1}\" and the compiled-in table's is "
                u"\"{2}\"".format(builder_row[0], builder_row[1], compiled_row[1]),
                u"text drift"))

    # 2. no duplicates
    for label, rows in ((u"the builder", built), (u"the compiled-in table", compiled)):
        checks += 1
        seen = set()
        duplicates = []

        for key, _text in rows:
            if key in seen:
                duplicates.append(key)
            seen.add(key)

        if duplicates:
            problems.append((
                u"{0} lists {1} more than once".format(label, u", ".join(sorted(set(duplicates)))),
                u"duplicate key"))

    table_keys = set(key for key, _text in compiled)

    # 3. every key referenced in the source exists
    references = cpp_referenced_keys(source_root)

    for key in sorted(references):
        checks += 1

        if key not in table_keys:
            where = references[key][0]
            problems.append((
                u"{0}:{1} asks for HUD string '{2}' and the table has no such row "
                u"- OneBrahamHUDStrings::Get returns empty and logs".format(
                    where[0], where[1], key), u"unknown key"))

    # 4. every table key is shown by something
    blueprint_keys = documented_keys()

    if blueprint_keys is None:
        problems.append((u"Docs/08 no longer has the DT_HUDStrings section, so no key "
                         u"ownership can be checked", u"documentation"))
    else:
        for key in sorted(table_keys):
            checks += 1

            if key in references or key in blueprint_keys:
                continue

            problems.append((
                u"'{0}' is in the copy table and is neither referenced from C++ nor "
                u"listed in Docs/08's Blueprint-layer table: nothing shows it".format(key),
                u"orphan key"))

        # and the document's list must be real keys
        for key in sorted(blueprint_keys - table_keys):
            checks += 1
            problems.append((
                u"Docs/08 lists '{0}' as a Blueprint-layer key and the copy table has "
                u"no such row".format(key), u"documentation"))

    # 5. the builder fills what it creates, with the struct's own column names
    builder_text = read(BUILDER)
    checks += 1

    if "fill_data_table_from_csv_string" not in builder_text:
        problems.append((u"build_ui.py creates DT_HUDStrings without writing any rows "
                         u"into it", u"empty table"))
    else:
        header = CSV_HEADER.search(builder_text)
        checks += 1

        if header is None:
            problems.append((u"the builder's CSV header could not be read", u"empty table"))
        else:
            columns = [c.strip() for c in header.group(1).split(",")]
            fields = [field for _type, field in STRUCT_FIELD.findall(read(TABLE_H))]

            if columns != fields:
                problems.append((
                    u"the builder writes CSV columns {0} and FOneBrahamHUDStringRow's "
                    u"fields are {1}".format(columns, fields), u"row struct"))

    # 6. the row struct is a DataTable row
    checks += 1

    if "FTableRowBase" not in read(TABLE_H):
        problems.append((u"FOneBrahamHUDStringRow does not derive from FTableRowBase, so "
                         u"no DataTable can be built on it", u"row struct"))

    return checks, problems


class _HUDStringsReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} HUD string checks, {1} problems ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} HUD string checks, {1} rows, every one shown".format(
            self.checked, len(compiled_rows()))


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()
    references = cpp_referenced_keys()
    blueprint = documented_keys() or set()

    C.log(u"")
    C.log(u"  HUD strings: {0} checks ({1} rows, {2} referenced from C++, {3} through "
          u"the Blueprint layer)".format(
              checked, len(compiled_rows()), len(references), len(blueprint)))

    if not problems:
        C.log(u"  the builder, the compiled-in table and the code that shows them "
              u"agree. OK")
    else:
        for message, kind in problems:
            C.log(u"      [{0}] {1}".format(kind, message))

    return _HUDStringsReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - HUD copy")
    C.log(u"=" * 70)
    C.log(u"  checks: {0}".format(checked))

    if not problems:
        C.log(u"  every key exists and something shows it. OK")
        return 0

    C.log(u"  PROBLEMS: {0}".format(len(problems)))
    for message, kind in problems:
        C.log(u"  [{0}] {1}".format(kind, message))

    return 1


if __name__ == "__main__":
    sys.exit(main())
