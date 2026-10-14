# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_soft_paths
==========================

Checks the asset paths the C++ loads by name against the naming contract the
content pipeline writes.

WHY THIS EXISTS
    Everything this game loads by string is a soft reference: the weapon
    registry, the character registry, the audio bank, every sound class, the HUD
    widget, the camera shakes, the maps. A soft reference is the right choice -
    a hard reference from C++ to a Blueprint would break the cook - and it has
    one failure mode that nothing else in the pipeline can see:

        the path is wrong, the load returns null, and the game continues.

    That is the whole problem. A typo in `/Game/OneBraham/UI/HUD/WBP_OneBrahamHUD`
    produces no error, no warning and no missing asset in the content browser. It
    produces a HUD that silently falls back to the C++ layout, or a music cue
    that never plays, or a camera shake that never shakes, and the only symptom
    is that something feels flat.

    This check compares every `/Game/...` literal in `Source/OneBraham` against
    two things the pipeline guarantees, both declared in `common.py`:

      1. **The directory.** Every asset lives under one of the content
         directories the builders write to (`UI/HUD`, `Audio/Mix`, `VFX`, ...). A
         path pointing somewhere no builder writes is a path nothing will ever
         create.
      2. **The name prefix.** Every asset name starts with the prefix for its
         type (`BP_`, `DA_`, `NS_`, `WBP_`, `SC_`, `L_`, ...). A C++ path naming
         `WeaponRegistry` where the pipeline writes `DA_WeaponRegistry` is exactly
         the silent-fallback bug, and it is invisible in every other check.

    Format strings are handled: a path containing `%s` has a variable leaf, so
    only its directory and any literal prefix before the first `%` are checked.

WHAT IT CANNOT DO
    It cannot know that an asset exists - that is what the editor-side check in
    validate_project does, by asking the asset registry. This check runs on a
    machine with no engine, which is why it is worth having.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_soft_paths.py
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
TOOLS_ROOT = os.path.join(python_root, "Tools", "Python")

GAME_PATH = re.compile(r'"(/Game/[^"]*)"')


def content_directories():
    """Every directory the pipeline writes to, derived from common.py."""
    directories = set()

    for name in dir(C):
        if name.isupper() and isinstance(getattr(C, name), str):
            value = getattr(C, name)
            if value.startswith("/Game/"):
                directories.add(value)

    return directories


def known_prefixes():
    """Every asset-name prefix the naming contract allows."""
    prefixes = set(C.PREFIXES.values())
    prefixes.add(C.PREFIX_ANY)

    # Levels are named L_ and the contract's table omits the row, which is how a
    # map called Level_Main got into the project once. Add it here and let the
    # check report any asset that matches nothing.
    prefixes.add("L_")

    return prefixes


def split_path(path):
    """Returns (directory, asset_name, object_name or None) for a game path."""
    path = path.split("?")[0]          # strip a map's options suffix
    object_name = None

    if "." in os.path.basename(path):
        asset_part, object_name = path.rsplit(".", 1)
    else:
        asset_part = path

    directory = os.path.dirname(asset_part)
    return directory, os.path.basename(asset_part), object_name


def pipeline_directory_names(tools_root=None):
    """Every directory name the content pipeline mentions anywhere.

    Collected by splitting every string literal in Tools/Python on "/", so that
    `C.UI_FONTS = UI + "/Fonts"` contributes both "UI" and "Fonts". This is the
    vocabulary of places the pipeline can put an asset, and it is deliberately
    literal: if no script has ever typed a folder's name, no script will ever
    create it.

    The first version of this check accepted any subdirectory up to three levels
    below a content directory, which meant a C++ path pointing at `Audio/Mixer`
    passed happily even though the mix assets live in `Audio/Mix`. A rule that
    cannot tell those apart is not a check.
    """
    root = tools_root or TOOLS_ROOT
    names = set()

    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith(".py"):
                continue

            text = io.open(os.path.join(dirpath, name), encoding="utf-8", errors="ignore").read()

            for literal in re.findall(r'"([^"\n]*)"', text):
                for piece in literal.split("/"):
                    piece = piece.strip()
                    if piece and piece.isidentifier():
                        names.add(piece)

    return names


def missing_directory_segments(directory, known_directories):
    """The segments of a path below the content root that no script names."""
    parts = directory.split("/")

    # /Game/OneBraham/...
    if len(parts) < 4:
        return [directory]

    # An exact match on a directory constant is the common case and needs no
    # further checking: /Game/OneBraham/Audio/Mix is C.AUDIO_MIX.
    if directory in known_directories:
        return []

    extra = parts[3:] if parts[2] == "OneBraham" else parts[2:]

    return [segment for segment in extra if segment and segment not in known_directories]


def run(source_root=None):
    """Returns (paths_checked, problems).

    problems is a list of (relative_path, the_path, message).
    """
    root = source_root or SOURCE_ROOT

    if not os.path.isdir(root):
        return 0, []

    known_dirs = content_directories() | pipeline_directory_names()
    prefixes = known_prefixes()

    if not known_dirs or not prefixes:
        return 0, []

    checked = 0
    problems = []

    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith((".cpp", ".h")):
                continue

            path = os.path.join(dirpath, name)
            text = io.open(path, encoding="utf-8", errors="ignore").read()
            rel = os.path.relpath(path, root)

            for line_number, line in enumerate(text.splitlines(), 1):
                # Comments carrying an example path are not loads.
                stripped = line.split("//", 1)[0]

                for match in GAME_PATH.finditer(stripped):
                    literal = match.group(1)

                    # An engine path is not this project's business.
                    if not literal.startswith("/Game/OneBraham"):
                        continue

                    checked += 1
                    directory, asset_name, _object_name = split_path(literal)

                    unknown_segments = missing_directory_segments(directory, known_dirs)

                    if unknown_segments:
                        problems.append((rel, literal,
                                         u"nothing in the content pipeline names {0} - the load will "
                                         u"return null and the game will carry on".format(
                                             "/".join(unknown_segments))))
                        continue

                    # Format strings have a variable leaf: check the literal part
                    # in front of the first conversion.
                    literal_part = asset_name.split("%", 1)[0]

                    if not literal_part:
                        continue          # the whole name is a format argument

                    if not any(literal_part.startswith(prefix) for prefix in prefixes):
                        problems.append((rel, literal,
                                         u"asset name does not start with a known prefix "
                                         u"(expected one of {0})".format(
                                             ", ".join(sorted(prefixes)))))

    return checked, problems


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()

    C.log(u"")
    C.log(u"  soft asset paths checked: {0}".format(checked))

    if not problems:
        C.log(u"  every /Game/ path matches a directory and a name prefix the pipeline writes. OK")
    else:
        C.log(u"  SUSPECT: {0}".format(len(problems)))
        for rel, literal, message in problems:
            C.log(u"      {0}: {1} - {2}".format(rel, literal, message))

    return _PathReport(checked, problems)


class _PathReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} paths, {1} suspect ({2})".format(
                self.checked, len(self.problems), self.problems[0][1])
        return u"{0} soft paths, all conventional".format(self.checked)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - soft asset paths")
    C.log(u"=" * 70)
    C.log(u"  paths checked: {0}".format(checked))

    if not problems:
        C.log(u"  all conventional. OK")
        return 0

    C.log(u"  SUSPECT: {0}".format(len(problems)))
    for rel, literal, message in problems:
        C.log(u"  {0}: {1} - {2}".format(rel, literal, message))

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
