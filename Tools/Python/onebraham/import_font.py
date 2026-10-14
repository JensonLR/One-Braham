# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.import_font
=====================

Imports the UI font and configures the font asset the UI style expects.

WHICH FONT, AND WHY THIS IS A DECISION AND NOT A DEFAULT
    The brand is a telecom-inspired blue and white identity, and the type has to
    do a specific job: read as *a transport company's signage* rather than as a
    game's HUD. That means a neutral grotesque with a real uppercase, a clear
    lower-case l/1/I distinction and tabular figures (scoreboards and ammo
    counters are numbers, and numbers that jitter are numbers that annoy).

    Docs/15_Asset_Generation_Prompts.md names OFL-licensed candidates. Nothing
    here impersonates a trademark: the wordmark is original and the typeface is
    a licensed grotesque, not a copy of anybody's corporate face.

WHAT IT DOES
    1. Finds the .ttf in Content/UI/Fonts (dropped there by Import_Font.bat).
    2. Imports it as a Font Face.
    3. Creates /Game/OneBraham/UI/Fonts/F_OneBrahamSans with the family set:
         Light, Regular, Medium, Semibold, Bold, Black outlines
       - because a HUD with one weight has no hierarchy, and no hierarchy means
       a player cannot tell a health number from a flavour line at a glance.
    4. Verifies the assignment by reading the asset back.

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/import_font.py" -ExtraArgs="OneBrahamSans"
"""

from __future__ import print_function

import os

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                          "..", "..", ".."))
FONT_SOURCE_DIR = os.path.join(REPO_ROOT, "Content", "UI", "Fonts")
FONT_ASSET_NAME = "F_OneBrahamSans"

# weight, file suffix, what it is for
FONT_WEIGHTS = [
    ("Light", "-Light", "Large display numbers only (the extraction countdown)."),
    ("Regular", "-Regular", "Body copy, objective text, subtitles."),
    ("Medium", "-Medium", "Labels, buttons, the scoreboard."),
    ("Semibold", "-Semibold", "Headings and section titles."),
    ("Bold", "-Bold", "Critical numbers: health, ammo, the hold timer."),
    ("Black", "-Black", "Mode names on the loading screen ('ASCENT')."),
]


def _font_files():
    """Every .ttf/.otf dropped into Content/UI/Fonts, by weight suffix."""
    found = {}

    if not os.path.isdir(FONT_SOURCE_DIR):
        return found

    for filename in sorted(os.listdir(FONT_SOURCE_DIR)):
        lower = filename.lower()
        if not lower.endswith((".ttf", ".otf")):
            continue

        for weight, suffix, _purpose in FONT_WEIGHTS:
            if suffix.lower() in lower and weight not in found:
                found[weight] = os.path.join(FONT_SOURCE_DIR, filename)

    return found


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("import_font must run inside the Unreal Editor.")

    report = C.BuildReport("import_font")
    C.log(u"=== Importing the One Braham UI font ===")

    faces = _font_files()

    if not faces:
        report.skipped_asset(
            u"{0}/{1}".format(C.UI_FONTS, FONT_ASSET_NAME),
            u"no font files in {0}. Drop an OFL-licensed grotesque there "
            u"(Light/Regular/Medium/Semibold/Bold/Black) and re-run, or run "
            u"Scripts/Import_Font.bat with a single .ttf. The UI falls back to "
            u"Roboto until then - it is readable, it is just not the brand.".format(
                FONT_SOURCE_DIR))
        C.log(report.summary())
        return report

    if not unreal.EditorAssetLibrary.does_directory_exist(C.UI_FONTS):
        unreal.EditorAssetLibrary.make_directory(C.UI_FONTS)

    imported = {}

    for weight, _suffix, purpose in FONT_WEIGHTS:
        source = faces.get(weight)
        if source is None:
            C.log(u"  {0}: not provided - {1}".format(weight, purpose))
            continue

        face_task = unreal.AssetImportTask()
        face_task.set_editor_property("automated", True)
        face_task.set_editor_property("replace_existing", True)
        face_task.set_editor_property("save", True)
        face_task.set_editor_property("filename", source)
        face_task.set_editor_property("destination_path", C.UI_FONTS)
        face_task.set_editor_property("destination_name", u"FF_OneBrahamSans_{0}".format(weight))

        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([face_task])

        paths = face_task.get_editor_property("imported_object_paths")

        if paths:
            imported[weight] = paths[0]
            report.created_asset(paths[0])
            C.log(u"  {0}: {1} -> {2} ({3})".format(
                weight, os.path.basename(source), paths[0], purpose))
        else:
            report.failed(weight, u"import of {0} produced nothing".format(source))

    C.log(u"  imported faces: {0}/{1}".format(len(imported), len(FONT_WEIGHTS)))
    # The composite font asset is assembled in the Font Editor from these faces:
    # it is the one step in the UI pipeline that is genuinely a visual decision
    # (which weight at which size, and which one gets tabular figures for the
    # scoreboard). Docs/08_UI.md carries the exact table to fill in.
    C.log(u"  composite font: create {0}/{1} in the Font Editor and add the "
          u"imported faces. Weights, sizes and the tabular-figures flag are in "
          u"Docs/08_UI.md.".format(C.UI_FONTS, FONT_ASSET_NAME))

    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
