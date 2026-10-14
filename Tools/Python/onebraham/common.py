# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.common
================

Shared vocabulary for every build script: where content lives, what things are
called, the exact numbers the level is built from, and a small set of helpers for
creating and saving assets.

Nothing in this module touches the world. It is imported by the builders.
"""

from __future__ import print_function

import os
import re
import sys
import traceback

# ---------------------------------------------------------------------------
#  Unreal
# ---------------------------------------------------------------------------
# The scripts are written to fail loudly in a plain Python interpreter and work
# inside the editor. Importing unreal at module scope is what makes that possible:
# a syntax check on a CI box without the engine still succeeds.
try:
    import unreal
    IN_EDITOR = True
except ImportError:  # pragma: no cover - only hit outside the editor
    unreal = None
    IN_EDITOR = False

# ---------------------------------------------------------------------------
#  Content paths
# ---------------------------------------------------------------------------
GAME_ROOT = "/Game/OneBraham"

MAPS = GAME_ROOT + "/Maps"
ENVIRONMENT = GAME_ROOT + "/Environment"
MATERIALS = GAME_ROOT + "/Materials"
MATERIALS_INSTANCES = MATERIALS + "/Instances"
PROPS = GAME_ROOT + "/Props"
CHARACTERS = GAME_ROOT + "/Characters"
# Interaction and shared animation montages. Loaded by name at runtime from
# UOneBrahamInteractComponent::PlayInteractionMontage, so this constant and that
# string have to agree - check_soft_paths.py verifies exactly that.
CHARACTER_ANIMS = CHARACTERS + "/Shared/Anims"
WEAPONS = GAME_ROOT + "/Weapons"
VFX = GAME_ROOT + "/VFX"
AUDIO = GAME_ROOT + "/Audio"
AUDIO_BANKS = AUDIO + "/Banks"
AUDIO_MIX = AUDIO + "/Mix"
AUDIO_MUSIC = AUDIO + "/Music"
AUDIO_AMBIENCE = AUDIO + "/Ambience"
UI = GAME_ROOT + "/UI"
UI_HUD = UI + "/HUD"
UI_FONTS = UI + "/Fonts"
UI_TEXTURES = UI + "/Textures"
INPUT = GAME_ROOT + "/Input"
BLUEPRINTS = GAME_ROOT + "/Blueprints"
DATA = GAME_ROOT + "/Data"
VOICE = GAME_ROOT + "/Voice"

# Map names. Referenced from DefaultEngine.ini and the packaging lists, so these
# strings are load-bearing in three places at once - do not rename casually.
MAP_PERSISTENT = "L_Persistent_OneBraham"
MAP_FIRE_DRILL = "L_FireDrill_Arena"
MAP_MAIN_MENU = "L_MainMenu"
MAP_LOADING = "L_Loading"

# ---------------------------------------------------------------------------
#  Naming conventions
# ---------------------------------------------------------------------------
# Prefix -> what it is. This table is the naming contract: the asset builders
# name assets with it, check_soft_paths.py verifies that every /Game/ path in the
# C++ starts with one of these prefixes and lives under a directory the builders
# write to, and a stray asset named without a prefix cannot be searched for.
#
# Rows are added when a new asset type appears, which is why MusicQue, AmbienceBed,
# PhysicalMaterial, DataTable and Level are here: the C++ was loading MUS_* music
# cues and L_* levels by name long before the contract mentioned them.
PREFIXES = {
    "StaticMesh": "SM_",
    "SkeletalMesh": "SK_",
    "Material": "M_",
    "MaterialInstanceConstant": "MI_",
    "MaterialParameterCollection": "MPC_",
    "PhysicalMaterial": "PM_",
    "Texture2D": "T_",
    "NiagaraSystem": "NS_",
    "NiagaraEmitter": "NE_",
    "SoundWave": "SFX_",
    "SoundCue": "SC_",
    "SoundClass": "SC_",
    "SoundMix": "SM_",
    "SoundAttenuation": "SA_",
    "SoundSubmix": "SS_",
    "MusicQue": "MUS_",
    "AmbienceBed": "AMB_",
    "Blueprint": "BP_",
    "WidgetBlueprint": "WBP_",
    "AnimBlueprint": "ABP_",
    "AnimSequence": "AS_",
    "AnimMontage": "AM_",
    "DataAsset": "DA_",
    "DataTable": "DT_",
    "LevelSequence": "LS_",
    "Level": "L_",
    "Font": "F_",
    "InputAction": "IA_",
    "InputMappingContext": "IMC_",
}

PREFIX_ANY = "OB_"

# ---------------------------------------------------------------------------
#  The building
# ---------------------------------------------------------------------------
# One Braham, Aldgate: fifteen floors of 400 cm, lobby at Z = 0.
FLOOR_HEIGHT_CM = 400.0
FLOOR_COUNT = 15

# The eight named areas of the map spec (Docs/09). Each entry is
# (name, (first_floor, last_floor), min_corner_x, min_corner_y, size_x, size_y):
# the footprint's MINIMUM corner, not its centre - read rows with area_centre().
# The tower is a rectangular core with four wings around a central lift core,
# which is what gives every floor a chokepoint, a flank and a sightline.
AREAS = [
    # name,              floors,        x,     y,     size x, size y
    ("Ground_Plaza",     (0, 0),        -2600, -2200, 5200,  4400),
    ("Lobby",            (1, 1),        -2200, -1800, 4400,  3600),
    ("Mezzanine",        (2, 2),        -2000, -1600, 4000,  3200),
    ("OpenPlan",         (3, 8),        -2200, -1800, 4400,  3600),
    ("ServerComms",      (9, 11),       -1600, -1400, 3200,  2800),
    ("Executive",        (12, 12),      -1800, -1500, 3600,  3000),
    ("Canteen",          (13, 14),      -2000, -1600, 4000,  3200),
    ("Skydeck",          (15, 15),      -2400, -2000, 4800,  4000),
]

AREA_NAMES = [a[0] for a in AREAS]


def area_centre(area):
    """(name, floors, centre_x, centre_y, size_x, size_y) for one AREAS row.

    AREAS stores the minimum corner of each footprint: (-2600, -2200) with a
    5200 x 4400 footprint is a plaza centred on the origin. Every builder places
    things about the area's *centre* (slabs, rooms, lights, volumes, nav bounds,
    props), while the lift core, the spawn anchors and the extraction pad are
    placed about the world origin. Handing the corner to code that wants the
    centre built every floor half a footprint south-west of its own lifts, so
    this is the only way the builders read an area.
    """
    name, floors, min_x, min_y, size_x, size_y = area
    return name, floors, min_x + size_x / 2.0, min_y + size_y / 2.0, size_x, size_y

def AREAS_AS_PAIRS():
    """(name, (first_floor, last_floor)) for every area, in build order.

    Exists so a check can ask the map what it builds without importing the four
    hundred lines of the environment builder that spawn it. check_building_layout
    compares this against the game's own layout table, which is how the floor
    plan stops naming the wrong area.
    """
    return [(name, floors) for name, floors, _x, _y, _sx, _sy in AREAS]

# Core / circulation dimensions.
LIFT_CORE_SIZE_CM = 1200.0          # 6 x 6 m lift core
LIFT_CAR_SIZE_CM = 220.0
CORRIDOR_WIDTH_CM = 260.0
DOOR_WIDTH_CM = 110.0
DOOR_HEIGHT_CM = 215.0
STAIRWELL_SIZE_CM = 800.0
STAIRWELL_FLIGHTS_PER_FLOOR = 2
SERVER_ROW_SPACING_CM = 180.0
DESK_GRID_CM = 180.0
CEILING_HEIGHT_CM = 400.0
WINDOW_SPACING_CM = 300.0

# ---------------------------------------------------------------------------
#  The service core: the same on every floor, plaza to Skydeck.
#
#  The game is a climb, and the core is how the climb works: two stairwells and
#  a lift bank in one band along the north of every floor, behind a corridor
#  that every floor keeps clear. Rooms, spawns, doors and terminals are laid out
#  around it, and check_building_layout fails if anything is placed inside it
#  that is not part of it. (The first layout put a 12 m core in the middle of
#  every floor, under the boardroom, the canteen and the helipad, with solid
#  boxes for stair landings and lift doors and no hole in any slab: nobody
#  could get from one floor to the next.)
# ---------------------------------------------------------------------------
#: (x min, x max, y min, y max) of the core band.
CORE_BAND = (-1500.0, 1500.0, 500.0, 1300.0)
#: The corridor in front of the core, kept clear of rooms on every floor.
CORE_CORRIDOR = (-1500.0, 1500.0, 300.0, 500.0)

#: Stairwells: (name, centre x). Each is STAIRWELL_SIZE_CM square, centred in
#: the band's depth, with its door in the middle of its south (corridor) wall.
STAIRWELLS = [("A", -1100.0), ("B", 1100.0)]
STAIRWELL_DOOR_Y = CORE_BAND[2]

#: Two flights a storey: 11 risers of 18.2 cm and 11 treads of 28 cm each, a
#: 200 cm floor-level landing by the door and a mid-landing at the back.
STAIR_RISERS_PER_FLIGHT = 11
STAIR_TREAD_CM = 28.0
STAIR_LANDING_DEPTH_CM = 200.0

#: Lift shafts: (name, centre x, centre y), each LIFT_SHAFT_SIZE_CM square
#: inside, running LIFT_FLOORS and opening onto the corridor at LIFT_STOPS.
LIFT_SHAFT_SIZE_CM = 260.0
LIFT_SHAFTS = [("L1", -150.0, 930.0), ("L2", 150.0, 930.0)]
LIFT_FLOORS = (1, 9)
LIFT_STOPS = (1, 3, 9)
#: The lift lobby: the alcove in front of the shafts, x and y range.
LIFT_LOBBY = (-300.0, 300.0, 500.0, 800.0)


def core_holes(floor_index):
    """(x0, x1, y0, y1) holes this floor's slab needs.

    A stairwell's slab is cut behind its floor-level landing, so the flights
    from the storey below come up through it. A lift shaft is cut on every
    floor the shaft passes, so the car can.
    """
    holes = []
    if floor_index > 0:
        for _name, cx in STAIRWELLS:
            holes.append((cx - STAIRWELL_SIZE_CM / 2.0, cx + STAIRWELL_SIZE_CM / 2.0,
                          CORE_BAND[2] + STAIR_LANDING_DEPTH_CM, CORE_BAND[3]))
    if LIFT_FLOORS[0] < floor_index <= LIFT_FLOORS[1]:
        for _name, cx, cy in LIFT_SHAFTS:
            half = LIFT_SHAFT_SIZE_CM / 2.0
            holes.append((cx - half, cx + half, cy - half, cy + half))
    return holes


def subtract_rects(rect, holes):
    """rect minus every hole, as a list of rectangles. All are (x0, x1, y0, y1).

    Used for the slabs and the ceilings: a box per remaining piece is real,
    walkable geometry with no boolean meshes.
    """
    pieces = [rect]
    for hx0, hx1, hy0, hy1 in holes:
        next_pieces = []
        for x0, x1, y0, y1 in pieces:
            if hx1 <= x0 or hx0 >= x1 or hy1 <= y0 or hy0 >= y1:
                next_pieces.append((x0, x1, y0, y1))
                continue
            cx0, cx1 = max(x0, hx0), min(x1, hx1)
            cy0, cy1 = max(y0, hy0), min(y1, hy1)
            if y0 < cy0:
                next_pieces.append((x0, x1, y0, cy0))
            if cy1 < y1:
                next_pieces.append((x0, x1, cy1, y1))
            if x0 < cx0:
                next_pieces.append((x0, cx0, cy0, cy1))
            if cx1 < x1:
                next_pieces.append((cx1, x1, cy0, cy1))
        pieces = next_pieces
    return [p for p in pieces if p[1] - p[0] > 1.0 and p[3] - p[2] > 1.0]


# Spawn safety: no spawn point may be within this distance of an enemy's.
SPAWN_MIN_ENEMY_DISTANCE_CM = 2000.0
SPAWN_MIN_SAME_TEAM_DISTANCE_CM = 600.0

# Gameplay anchors.
# On the Skydeck slab: floor N stands at (N - 1) x 400 (Docs/09 section 1), so
# floor 15 is 5600. This was 15 x 400 = 6000, a storey above the deck it sits on.
EXTRACTION_PAD_LOCATION = (0.0, -800.0, 14.0 * FLOOR_HEIGHT_CM)
EXTRACTION_HOLD_RADIUS_CM = 900.0
CANTEEN_SKYDECK_STAIR_XY = (900.0, -700.0)

# ---------------------------------------------------------------------------
#  Brand
# ---------------------------------------------------------------------------
# Telecom-inspired: blue, white, deep navy, light grey, signal blue, safety
# yellow for warnings, emergency red for alarms. Mirrored in
# Config/DefaultGame.ini [OneBrahamGameSettings] so the C++ and the content
# agree on the same seven colours.
PALETTE = {
    "Primary":       (0.000, 0.439, 0.753),
    "Navy":          (0.016, 0.063, 0.145),
    "SignalBlue":    (0.078, 0.522, 1.000),
    "SafetyYellow":  (1.000, 0.804, 0.000),
    "EmergencyRed":  (0.867, 0.118, 0.122),
    "LightGrey":     (0.878, 0.898, 0.922),
    "White":         (1.000, 1.000, 1.000),
}

# ---------------------------------------------------------------------------
#  Parsing C++ class bodies
# ---------------------------------------------------------------------------
# Two checkers need this - check_uht_contracts.py to attribute UPROPERTY and
# UFUNCTION specifiers, check_calls.py to read declarations - and it took three
# iterations to get right, so it lives in one place.
MACRO_START = re.compile(r"^([A-Z][A-Z0-9_]*)\s*\(")
ACCESS_SPECIFIER = re.compile(r"^\s*(?:public|protected|private)\s*:\s*")


def strip_comments(text):
    """Comments removed, string literals left alone, line count preserved.

    Two contracts live here because two kinds of check need them. The scanners
    that read code structure (declarations, calls, brace balance) want strings
    gone. The scanners that read string literals - the gameplay tags, the voice
    line ids, the input action names - want them kept, and removing them turns
    those checks into checks that inspect nothing and therefore pass.
    """
    return _strip(text, blank_strings=False)


def strip_comments_and_strings(text):
    """Comments and string literals removed, line count preserved.

    ONE PASS, LEFT TO RIGHT, like a compiler - because the version this replaces
    was four regexes in the wrong order, and it could delete real code from the
    analysis while reporting that everything was fine:

        text = re.sub(r'/\\*.*?\\*/', ..., text, flags=re.DOTALL)
        text = re.sub(r'//[^\\n]*', "", text)

    A `/*` inside a line comment - or inside a string literal, which is how the
    project's own scripts are described ("Scripts/*.bat") - opened a block
    comment that ran until the next `*/` anywhere in the file. Every brace,
    declaration and call between those two points was removed before any check
    looked at it. The file's delimiters then did not balance, and the check that
    noticed was the cheapest one in the suite.

    Contract: braces, parentheses and brackets outside comments and strings are
    unchanged; every newline is preserved so line numbers still mean something;
    string and character literals are replaced by their empty forms.
    """

    return _strip(text, blank_strings=True)


def _strip(text, blank_strings):
    """One pass, left to right. `blank_strings` replaces literals with ""."""
    out = []
    index = 0
    length = len(text)

    while index < length:
        char = text[index]

        # Line comment: to the end of the line, newline kept.
        if char == "/" and index + 1 < length and text[index + 1] == "/":
            while index < length and text[index] != "\n":
                index += 1

        # Block comment: newlines kept so line numbers survive.
        elif char == "/" and index + 1 < length and text[index + 1] == "*":
            index += 2
            while index < length and not (text[index] == "*" and index + 1 < length
                                          and text[index + 1] == "/"):
                if text[index] == "\n":
                    out.append("\n")
                index += 1
            index += 2

        # String literal, including an escaped quote inside it.
        elif char == '"':
            if blank_strings:
                out.append('""')
            else:
                out.append('"')

            index += 1
            while index < length:
                if text[index] == "\\" and index + 1 < length:
                    if not blank_strings:
                        out.append(text[index:index + 2])
                    index += 2
                    continue
                if text[index] == '"':
                    if not blank_strings:
                        out.append('"')
                    index += 1
                    break
                if text[index] == "\n":
                    out.append("\n")
                elif not blank_strings:
                    out.append(text[index])
                index += 1

        # Character literal, e.g. '\n' or 'A'. Also matches the apostrophe in a
        # C++14 digit separator, which is harmless: neither carries a brace.
        elif char == "'":
            if blank_strings:
                out.append("''")
            else:
                out.append("'")

            index += 1
            while index < length:
                if text[index] == "\\" and index + 1 < length:
                    if not blank_strings:
                        out.append(text[index:index + 2])
                    index += 2
                    continue
                if text[index] == "'":
                    if not blank_strings:
                        out.append("'")
                    index += 1
                    break
                if text[index] == "\n":
                    out.append("\n")
                elif not blank_strings:
                    out.append(text[index])
                index += 1

        else:
            out.append(char)
            index += 1

    return "".join(out)


def class_bodies(text):
    """Yields (class_name, body, is_interface) for every class or struct.

    is_interface is inferred from the name: an `I` followed by a capital is how
    this project names its interfaces (IOneBrahamBreachableInterface), and the
    UHT checker treats those differently because an interface is allowed to
    declare a method with no definition where a class is not.
    """
    # The prefix set is Unreal's conventions, not a guess: A actors, U objects
    # and components, I interfaces, E enums, F structs. Leaving F out was a real
    # blind spot - every USTRUCT in this project was invisible to both the UHT
    # contract check and the call-arity check, and a struct is exactly where a
    # replicated property hides.
    # `[^;{()*&=]` rather than `[^;{]`, and the difference is a real class of
    # bug: `void SetOwningWeaponComponent(class UOneBrahamWeaponComponent* In) {...}`
    # is an elaborated type specifier in a parameter list, and with the loose
    # version the parser read it as a second declaration of
    # UOneBrahamWeaponComponent - which every downstream check then believed.
    # A class head has no parenthesis, asterisk, ampersand or equals sign in it;
    # base lists are made of names, commas and colons, so they still match.
    for match in re.finditer(
            r"\b(?:class|struct)\s+(?:ONEBRAHAM_API\s+)?([AUIEF][A-Za-z0-9_]+)[^;{()*&=]*\{",
            text):
        open_index = text.index("{", match.end() - 1)

        depth = 0
        index = open_index

        while index < len(text):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    break
            index += 1

        if depth != 0:
            continue

        name = match.group(1)
        is_interface = len(name) > 1 and name[0] == "I" and name[1].isupper()

        yield name, text[open_index + 1:index], is_interface


def split_class_statements(body):
    """Brace-depth-zero statements of a class body, with their pending macros.

    Returns a list of (macros, statement_text), where macros is the list of
    (name, text) macro invocations immediately above the statement - the
    UPROPERTY(...) or UFUNCTION(...) that gives it its specifiers.

    Three things about C++ that this has to know:

      * A macro invocation is not semicolon-terminated, so `UPROPERTY(...) FName
        ZoneId;` arrives as a single statement and the specifier is never seen.
        The macro is flushed at its own closing paren: an all-caps call at the
        start of a statement is one, and no declaration looks like that, because
        a declaration has a type in front of its parentheses.

      * `protected:` is commonly written on the line above the macro and lands
        in the same statement, so it is stripped first. Missing this is how an
        earlier version of the UHT check reported eleven correct callbacks as
        broken.
    """
    statements = []
    current = []
    depth_paren = 0
    depth_brace = 0

    for char in body:
        if char in "([":
            depth_paren += 1
        elif char in ")]":
            depth_paren -= 1
        elif char == "{":
            depth_brace += 1
        elif char == "}":
            depth_brace -= 1

        if char == ";" and depth_paren == 0 and depth_brace == 0:
            statements.append("".join(current) + ";")
            current = []
        elif char == "}" and depth_paren == 0 and depth_brace == 0:
            statements.append("".join(current) + "}")
            current = []
        else:
            current.append(char)

        if char == ")" and depth_paren == 0 and depth_brace == 0:
            so_far = ACCESS_SPECIFIER.sub("", "".join(current).strip())

            if MACRO_START.match(so_far):
                statements.append(so_far)
                current = []

    if current:
        statements.append("".join(current))

    pending = []
    out = []

    for statement in statements:
        text = " ".join(statement.split()).strip()
        macro = MACRO_START.match(text)

        if macro is not None and text.endswith(")"):
            pending.append((macro.group(1), text))
            continue

        out.append((list(pending), text))
        pending = []

    return out


# ---------------------------------------------------------------------------
#  Logging
# ---------------------------------------------------------------------------
_PREFIX = "[OB.BUILD]"


def log(message):
    """Progress line. Always visible: a build script that says nothing is a
    build script nobody trusts."""
    text = u"{0} {1}".format(_PREFIX, message)
    if IN_EDITOR:
        unreal.log(text)
    else:
        print(text)


def warn(message):
    text = u"{0} WARNING: {1}".format(_PREFIX, message)
    if IN_EDITOR:
        unreal.log_warning(text)
    else:
        print(text)


def error(message):
    text = u"{0} ERROR: {1}".format(_PREFIX, message)
    if IN_EDITOR:
        unreal.log_error(text)
    else:
        print(text, file=sys.stderr)


def step(index, total, message):
    log(u"[{0}/{1}] {2}".format(index, total, message))


class BuildReport(object):
    """Collects what a build did, so run_all.py can print one honest summary."""

    def __init__(self, name):
        self.name = name
        self.created = []
        self.updated = []
        self.skipped = []
        self.errors = []

    def created_asset(self, path):
        self.created.append(path)

    def updated_asset(self, path):
        self.updated.append(path)

    def skipped_asset(self, path, why):
        self.skipped.append((path, why))

    def failed(self, what, why):
        self.errors.append((what, why))
        error(u"{0}: {1}".format(what, why))

    def summary(self):
        return (u"{0}: {1} created, {2} updated, {3} skipped, {4} errors"
                .format(self.name, len(self.created), len(self.updated),
                        len(self.skipped), len(self.errors)))

    def ok(self):
        return len(self.errors) == 0


def guard(report, label, fn, *args, **kwargs):
    """Runs fn, converting any exception into a report entry.

    A build script that dies on the first unexpected asset takes twenty minutes
    of a producer's afternoon with it. Failing one item and carrying on means the
    report at the end lists everything that is wrong at once.
    """
    try:
        return fn(*args, **kwargs)
    except Exception as exc:  # noqa: BLE001 - deliberately broad at the boundary
        report.failed(label, u"{0}\n{1}".format(exc, traceback.format_exc()))
        return None


def make_gameplay_tag(tag_string):
    """'OB.Zone.Lobby' -> the registered unreal.GameplayTag, or None.

    The engine's Python API has no tag-from-string function, so the module
    exposes one (UOneBrahamGameplayStatics::RequestTag). None means the tag is
    not registered: the C++ module did not load, or the name is not in
    DefaultGame.ini's tag list. The caller reports that; it is never silently
    an empty tag.
    """
    library = getattr(unreal, "OneBrahamGameplayStatics", None) if IN_EDITOR else None
    if library is None:
        return None
    tag = library.request_tag(unreal.Name(tag_string))
    try:
        valid = unreal.BlueprintGameplayTagLibrary.is_gameplay_tag_valid(tag)
    except Exception:  # noqa: BLE001
        valid = str(tag_string) in tag.export_text()
    return tag if valid else None


def data_asset_factory(asset_class):
    """A DataAssetFactory that makes exactly this class.

    The factory falls back to the class create_asset is given, but naming it
    on the factory as well is what the engine's own UI does, and leaves no
    path where an abstract base class is instantiated instead.
    """
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("DataAssetClass", asset_class)
    return factory
