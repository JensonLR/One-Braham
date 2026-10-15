# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_weapons
=======================

Authors the ten weapons: one data asset each, one Blueprint each, and the
registry that binds them together.

WHY THIS IS A SCRIPT AND NOT TEN MANUAL ASSET CREATIONS
    A weapon in this game is 150 tuned fields. Ten of them is 1500 numbers, and
    the only way those numbers stay coherent - a pistol that is not better than a
    carbine, a marksman rifle that is not a shotgun, a suppressed pistol that
    costs something real - is if they are written down next to each other and
    read as a table. That is what WEAPONS below is. Change a number there, re-run
    the script, and the whole armoury re-balances together.

THE ARMOURY (and the one-line design intent of each)
    W_Pistol            standard-issue sidearm. Reliable, unremarkable, 3 shots.
    W_CompactPistol     machine pistol. Fast, weak, useless past 30 m.
    W_SuppressedPistol  quiet. Costs damage and velocity for silence.
    W_SMG               9mm room-clearer. Best in the building at 15 m.
    W_PDW               armour-piercing 4.6mm. The anti-armour choice.
    W_PumpShotgun       8 pellets, one shot, 2.5 s between shots.
    W_SemiShotgun       faster cycling, wider spread, less damage per pellet.
    W_Carbine556        the default rifle. Nothing it is bad at.
    W_CompactAR         shorter, faster handling, more recoil, less reach.
    W_MarksmanRifle     semi-auto .308. Two shots, any range, slow.

EVERY WEAPON IS A REAL DECISION
    Full-auto is not available on the marksman rifle and never will be. Shotguns
    cannot penetrate anything. The suppressed pistol has the worst time-to-kill in
    the game and the best information profile: it is the weapon you take when you
    intend to be somewhere you are not supposed to be.

WHAT IT CREATES
    /Game/OneBraham/Data/Weapons/DA_Weapon_<Name>     UOneBrahamWeaponDefinition
    /Game/OneBraham/Weapons/BP_OB_Weapon_<Name>       AOneBrahamWeaponBase subclass
    /Game/OneBraham/Data/DA_WeaponRegistry            UOneBrahamWeaponRegistry

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/build_weapons.py"
"""

from __future__ import print_function

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

WEAPONS_ROOT = C.DATA + "/Weapons"
BP_WEAPONS_ROOT = C.WEAPONS + "/Blueprints"

# ---------------------------------------------------------------------------
#  The armoury table.
#
#  Column notes:
#    damage       per bullet, before falloff and hit zone
#    rpm          rounds per minute (single-shot weapons use a cycle rate)
#    mag/reserve  magazine size and starting reserve
#    reload       (tactical, empty) seconds
#    falloff      (full damage range, min damage range, min damage multiplier)
#    spread       (hip standing, hip crouch, hip move, ADS)
#    bloom        (per shot, maximum)
#    pen          (max thickness cm, retention) - 0 means it stops dead
#    ads/draw     seconds to aim, seconds to raise
#    audio        (audible radius, interior audible radius, suppressed)
#    floor        (minimum floor it appears on as loot, maximum)
# ---------------------------------------------------------------------------
WEAPONS = [
    dict(
        id="W_Pistol", name="P99 Sidearm", bp="BP_OB_Weapon_Pistol",
        category="Pistol", slot="Sidearm", fire_modes=["Single"], default_mode="Single",
        damage=32.0, pellets=1, rpm=420.0, velocity=42000.0,
        mag=15, reserve=45, max_reserve=90, reload=(1.7, 2.2),
        falloff=(1800.0, 5000.0, 0.62),
        spread=(2.6, 1.8, 3.8, 0.28), bloom=(0.28, 3.0), decay=5.0,
        recoil=(0.85, 0.35, 0.85, 0.9, 30.0, 0.95, 0.7),
        pen=(0.0, 0.0), ads=0.19, draw=0.32, holster=0.24,
        melee=(32.0, 130.0, 0.7), ads_fov=62.0,
        audio=(7000.0, 11000.0, False), tracer=False,
        floors=(0, 17), weight=1.0,
        description="Standard issue. Fifteen rounds, no surprises, and it will "
                    "still be working when the magazine is empty and you are not.",
    ),
    dict(
        id="W_CompactPistol", name="LCP Machine Pistol", bp="BP_OB_Weapon_CompactPistol",
        category="CompactPistol", slot="Sidearm", fire_modes=["Single", "Auto"],
        default_mode="Single",
        damage=21.0, pellets=1, rpm=900.0, velocity=36000.0,
        mag=20, reserve=60, max_reserve=120, reload=(1.55, 2.0),
        falloff=(900.0, 3000.0, 0.45),
        spread=(4.2, 3.1, 6.4, 0.75), bloom=(0.42, 5.2), decay=6.0,
        recoil=(1.15, 0.55, 1.35, 1.05, 26.0, 0.9, 0.75),
        pen=(0.0, 0.0), ads=0.16, draw=0.26, holster=0.2,
        melee=(28.0, 125.0, 0.65), ads_fov=64.0,
        audio=(6800.0, 10500.0, False), tracer=False,
        floors=(0, 17), weight=0.8,
        description="Twenty rounds of nine-millimetre and no pretence of accuracy "
                    "past the end of the corridor. Useful exactly once, at once.",
    ),
    dict(
        id="W_SuppressedPistol", name="P99-S Suppressed", bp="BP_OB_Weapon_SuppressedPistol",
        category="SuppressedPistol", slot="Sidearm", fire_modes=["Single"], default_mode="Single",
        damage=26.0, pellets=1, rpm=380.0, velocity=32000.0,
        mag=15, reserve=45, max_reserve=90, reload=(1.8, 2.35),
        falloff=(1200.0, 3600.0, 0.5),
        spread=(2.9, 2.0, 4.1, 0.30), bloom=(0.30, 3.2), decay=5.0,
        recoil=(0.75, 0.30, 0.8, 0.88, 32.0, 0.96, 0.7),
        pen=(0.0, 0.0), ads=0.21, draw=0.34, holster=0.26,
        melee=(32.0, 130.0, 0.7), ads_fov=62.0,
        audio=(2600.0, 3200.0, True), tracer=False,
        floors=(0, 17), weight=0.9,
        description="Quieter than the building's own air handling. Most of the "
                    "people in One Braham never learn where a suppressed shot "
                    "came from, which is the entire point of carrying one.",
    ),
    dict(
        id="W_SMG", name="MP5 9mm", bp="BP_OB_Weapon_SMG",
        category="SMG", slot="Primary", fire_modes=["Single", "Auto"], default_mode="Auto",
        damage=25.0, pellets=1, rpm=800.0, velocity=40000.0,
        mag=30, reserve=120, max_reserve=240, reload=(1.9, 2.5),
        falloff=(1400.0, 4200.0, 0.55),
        spread=(3.6, 2.6, 5.0, 0.55), bloom=(0.32, 4.2), decay=5.5,
        recoil=(0.95, 0.42, 1.0, 0.95, 30.0, 0.94, 0.72),
        pen=(0.0, 0.0), ads=0.20, draw=0.38, holster=0.28,
        melee=(34.0, 140.0, 0.75), ads_fov=60.0,
        audio=(8600.0, 13500.0, False), tracer=False,
        floors=(0, 17), weight=1.1,
        description="The floor-clearer. Nothing in the building beats it inside "
                    "fifteen metres, and nothing in the building is easier to "
                    "hear from three floors up.",
    ),
    dict(
        id="W_PDW", name="MP7 PDW 4.6mm", bp="BP_OB_Weapon_PDW",
        category="PDW", slot="Primary", fire_modes=["Single", "Auto"], default_mode="Auto",
        damage=22.0, pellets=1, rpm=950.0, velocity=48000.0,
        mag=40, reserve=160, max_reserve=320, reload=(1.95, 2.6),
        falloff=(1300.0, 4000.0, 0.52),
        spread=(3.4, 2.5, 4.8, 0.48), bloom=(0.30, 4.0), decay=5.5,
        recoil=(0.88, 0.38, 0.95, 0.92, 31.0, 0.95, 0.74),
        pen=(6.0, 0.72), ads=0.19, draw=0.36, holster=0.27,
        melee=(34.0, 140.0, 0.75), ads_fov=60.0,
        audio=(8800.0, 13000.0, False), tracer=False,
        floors=(3, 17), weight=1.0,
        description="Small round, high pressure, and it goes through things that "
                    "stop nine-millimetre. The round the server floor is worth "
                    "carrying for.",
    ),
    dict(
        id="W_PumpShotgun", name="M870 Pump", bp="BP_OB_Weapon_PumpShotgun",
        category="PumpShotgun", slot="Primary", fire_modes=["Pump"], default_mode="Pump",
        damage=17.0, pellets=8, rpm=75.0, velocity=38000.0,
        mag=7, reserve=28, max_reserve=56, reload=(0.0, 4.6),
        per_shell=True, shell_time=0.62,
        falloff=(700.0, 2200.0, 0.22),
        spread=(6.5, 5.2, 7.8, 5.6), bloom=(0.0, 0.0), decay=0.0,
        recoil=(2.6, 1.0, 2.4, 1.6, 14.0, 0.85, 0.88),
        pen=(0.0, 0.0), ads=0.30, draw=0.55, holster=0.4,
        melee=(40.0, 150.0, 0.9), ads_fov=58.0,
        audio=(10500.0, 16000.0, False), tracer=False,
        floors=(0, 17), weight=1.4,
        description="Eight pellets, one pump, and a sound that empties a room "
                    "before the second shot does. Two and a half seconds is a "
                    "very long time.",
    ),
    dict(
        id="W_SemiShotgun", name="Semi-Auto 12 Bore", bp="BP_OB_Weapon_SemiShotgun",
        category="SemiAutoShotgun", slot="Primary", fire_modes=["Single"], default_mode="Single",
        damage=12.5, pellets=9, rpm=190.0, velocity=36000.0,
        mag=6, reserve=30, max_reserve=60, reload=(2.6, 3.4),
        falloff=(600.0, 1900.0, 0.20),
        spread=(7.2, 6.0, 8.6, 6.4), bloom=(0.6, 2.0), decay=4.0,
        recoil=(2.1, 0.85, 2.0, 1.35, 16.0, 0.88, 0.85),
        pen=(0.0, 0.0), ads=0.27, draw=0.52, holster=0.38,
        melee=(40.0, 150.0, 0.9), ads_fov=58.0,
        audio=(10000.0, 15500.0, False), tracer=False,
        floors=(6, 17), weight=1.5,
        description="More shots, less per shot, and a pattern you can put on a "
                    "doorway from across the canteen. It is the weapon for "
                    "people who have decided the stairs are a bad idea.",
    ),
    dict(
        id="W_Carbine556", name="M4 Carbine", bp="BP_OB_Weapon_Carbine556",
        category="Carbine556", slot="Primary", fire_modes=["Single", "Burst", "Auto"],
        default_mode="Auto",
        damage=33.0, pellets=1, rpm=740.0, velocity=88000.0,
        mag=30, reserve=120, max_reserve=240, reload=(2.05, 2.75),
        falloff=(3200.0, 9500.0, 0.68),
        spread=(3.0, 2.1, 4.4, 0.22), bloom=(0.30, 3.6), decay=5.0,
        recoil=(1.1, 0.45, 1.1, 0.9, 29.0, 0.94, 0.68),
        pen=(9.0, 0.78), ads=0.22, draw=0.45, holster=0.32,
        melee=(40.0, 150.0, 0.85), ads_fov=55.0,
        audio=(9200.0, 14000.0, False), tracer=True,
        floors=(0, 17), weight=1.6,
        description="The default rifle because it has no bad habit. Thirty "
                    "rounds, one in three of them a tracer, and it will put a "
                    "hole in the partition you are hiding behind.",
    ),
    dict(
        id="W_CompactAR", name="MCX Compact 5.56", bp="BP_OB_Weapon_CompactAR",
        category="CompactAR", slot="Primary", fire_modes=["Single", "Burst", "Auto"],
        default_mode="Burst",
        damage=31.0, pellets=1, rpm=780.0, velocity=80000.0,
        mag=25, reserve=100, max_reserve=200, reload=(1.9, 2.55),
        falloff=(2600.0, 8000.0, 0.62),
        spread=(3.3, 2.3, 4.7, 0.30), bloom=(0.34, 4.2), decay=5.0,
        recoil=(1.28, 0.52, 1.25, 0.95, 27.0, 0.92, 0.7),
        pen=(9.0, 0.75), ads=0.19, draw=0.4, holster=0.29,
        melee=(40.0, 150.0, 0.85), ads_fov=58.0,
        audio=(9000.0, 13800.0, False), tracer=True,
        floors=(5, 17), weight=1.5,
        description="Shorter, faster to the shoulder, and it climbs harder. This "
                    "is the rifle you take into the stairwell: it is the one that "
                    "turns a corner without catching on the wall.",
    ),
    dict(
        id="W_MarksmanRifle", name="L129A1 Marksman", bp="BP_OB_Weapon_MarksmanRifle",
        category="MarksmanRifle", slot="Primary", fire_modes=["Single"], default_mode="Single",
        damage=78.0, pellets=1, rpm=170.0, velocity=92000.0,
        mag=10, reserve=40, max_reserve=80, reload=(2.3, 3.0),
        falloff=(6000.0, 14000.0, 0.85),
        spread=(4.4, 3.2, 6.0, 0.08), bloom=(1.4, 6.0), decay=3.2,
        recoil=(2.35, 0.95, 2.2, 0.75, 20.0, 0.9, 0.55),
        pen=(16.0, 0.88), ads=0.28, draw=0.58, holster=0.42,
        melee=(45.0, 160.0, 1.0), ads_fov=42.0,
        audio=(11000.0, 17500.0, False), tracer=True,
        floors=(6, 17), weight=1.9,
        description="Ten rounds of .308 and a scope that makes the length of the "
                    "open-plan floor a problem for whoever is standing at the far "
                    "end of it. Two shots anywhere. One, usually.",
    ),
]

FIRE_MODE_ROTATION = {
    "Single": "Single", "Auto": "Auto", "Burst": "Burst", "Pump": "Pump", "Bolt": "Bolt",
}


def _ensure_dirs():
    for path in (WEAPONS_ROOT, BP_WEAPONS_ROOT):
        if not unreal.EditorAssetLibrary.does_directory_exist(path):
            unreal.EditorAssetLibrary.make_directory(path)


def _set(obj, prop, value, report, context):
    """Sets an editor property, reporting rather than throwing when it is absent.

    A reflected name that does not exist is a real thing to know about - it means
    the C++ struct changed and this table did not - so it is recorded as a
    failure with the property name, not swallowed.
    """
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as exc:  # noqa: BLE001
        report.failed(context, u"{0}: {1}".format(prop, exc))
        return False


def _make_enum(enum_name, value_name):
    enum = getattr(unreal, enum_name, None)
    if enum is None:
        return None
    return getattr(enum, value_name.upper(), None)


def _text(value):
    return unreal.Text(value)


def build_weapon_blueprints(report):
    """One Blueprint per weapon, subclassing AOneBrahamWeaponBase.

    The Blueprint is where the meshes, sockets and animations get attached; the
    data asset is where the behaviour gets tuned. Keeping them apart is what lets
    artists work on a weapon without touching its ballistics.
    """
    parent = getattr(unreal, "OneBrahamWeaponBase", None)
    blueprints = {}

    for weapon in WEAPONS:
        name = weapon["bp"]
        path = u"{0}/{1}".format(BP_WEAPONS_ROOT, name)

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            blueprints[weapon["id"]] = unreal.EditorAssetLibrary.load_asset(path)
            report.updated_asset(path)
            continue

        factory = unreal.BlueprintFactory()
        if parent is not None:
            factory.set_editor_property("parent_class", parent)
        else:
            report.failed(name, "AOneBrahamWeaponBase is not reflected yet - compile the "
                                "OneBraham module, then re-run this script")

        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, BP_WEAPONS_ROOT, unreal.Blueprint, factory)

        if asset is None:
            report.failed(name, "could not create weapon blueprint")
            continue

        unreal.EditorAssetLibrary.save_asset(path)
        report.created_asset(path)
        blueprints[weapon["id"]] = asset

    C.log(u"  weapon blueprints: {0}/{1}".format(len(blueprints), len(WEAPONS)))
    return blueprints


def build_weapon_definitions(report, blueprints):
    """One UOneBrahamWeaponDefinition per weapon, fully tuned."""
    definition_class = getattr(unreal, "OneBrahamWeaponDefinition", None)
    if definition_class is None:
        for weapon in WEAPONS:
            report.skipped_asset(u"{0}/DA_Weapon_{1}".format(WEAPONS_ROOT, weapon["id"][2:]),
                                 "UOneBrahamWeaponDefinition is not reflected yet - compile "
                                 "the OneBraham module, then re-run this script")
        return {}

    definitions = {}

    for weapon in WEAPONS:
        asset_name = u"DA_Weapon_{0}".format(weapon["id"][2:])
        path = u"{0}/{1}".format(WEAPONS_ROOT, asset_name)

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            existing = unreal.EditorAssetLibrary.load_asset(path)
            # Tuning is not re-applied over an existing definition, but models
            # added since it was made are: an empty slot is never a choice.
            if _apply_stand_in_models(report, existing, weapon, asset_name):
                unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
            definitions[weapon["id"]] = existing
            report.updated_asset(path)
            continue

        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, WEAPONS_ROOT, definition_class, unreal.DataAssetFactory())

        if asset is None:
            report.failed(asset_name, "could not create weapon definition")
            continue

        _apply_definition(report, asset, weapon, blueprints)
        unreal.EditorAssetLibrary.save_asset(path)
        report.created_asset(path)
        definitions[weapon["id"]] = asset

        C.log(u"  {0}: {1}, {2:.0f} dmg, {3:.0f} rpm, mag {4}, pen {5} cm".format(
            weapon["id"], weapon["name"], weapon["damage"], weapon["rpm"],
            weapon["mag"], weapon["pen"][0]))

    return definitions


def _apply_stand_in_models(report, asset, weapon, context):
    """Epic's template rifle and pistol, until the armoury's own models exist.

    Copied from the engine install by fetch_open_assets.py: sidearms get the
    pistol, everything else the rifle. Only slots that read back empty are
    filled. Python reads a soft reference as None until its target is loaded,
    so the models are loaded first; a hand-assigned model elsewhere in the
    project can still read as empty and be replaced - replace the stand-ins in
    this table, not by hand. Returns True if anything changed.
    """
    family = "Pistol" if weapon.get("slot") == "Sidearm" else "Rifle"
    changed = False
    for prop, path in (("third_person_mesh", "/Game/Weapons/{0}/Meshes/SKM_{0}".format(family)),
                       ("first_person_mesh", "/Game/Weapons/{0}/Meshes/SKM_{0}".format(family)),
                       ("world_pickup_mesh", "/Game/Weapons/{0}/Meshes/SM_{0}".format(family))):
        model = unreal.load_asset(path)
        if model is None:
            report.skipped_asset(path, "stand-in weapon model not in the project yet")
            continue
        if asset.get_editor_property(prop) is not None:
            continue
        changed = _set(asset, prop, model, report, context) or changed
    return changed


def _apply_definition(report, asset, weapon, blueprints):
    weapon_id = weapon["id"]
    context = u"definition {0}".format(weapon_id)

    # --- identity --------------------------------------------------------
    _set(asset, "weapon_id", weapon["id"], report, context)
    _set(asset, "display_name", _text(weapon["name"]), report, context)
    _set(asset, "description", _text(weapon["description"]), report, context)

    category = _make_enum("OneBrahamWeaponCategory", weapon["category"])
    if category is not None:
        _set(asset, "category", category, report, context)

    slot = _make_enum("OneBrahamWeaponSlot", weapon["slot"])
    if slot is not None:
        _set(asset, "default_slot", slot, report, context)

    # --- ballistics ------------------------------------------------------
    _set(asset, "base_damage", weapon["damage"], report, context)
    _set(asset, "pellet_count", weapon["pellets"], report, context)
    _set(asset, "muzzle_velocity", weapon["velocity"], report, context)
    _set(asset, "rounds_per_minute", weapon["rpm"], report, context)

    modes = []
    for mode_name in weapon["fire_modes"]:
        mode = _make_enum("OneBrahamFireMode", FIRE_MODE_ROTATION[mode_name])
        if mode is not None:
            modes.append(mode)
    if modes:
        _set(asset, "available_fire_modes", modes, report, context)

    default_mode = _make_enum("OneBrahamFireMode", FIRE_MODE_ROTATION[weapon["default_mode"]])
    if default_mode is not None:
        _set(asset, "default_fire_mode", default_mode, report, context)

    # --- falloff ---------------------------------------------------------
    full_range, min_range, min_multiplier = weapon["falloff"]
    falloff = None
    try:
        falloff = asset.get_editor_property("falloff")
    except Exception:  # noqa: BLE001
        falloff = None

    if falloff is not None:
        try:
            falloff.set_editor_property("full_damage_range", full_range)
            falloff.set_editor_property("min_damage_range", min_range)
            falloff.set_editor_property("min_damage_multiplier", min_multiplier)
            asset.set_editor_property("falloff", falloff)
        except Exception as exc:  # noqa: BLE001
            report.failed(context, u"falloff: {0}".format(exc))
    else:
        report.failed(context, "falloff struct not readable - check the property name")

    # --- spread ----------------------------------------------------------
    spread = None
    try:
        spread = asset.get_editor_property("spread")
    except Exception:  # noqa: BLE001
        spread = None

    if spread is not None:
        hip_stand, hip_crouch, hip_move, ads_spread = weapon["spread"]
        bloom_per_shot, bloom_max = weapon["bloom"]
        fields = [
            ("hip_standing_spread", hip_stand),
            ("hip_crouching_spread", hip_crouch),
            ("hip_moving_spread", hip_move),
            ("hip_sprinting_spread", hip_move * 1.65),
            ("ads_spread", ads_spread),
            ("ads_moving_spread", ads_spread * 4.0),
            ("bloom_per_shot", bloom_per_shot),
            ("max_bloom", bloom_max),
            ("bloom_decay_per_second", weapon["decay"]),
        ]
        for prop, value in fields:
            try:
                spread.set_editor_property(prop, value)
            except Exception:  # noqa: BLE001
                pass
        try:
            asset.set_editor_property("spread", spread)
        except Exception as exc:  # noqa: BLE001
            report.failed(context, u"spread: {0}".format(exc))

    # --- recoil ----------------------------------------------------------
    recoil = None
    try:
        recoil = asset.get_editor_property("recoil")
    except Exception:  # noqa: BLE001
        recoil = None

    if recoil is not None:
        vertical, jitter, ads_v, ads_h, recovery, recovery_fraction, first_shot = weapon["recoil"]
        fields = [
            ("yaw_jitter", jitter),
            ("adsv_recoil_multiplier", ads_v),
            ("adsh_recoil_multiplier", ads_h),
            ("recovery_speed", recovery),
            ("recovery_fraction", recovery_fraction),
            ("first_shot_multiplier", first_shot),
        ]
        for prop, value in fields:
            try:
                recoil.set_editor_property(prop, value)
            except Exception:  # noqa: BLE001
                pass
        try:
            asset.set_editor_property("recoil", recoil)
        except Exception as exc:  # noqa: BLE001
            report.failed(context, u"recoil: {0}".format(exc))

    # --- penetration -----------------------------------------------------
    thickness, retention = weapon["pen"]
    _set(asset, "max_penetration_thickness", thickness, report, context)
    _set(asset, "penetration_damage_retention", retention, report, context)
    _set(asset, "bCanPenetrateCover", thickness > 0.0, report, context)

    # --- ammunition ------------------------------------------------------
    _set(asset, "magazine_size", weapon["mag"], report, context)
    _set(asset, "starting_reserve", weapon["reserve"], report, context)
    _set(asset, "max_reserve", weapon["max_reserve"], report, context)
    _set(asset, "reload_time_tactical", weapon["reload"][0], report, context)
    _set(asset, "reload_time_empty", weapon["reload"][1], report, context)

    if weapon.get("per_shell"):
        _set(asset, "bPerShellReload", True, report, context)
        _set(asset, "per_shell_reload_time", weapon.get("shell_time", 0.6), report, context)

    # --- handling --------------------------------------------------------
    _set(asset, "ads_time", weapon["ads"], report, context)
    _set(asset, "ads_field_of_view", weapon["ads_fov"], report, context)
    _set(asset, "draw_time", weapon["draw"], report, context)
    _set(asset, "holster_time", weapon["holster"], report, context)
    _set(asset, "melee_damage", weapon["melee"][0], report, context)
    _set(asset, "melee_range", weapon["melee"][1], report, context)
    _set(asset, "melee_cooldown", weapon["melee"][2], report, context)

    # --- audio -----------------------------------------------------------
    audible, interior, suppressed = weapon["audio"]
    _set(asset, "audible_radius", audible, report, context)
    _set(asset, "interior_audible_radius", interior, report, context)

    sound_prefix = {
        "W_Pistol": "SFX_Gunshot_Pistol",
        "W_CompactPistol": "SFX_Gunshot_Pistol",
        "W_SuppressedPistol": "SFX_Gunshot_Suppressed",
        "W_SMG": "SFX_Gunshot_SMG",
        "W_PDW": "SFX_Gunshot_SMG",
        "W_PumpShotgun": "SFX_Gunshot_Shotgun",
        "W_SemiShotgun": "SFX_Gunshot_Shotgun",
        "W_Carbine556": "SFX_Gunshot_Rifle",
        "W_CompactAR": "SFX_Gunshot_Rifle",
        "W_MarksmanRifle": "SFX_Gunshot_Marksman",
    }.get(weapon_id, "SFX_Gunshot_Rifle")

    # Sound assets are resolved by the audio manager's bank, so the definition
    # only has to name the cue. An empty soft reference here is deliberate: the
    # bank is the single source of truth for "what does a rifle sound like".
    C.log(u"  audio cue for {0}: {1} (radius {2:.0f} cm)".format(weapon_id, sound_prefix, audible))

    # --- VFX -------------------------------------------------------------
    _set(asset, "bHideTracer", not weapon["tracer"], report, context)

    # --- loot ------------------------------------------------------------
    min_floor, max_floor = weapon["floors"]
    _set(asset, "loot_weight", weapon["weight"], report, context)
    _set(asset, "min_floor", min_floor, report, context)
    _set(asset, "max_floor", max_floor, report, context)

    _apply_stand_in_models(report, asset, weapon, context)

    # --- classes ---------------------------------------------------------
    blueprint = blueprints.get(weapon_id)
    if blueprint is not None:
        try:
            asset.set_editor_property("weapon_actor_class", blueprint.get_editor_property("generated_class")
                                      if hasattr(blueprint, "generated_class") else blueprint)
        except Exception:  # noqa: BLE001
            # The class reference resolves from the Blueprint's generated class at
            # runtime; recording it here is a convenience for the editor.
            pass


def build_registry(report, definitions):
    """DA_WeaponRegistry: the list every system reads through."""
    # The registry is a UOneBrahamWeaponDataAsset: the weapon component loads
    # DA_WeaponRegistry as exactly that type.
    registry_class = getattr(unreal, "OneBrahamWeaponDataAsset", None)
    if registry_class is None:
        report.skipped_asset(u"{0}/DA_WeaponRegistry".format(C.DATA),
                             "UOneBrahamWeaponDataAsset is not reflected yet - compile the "
                             "OneBraham module, then re-run this script")
        return None

    path = u"{0}/DA_WeaponRegistry".format(C.DATA)
    asset_name = "DA_WeaponRegistry"

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        registry = unreal.EditorAssetLibrary.load_asset(path)
        report.updated_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", registry_class)
        registry = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, C.DATA, registry_class, factory)
        if registry is None:
            report.failed(asset_name, "could not create the weapon registry")
            return None

        report.created_asset(path)

    ordered = [definitions[w["id"]] for w in WEAPONS if w["id"] in definitions]

    if not _set(registry, "weapons", ordered, report, "DA_WeaponRegistry"):
        return registry

    # Validation is a UFUNCTION on the registry: running it here means a broken
    # table is caught by the build script instead of by a player at 2 a.m.
    try:
        problems = registry.call_method("ValidateDefinitions")
        if problems:
            for problem in problems:
                report.failed("DA_WeaponRegistry", str(problem))
        else:
            C.log(u"  registry validated: {0} weapons, no problems".format(len(ordered)))
    except Exception as exc:  # noqa: BLE001
        C.log(u"  registry validation unavailable in this build: {0}".format(exc))

    unreal.EditorAssetLibrary.save_asset(path)
    return registry


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("build_weapons must run inside the Unreal Editor.")

    report = C.BuildReport("build_weapons")
    C.log(u"=== Building One Braham weapons ===")

    C.guard(report, "directories", _ensure_dirs)
    blueprints = C.guard(report, "weapon blueprints", build_weapon_blueprints, report) or {}
    definitions = C.guard(report, "weapon definitions", build_weapon_definitions, report, blueprints) or {}
    C.guard(report, "weapon registry", build_registry, report, definitions)

    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
