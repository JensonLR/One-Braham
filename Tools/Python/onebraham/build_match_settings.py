# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_match_settings
==============================

Authors the three match settings assets the game instance loads by name:
one UOneBrahamMatchSettingsAsset per mode, at the path the C++ builds from the
mode (`UOneBrahamGameInstance::GetMatchSettings`).

WHY THIS EXISTS
    The game instance falls back to a transient default when the asset is
    missing, so that a code-only checkout still plays. That fallback was the
    only match settings the game had ever run on: the first packaged build
    logged "Match settings asset '/Game/OneBraham/Data/DA_MatchSettings_Ascent'
    missing - generated a runtime default" on every boot, and the mode-specific
    rules in Docs/06 that the class defaults do not carry (FIRE DRILL without a
    downed state, a ten-minute FIRE DRILL, NIGHT SHIFT as four players) were
    not in the game at all.

WHERE THE NUMBERS COME FROM
    Every value in MODES below is quoted from one of three places, and the
    comment beside it says which:
        Docs/06        the design (section 1 per mode, 2 access, 3 lockdown,
                       4 extraction, "Spawn safety")
        DefaultGame.ini  [/Script/OneBraham.OneBrahamMatchSettings]
        C++            the UPROPERTY default or EnsureDefaults()
    A field the docs are silent about is not listed and keeps its C++ default.
    Where Docs/06's section 6 table disagrees with the code it is not used:
    its field names (MaxPlayers, bAllowRespawn, PurgeDamagePerTick...) are not
    properties of the class, and its numbers (bleed-out 45 s, revive 4.0 s,
    FIRE DRILL respawn 12 s) contradict the constants the automation tests hold
    the class to (OneBraham.Core.AscentRules / FireDrillRules).

WHAT IS LEFT TO THE CODE
    WeaponSpawnTable, FloorLootProfiles and ControlPoints stay empty here.
    The GameMode calls EnsureDefaults() on whatever asset it loads, and that
    fills each of them from the one table that check_weapon_ids.py holds
    against build_weapons.py and Docs/05. Writing them here as well would be a
    fifth copy of the armoury with nothing to keep it honest.

RE-RUNNING
    Idempotent. An existing asset is loaded and every field in its row is
    written again, so this table wins over a hand edit of the same field;
    fields the table does not name are left as they were. Retune a mode here,
    not in the details panel, or the next content build undoes it.

WHAT IT CREATES
    /Game/OneBraham/Data/DA_MatchSettings_Ascent        UOneBrahamMatchSettingsAsset
    /Game/OneBraham/Data/DA_MatchSettings_FireDrill     UOneBrahamMatchSettingsAsset
    /Game/OneBraham/Data/DA_MatchSettings_NightShift    UOneBrahamMatchSettingsAsset

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/build_match_settings.py"
"""

from __future__ import print_function

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

#: The map every mode travels to. L_FireDrill_Arena is built and cooked, but it
#: has no player starts and no control point volumes yet, and the FIRE DRILL
#: points EnsureDefaults() writes are floors 1, 9 and 15 of the tower.
TOWER_MAP = C.MAP_PERSISTENT

# ---------------------------------------------------------------------------
#  The modes.
#
#  "asset" is the <Mode> in /Game/OneBraham/Data/DA_MatchSettings_<Mode>, spelt
#  exactly as the switch in UOneBrahamGameInstance::GetMatchSettings spells it.
#  "mode" is the EOneBrahamMatchMode entry, by its C++ name.
#  "values" is property -> value; texts are plain strings and become FText.
# ---------------------------------------------------------------------------
MODES = [
    dict(
        asset="Ascent", mode="Ascent",
        display_name="ASCENT",                      # enum DisplayName
        description="Twelve players in three squads of four. Climb from the plaza "
                    "to the Skydeck, call the winch, hold the pad for thirty "
                    "seconds and get out. No respawns, no AI, one extraction.",
        briefing="Reach the Skydeck. Hold the pad. Extract. The building closes "
                 "behind you, and only one squad leaves.",
        values=dict(
            team_count=3,                           # Docs/06 s1; ini AscentSquadCount
            players_per_team=4,                     # Docs/06 s1; ini AscentSquadSize
            bFriendlyFire=False,                    # ini AscentFriendlyFire
            warmup_seconds=30,                      # ini WarmupSeconds
            post_match_seconds=45,                  # ini PostMatchSeconds
            bEnableDownedState=True,                # Docs/06 s1; ini AscentDownedStateEnabled
            bleed_out_seconds=60.0,                 # Docs/03; ini AscentBleedOutSeconds
            revive_seconds=4.5,                     # Docs/03; ini AscentReviveSeconds
            bRespawnEnabled=False,                  # Docs/06 s1; ini AscentRespawnEnabled
            spawn_grace_invulnerability_seconds=8.0,  # Docs/06 spawn safety; ini AscentSpawnGraceSeconds
            terminal_override_seconds=12.0,         # Docs/06 s2; ini AscentAccessUnlockCostSeconds
            terminal_override_with_keycard_seconds=3.0,  # Docs/06 s2
            bLockdownEnabled=True,                  # Docs/06 s3
            lockdown_stage_seconds=90.0,            # Docs/06 s3; ini AscentLockdownStageSeconds
            lockdown_warning_seconds=15.0,          # Docs/06 s3
            purge_floor_ceiling=14,                 # Docs/06 s3
            bExtractionEnabled=True,                # Docs/06 s1
            extraction_broadcast_seconds=10.0,      # Docs/06 s4
            helicopter_arrival_seconds=45.0,        # Docs/06 s4; ini AscentHelicopterArrivalSeconds
            extraction_hold_seconds=30.0,           # Docs/06 s4; ini AscentExtractionHoldSeconds
            extraction_zone_radius_cm=900.0,        # Docs/06 s4
            helicopter_loiter_seconds=75.0,         # Docs/06 s4
            bScoreByTimeLimit=True,
            time_limit_seconds=2100,                # Docs/06 s1 "hard end at 35"
        ),
    ),
    dict(
        asset="FireDrill", mode="FireDrill",
        display_name="FIRE DRILL",                  # enum DisplayName
        description="Internal Staff against External Contractors, six a side. "
                    "Three control points - Lobby, Server Floor, Canteen-Skydeck - "
                    "fast respawns and no downed state. Ten minutes, or first to "
                    "200 points.",
        briefing="Take the points and hold them. First team to 200, or the most "
                 "points when the drill ends.",
        values=dict(
            team_count=2,                           # Docs/06 s1; EnsureDefaults
            players_per_team=6,                     # Docs/06 s1 (6v6); ini FireDrillTeamSize
            bFriendlyFire=False,                    # C++
            warmup_seconds=30,                      # ini WarmupSeconds
            post_match_seconds=45,                  # ini PostMatchSeconds
            bEnableDownedState=False,               # Docs/06 s1, Docs/03
            bRespawnEnabled=True,                   # Docs/06 s1; EnsureDefaults
            respawn_delay_seconds=5.0,              # ini FireDrillRespawnDelaySeconds; C++
            spawn_grace_invulnerability_seconds=3.0,  # Docs/06 s1 + spawn safety; EnsureDefaults
            bLockdownEnabled=False,                 # Docs/06 s6
            bExtractionEnabled=False,               # Docs/06 s1; EnsureDefaults
            score_to_win=200,                       # Docs/06 s1; ini FireDrillScoreToWin
            bScoreByTimeLimit=True,
            time_limit_seconds=600,                 # Docs/06 s1 "10 minutes"
        ),
    ),
    dict(
        asset="NightShift", mode="NightShift",
        display_name="NIGHT SHIFT (Beta)",          # enum DisplayName
        description="Four-player co-op against the building itself: lockdowns, "
                    "purge waves and shutters that close on you. Post-launch - "
                    "not part of the launch scope.",
        briefing="Keep the floor alive long enough to walk out of it.",
        values=dict(
            team_count=1,                           # Docs/06 s1 "4-player co-op"
            players_per_team=4,                     # Docs/06 s1; ini NightShiftPlayerCount
            warmup_seconds=30,                      # ini WarmupSeconds
            post_match_seconds=45,                  # ini PostMatchSeconds
            night_shift_wave_count=8,               # ini NightShiftEnemyWaves; C++
        ),
    ),
]

#: EOneBrahamMatchMode's C++ names against the names Python reflects them as.
MODE_ENUM_NAMES = {
    "Ascent": ("ASCENT",),
    "FireDrill": ("FIRE_DRILL", "FIREDRILL"),
    "NightShift": ("NIGHT_SHIFT", "NIGHTSHIFT"),
}


def asset_path(mode_row):
    return u"{0}/DA_MatchSettings_{1}".format(C.DATA, mode_row["asset"])


def _ensure_dirs():
    if not unreal.EditorAssetLibrary.does_directory_exist(C.DATA):
        unreal.EditorAssetLibrary.make_directory(C.DATA)


def _set(obj, prop, value, report, context):
    """Sets an editor property, reporting rather than throwing when it is absent.

    A name the class does not have means the header changed and this table did
    not, so it is a failure with the property name, never swallowed.
    """
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as exc:  # noqa: BLE001
        report.failed(context, u"{0}: {1}".format(prop, exc))
        return False


def _mode_enum(mode_name):
    enum = getattr(unreal, "OneBrahamMatchMode", None)
    if enum is None:
        return None
    for candidate in MODE_ENUM_NAMES.get(mode_name, ()):
        value = getattr(enum, candidate, None)
        if value is not None:
            return value
    return None


def _same(expected, actual):
    if isinstance(expected, float):
        try:
            return abs(float(actual) - expected) < 0.001
        except (TypeError, ValueError):
            return False
    return expected == actual


def _apply(report, asset, row):
    context = u"DA_MatchSettings_{0}".format(row["asset"])

    mode = _mode_enum(row["mode"])
    if mode is None:
        report.failed(context, u"EOneBrahamMatchMode has no '{0}' in this build".format(row["mode"]))
        return False

    _set(asset, "mode", mode, report, context)
    _set(asset, "mode_display_name", unreal.Text(row["display_name"]), report, context)
    _set(asset, "mode_description", unreal.Text(row["description"]), report, context)
    _set(asset, "mode_briefing", unreal.Text(row["briefing"]), report, context)
    _set(asset, "map_name", unreal.Name(TOWER_MAP), report, context)

    for prop, value in sorted(row["values"].items()):
        _set(asset, prop, value, report, context)

    # Read every field back: a clamp in the header (TeamCount 1-4, PlayersPerTeam
    # 1-8) or a mistyped value is caught here, not by the first lobby.
    for prop, value in sorted(row["values"].items()):
        try:
            actual = asset.get_editor_property(prop)
        except Exception as exc:  # noqa: BLE001
            report.failed(context, u"{0}: unreadable after set: {1}".format(prop, exc))
            continue
        if not _same(value, actual):
            report.failed(context, u"{0}: wrote {1}, reads back {2}".format(prop, value, actual))

    return True


def build_match_settings(report):
    """One UOneBrahamMatchSettingsAsset per mode, at the conventional path."""
    settings_class = getattr(unreal, "OneBrahamMatchSettingsAsset", None)
    if settings_class is None:
        for row in MODES:
            report.skipped_asset(asset_path(row),
                                 "UOneBrahamMatchSettingsAsset is not reflected yet - compile "
                                 "the OneBraham module, then re-run this script")
        return {}

    built = {}

    for row in MODES:
        path = asset_path(row)
        asset_name = u"DA_MatchSettings_{0}".format(row["asset"])

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            asset = unreal.EditorAssetLibrary.load_asset(path)
            created = False
        else:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property("data_asset_class", settings_class)
            asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                asset_name, C.DATA, settings_class, factory)
            created = True

        if asset is None:
            report.failed(asset_name, "could not create or load the match settings asset")
            continue

        if not _apply(report, asset, row):
            continue

        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        if created:
            report.created_asset(path)
        else:
            report.updated_asset(path)
        built[row["asset"]] = asset

        read = asset.get_editor_property
        C.log(u"  {0}: {1} x {2}, respawn {3}, downed {4}, limit {5} s".format(
            asset_name, read("team_count"), read("players_per_team"),
            read("bRespawnEnabled"), read("bEnableDownedState"), read("time_limit_seconds")))

    C.log(u"  match settings: {0}/{1}".format(len(built), len(MODES)))
    return built


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("build_match_settings must run inside the Unreal Editor.")

    report = C.BuildReport("build_match_settings")
    C.log(u"=== Building One Braham match settings ===")

    C.guard(report, "directories", _ensure_dirs)
    C.guard(report, "match settings", build_match_settings, report)

    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
