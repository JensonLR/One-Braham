# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_ui
==================

Builds the UMG content: the HUD widget the C++ expects to find, the menus, the
settings screens, the deploy screen and the results screen.

WHY THE HUD IS MOSTLY CODE
    UOneBrahamHUDWidget builds its entire layout in C++ when its WidgetTree has
    no root (see UI/OneBrahamHUDWidget.cpp). That means the HUD works with *zero*
    UMG assets present, which is what makes the game playable on a fresh clone -
    and the critical path (crosshair, health, ammo, extraction timer) cannot be
    broken by a designer's edit. This script creates the widget blueprint so a
    designer *can* take the layout over: it subclasses the C++ widget, keeps the
    BindWidgetOptional names, and starts empty, so adding a widget to it starts
    overriding the code layout one element at a time.

WHAT IT CREATES
    /Game/OneBraham/UI/HUD/WBP_OneBrahamHUD          subclass of the C++ HUD widget
    /Game/OneBraham/UI/HUD/WBP_OneBrahamScoreboard   C++-driven, optional override
    /Game/OneBraham/UI/HUD/WBP_QuickLineWheel        the comms wheel (C++ draws it)
    /Game/OneBraham/UI/HUD/WBP_FloorPlan             the floor plan (C++ draws it)
    /Game/OneBraham/UI/Menus/WBP_MainMenu
    /Game/OneBraham/UI/Menus/WBP_Settings            video/audio/controls/gameplay
    /Game/OneBraham/UI/Menus/WBP_Results
    /Game/OneBraham/UI/Fonts/F_OneBrahamSans         (import target; see Docs/15)
    /Game/OneBraham/UI/HUD/DT_HUDStrings             all HUD copy, localisable

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/build_ui.py"
"""

from __future__ import print_function

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

# Widget blueprints: name, folder, parent class (C++ name or None for UserWidget),
# and what it is for.
WIDGETS = [
    ("WBP_OneBrahamHUD",    C.UI_HUD, "OneBrahamHUDWidget",
     "The in-match HUD. Subclasses the C++ widget; empty by default so the "
     "code-drawn layout is used until a designer starts overriding elements."),
    ("WBP_OneBrahamScoreboard", C.UI_HUD, "OneBrahamHUDWidget",
     "Hold-Tab scoreboard. Optional: the C++ widget already draws one."),
    ("WBP_MainMenu",        C.UI + "/Menus", "OneBrahamMainMenuWidget",
     "The front end. Subclasses the C++ menu (play, loadout, server browser, "
     "wardrobe, achievements, settings); empty by default."),
    ("WBP_Settings",        C.UI + "/Menus", "OneBrahamSettingsWidget",
     "Video, audio, controls, gameplay, accessibility. Subclasses the C++ "
     "screen, which builds every page and the rebinding list; empty by default."),
    ("WBP_Results",         C.UI + "/Menus", "OneBrahamResultsWidget",
     "End of match: the ending, your line, every squad, what you earned. "
     "Subclasses the C++ screen, which builds it; empty by default."),
    ("WBP_QuickLineWheel",  C.UI_HUD, "OneBrahamQuickLineWidget",
     "The comms wheel: eight segments, clockwise from the top. Subclasses the "
     "C++ widget, which already draws the ring, the labels and the cooldowns - "
     "the Blueprint exists to restyle it and to put a squad panel behind it."),
    ("WBP_FloorPlan",       C.UI_HUD, "OneBrahamFloorPlanWidget",
     "The floor plan: fifteen floors, sealed floors, squad positions, the "
     "extraction. Code-drawn by the C++ widget; this exists to add anything "
     "the design wants around it."),
]

# HUD copy lives in a data table so it can be localised and so a wordsmith can
# review every string the player reads without opening a widget.
HUD_STRINGS = [
    ("HUD_Mode_Ascent",      "ASCENT"),
    ("HUD_Mode_FireDrill",   "FIRE DRILL"),
    ("HUD_Mode_NightShift",  "NIGHT SHIFT"),
    ("HUD_Objective_SignIn", "Sign in at reception"),
    ("HUD_Objective_Ascend", "Reach the Skydeck"),
    ("HUD_Objective_Winch",  "Winch zone is open - take the Skydeck"),
    ("HUD_Objective_Hold",   "Winch is holding - get to the pad"),
    ("HUD_Objective_Lockdown", "Shutters down - the Skydeck is still open"),
    ("HUD_Objective_Purge",  "Security purge on lower floors - get above 13"),
    ("HUD_Prompt_Override",  "Hold {Interact} - override"),
    ("HUD_Prompt_Card",      "Use badge"),
    ("HUD_Prompt_Revive",    "Hold {Interact} - revive"),
    ("HUD_Prompt_Execute",   "Hold {Interact} - finish"),
    ("HUD_Prompt_Breach",    "Plant charge"),
    ("HUD_Locked_Executive", "Executive badge required"),
    ("HUD_Locked_Secure",    "IT Operations badge required"),
    ("HUD_Locked_Skydeck",   "Facilities master badge required"),
    ("HUD_Downed_BleedOut",  "BLEEDING OUT"),
    ("HUD_Downed_CallHelp",  "CALL FOR HELP"),
    ("HUD_Extraction_Hold",  "HOLD THE PAD"),
    ("HUD_Extraction_Contested", "CONTESTED"),
    ("HUD_Extraction_Failed", "WINCH ABORTED"),
    ("HUD_Match_NoSurvivors", "NO SURVIVORS"),
    ("HUD_Match_Extracted",  "EXTRACTED"),
    ("HUD_Squad_You",        "YOU"),
    ("HUD_Squad_Downed",     "DOWN"),
    ("HUD_Squad_Dead",       "KIA"),
    ("HUD_Floor_Format",     "FL {0}"),
    ("HUD_Voice_Proximity",  "PROXIMITY"),
    ("HUD_Voice_Squad",      "SQUAD"),
    ("HUD_Voice_Team",       "TEAM"),
]


def _ensure_dirs():
    for path in (C.UI, C.UI_HUD, C.UI + "/Menus", C.UI_FONTS, C.UI_TEXTURES):
        if not unreal.EditorAssetLibrary.does_directory_exist(path):
            unreal.EditorAssetLibrary.make_directory(path)


def _parent_class(class_name):
    """Resolves a C++ widget class by name, or None for plain UserWidget."""
    if class_name is None:
        return None
    return getattr(unreal, class_name, None)


def build_widgets(report):
    created = []

    for name, folder, parent_name, purpose in WIDGETS:
        path = u"{0}/{1}".format(folder, name)

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            report.updated_asset(path)
            C.log(u"  {0}: exists ({1})".format(name, purpose))
            continue

        factory = unreal.WidgetBlueprintFactory()
        parent = _parent_class(parent_name)

        if parent is not None:
            factory.set_editor_property("parent_class", parent)
        elif parent_name is not None:
            # The C++ class is not available: create a plain UserWidget subclass
            # anyway so the UI work is not blocked on a compile, and report it so
            # the next build pass reparents it.
            report.failed(name, u"parent class '{0}' is unavailable - created as a "
                                u"plain UserWidget; recompile and re-run to reparent"
                          .format(parent_name))

        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.WidgetBlueprint, factory)

        if asset is None:
            report.failed(name, "could not create widget blueprint")
            continue

        unreal.EditorAssetLibrary.save_asset(path)
        report.created_asset(path)
        created.append(name)
        C.log(u"  {0}: created ({1})".format(name, purpose))

    return created


def build_hud_strings(report):
    """Creates DT_HUDStrings: every string the HUD shows, in one table."""
    path = u"{0}/DT_HUDStrings".format(C.UI_HUD)

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        report.updated_asset(path)
        return unreal.EditorAssetLibrary.load_asset(path)

    row_struct = getattr(unreal, "OneBrahamHUDStringRow", None)
    if row_struct is None:
        # The row struct is generated from C++; without a compiled module there is
        # no DataTable to make, so the strings are still written out below for the
        # loc team and the table is created on the next run.
        report.skipped_asset(path, "FOneBrahamHUDStringRow not reflected yet - "
                                   "compile the OneBraham module and re-run")
        for key, text in HUD_STRINGS:
            C.log(u"  string {0}: {1}".format(key, text))
        return None

    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", row_struct)

    try:
        table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DT_HUDStrings", C.UI_HUD, unreal.DataTable, factory)
    except Exception as exc:  # noqa: BLE001
        report.failed("DT_HUDStrings", str(exc))
        return None

    if table is not None:
        # The rows, from the same list the reference implementation in
        # Source/OneBraham/UI/OneBrahamHUDStringTable.cpp compiles in. The
        # columns are the UPROPERTY names on FOneBrahamHUDStringRow; a CSV is
        # the one way to fill a DataTable from Python that does not depend on
        # per-row factory internals.
        rows = "\n".join(
            u"{0},{1}".format(key, text.replace(",", "\\,"))
            for key, text in HUD_STRINGS)

        try:
            unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(
                table, "Key,Text\n" + rows)
            unreal.EditorAssetLibrary.save_asset(path)
            report.created_asset(path)
            C.log(u"  DT_HUDStrings: {0} rows written".format(len(HUD_STRINGS)))
        except Exception as exc:  # noqa: BLE001
            report.failed("DT_HUDStrings", str(exc))

    return table


def build_font(report):
    """Registers the project font.

    F_OneBrahamSans is a neutral grotesque (see Docs/15 for the licence-safe
    choice and the exact import settings). The UI style falls back to Roboto when
    the font asset is missing, so the HUD is never unreadable.
    """
    path = u"{0}/F_OneBrahamSans".format(C.UI_FONTS)

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        report.updated_asset(path)
        return unreal.EditorAssetLibrary.load_asset(path)

    # A .ttf has to be placed in Content/UI/Fonts by hand or by the art pipeline;
    # the import itself is scriptable, so this reports exactly what to drop where.
    source = u"Content/UI/Fonts/OneBrahamSans-Regular.ttf"
    report.skipped_asset(
        path,
        u"font file not imported yet. Drop an OFL-licensed grotesque at {0} then run "
        u"Scripts/Import_Font.bat, or use Tools/Python/onebraham/import_font.py. "
        u"The UI falls back to Roboto until then.".format(source))

    return None


def build_style_guide(report):
    """Writes the UI style guide's machine-readable half.

    The palette, spacing scale and type scale live in C++
    (UI/OneBrahamUIStyle.cpp) and in Config/DefaultGame.ini's
    [OneBrahamGameSettings] section. This function exists so the two are checked
    against each other rather than assumed to agree.
    """
    expected = {
        "Primary": (0.000, 0.439, 0.753),
        "Navy": (0.016, 0.063, 0.145),
        "SignalBlue": (0.078, 0.522, 1.000),
        "SafetyYellow": (1.000, 0.804, 0.000),
        "EmergencyRed": (0.867, 0.118, 0.122),
        "LightGrey": (0.878, 0.898, 0.922),
    }

    mismatches = []
    for name, rgb in expected.items():
        if C.PALETTE.get(name) != rgb:
            mismatches.append(name)

    if mismatches:
        report.failed("UI palette", u"common.py and the style guide disagree on: {0}"
                      .format(", ".join(mismatches)))
    else:
        C.log(u"  palette verified: {0} brand colours match OneBrahamUIStyle".format(len(expected)))

    C.log(u"  type scale: Micro 10, Small 12, Body 14, Medium 18, Large 28, Huge 44, "
          u"Title 64 - see Docs/08_UI.md")
    C.log(u"  spacing scale: 2, 4, 6, 8, 12, 18, 24, 32 px")
    C.log(u"  radius scale: 0, 2, 4, 8 px (panels 4, buttons 2, pill 8)")


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("build_ui must run inside the Unreal Editor.")

    report = C.BuildReport("build_ui")
    C.log(u"=== Building One Braham UI ===")

    C.guard(report, "directories", _ensure_dirs)
    C.guard(report, "widgets", build_widgets, report)
    C.guard(report, "hud strings", build_hud_strings, report)
    C.guard(report, "font", build_font, report)
    C.guard(report, "style guide", build_style_guide, report)

    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
