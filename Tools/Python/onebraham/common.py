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
MAP_TUTORIAL = "L_Tutorial_SignIn"
MAP_LOADING = "L_Loading"
MAP_BENCHMARK = "L_Benchmark_Skydeck"

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
# (name, first_floor, last_floor, wing_origin_xy, wing_size_xy).
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

# Core / circulation dimensions.
LIFT_CORE_SIZE_CM = 1200.0          # 6 x 6 m lift core
LIFT_CAR_SIZE_CM = 220.0
CORRIDOR_WIDTH_CM = 260.0
DOOR_WIDTH_CM = 110.0
DOOR_HEIGHT_CM = 215.0
STAIRWELL_SIZE_CM = 900.0
STAIRWELL_FLIGHTS_PER_FLOOR = 2
SERVER_ROW_SPACING_CM = 180.0
DESK_GRID_CM = 180.0
CEILING_HEIGHT_CM = 400.0
WINDOW_SPACING_CM = 300.0

# Spawn safety: no spawn point may be within this distance of an enemy's.
SPAWN_MIN_ENEMY_DISTANCE_CM = 2000.0
SPAWN_MIN_SAME_TEAM_DISTANCE_CM = 600.0

# Gameplay anchors.
EXTRACTION_PAD_LOCATION = (0.0, 0.0, 15.0 * FLOOR_HEIGHT_CM)
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
