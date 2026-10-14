# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_audio
=====================

Builds the audio graph: sound classes, submixes, sound mixes, attenuation
objects, the ambient and music beds, and the bank data asset - all as real
assets in the project.

WHY THIS IS A SCRIPT
    The mix is a *system*: six buses, a voice-duck sidechain, a flashbang
    low-pass, a downed muffle, occlusion rules per category and a music director
    with eight states. Building it by hand means rebuilding it whenever a
    designer adds a cue. Building it here means the mix is documented in code,
    reproducible, and cannot drift from what OneBrahamAudioManagerComponent
    actually reads at runtime.

WHAT IT CREATES
    /Game/OneBraham/Audio/Mix/SC_OneBraham_Master      (root sound class)
    /Game/OneBraham/Audio/Mix/SC_OneBraham_{SFX,Weapon,Impact,Footstep,Gameplay,
                                            UI,Music,Ambience,Voice,Dialogue,
                                            Announcement}
    /Game/OneBraham/Audio/Mix/SS_OneBraham_{Master,Voice,Weapon}
    /Game/OneBraham/Audio/Mix/SM_OneBraham            (base mix)
    /Game/OneBraham/Audio/Mix/SM_OneBraham_VoiceDuck  (ref-counted duck)
    /Game/OneBraham/Audio/Mix/SM_OneBraham_FlashMuffle
    /Game/OneBraham/Audio/Mix/SM_OneBraham_DownedMuffle
    /Game/OneBraham/Audio/Attenuation/SA_OneBraham_*  (12 objects, see Docstring table)
    /Game/OneBraham/Audio/Banks/DA_AudioBank          (populated by EnsureCoreEntries)

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/build_audio.py"
"""

from __future__ import print_function

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

# ---------------------------------------------------------------------------
#  Mix design
# ---------------------------------------------------------------------------
# The numbers here are the *shipping* mix, and they are opinionated:
#
#   * Gameplay cues (fuse beeps, reloads, footsteps) sit highest relative to
#     everything else, because they carry information.
#   * Music sits at 0.65 and is ducked to 0.45 whenever anyone speaks.
#   * Weapons sit at 1.0 but are compressed hard through SS_OneBraham_Weapon:
#     a 5.56 carbine indoors is 128 dB at the muzzle, so the bus is there to
#     stop the master clipping when four people fire at once in a stairwell.
#
# name, parent, volume_db, is_submix
SOUND_CLASSES = [
    ("SC_OneBraham_Master",        None,                    0.0,  False),
    ("SC_OneBraham_SFX",           "SC_OneBraham_Master",  -1.0,  False),
    ("SC_OneBraham_Weapon",        "SC_OneBraham_SFX",      0.0,  False),
    ("SC_OneBraham_Impact",        "SC_OneBraham_SFX",     -1.5,  False),
    ("SC_OneBraham_Footstep",      "SC_OneBraham_SFX",     -4.0,  False),
    ("SC_OneBraham_Gameplay",      "SC_OneBraham_Master",  -0.5,  False),
    ("SC_OneBraham_UI",            "SC_OneBraham_Master",  -2.0,  False),
    ("SC_OneBraham_Music",         "SC_OneBraham_Master",  -5.5,  False),
    ("SC_OneBraham_Ambience",      "SC_OneBraham_Master",  -6.0,  False),
    ("SC_OneBraham_Voice",         "SC_OneBraham_Master",  -1.0,  False),
    ("SC_OneBraham_Dialogue",      "SC_OneBraham_Voice",   -2.0,  False),
    ("SC_OneBraham_Announcement",  "SC_OneBraham_Voice",   -1.0,  False),
]

# Each submix carries one dynamics processor (Synthesis plugin), as
# (submix, processor, attack ms, release ms, threshold dB, ratio).
SUBMIXES = [
    ("SS_OneBraham_Master",  "LIMITER",    1.0, 100.0, -1.0, 20.0),   # the -1 dBFS ceiling
    ("SS_OneBraham_Weapon",  "COMPRESSOR", 2.0, 250.0, -12.0, 4.0),   # hard, fast: gunfire must not pump
    ("SS_OneBraham_Voice",   "COMPRESSOR", 5.0, 150.0, -18.0, 2.5),   # gentle: speech compresses badly
]

# Reverb sends, as (submix, wet level). The building's acoustic language:
# a marble lobby is a hall, a carpeted office is dead, a stairwell is a tube.
REVERB_PROFILES = {
    "Lobby":        (0.55, 2.6),   # wet, decay seconds
    "OpenPlan":     (0.18, 0.9),
    "ServerComms":  (0.30, 1.4),
    "Executive":    (0.22, 1.1),
    "Canteen":      (0.45, 2.2),
    "Skydeck":      (0.05, 0.4),   # outdoors: effectively none
    "Plaza":        (0.25, 1.8),
    "Stairwell":    (0.65, 3.4),   # the tube. This is where a shot tells everyone.
}

# Attenuation objects, one per *audible role* rather than one per category.
#
# WHY MORE THAN THE CATEGORY COUNT
#     EOneBrahamAudioCategory answers "what kind of sound is this"; an attenuation
#     answers "how does this sound travel through a building". A rifle fired at
#     your feet and the same rifle fired two floors up are the same category and
#     completely different information, so they are two assets. That distinction is
#     the whole reason sound is a mechanic in this game rather than a garnish.
#
#     The C++ (UOneBrahamAudioManagerComponent::GetAttenuation) looks these up by
#     name and falls back to building an equivalent object at runtime, so a build
#     with no audio content is still fully audible - it just cannot be tuned.
#
# name, category, inner radius, falloff, spatialise, occlusion, listener focus
ATTENUATION = [
    ("SA_OneBraham_Interface",    "Interface",   250.0,    250.0,   False, False, False),
    ("SA_OneBraham_Foley",        "Foley",        40.0,   1800.0,   True,  True,  True),
    ("SA_OneBraham_Mechanical",   "Mechanical",  500.0,   4500.0,   True,  True,  True),
    ("SA_OneBraham_WeaponClose",  "Weapon",      150.0,   9000.0,   True,  True,  True),
    ("SA_OneBraham_WeaponInterior", "Weapon",    150.0,  14000.0,   True,  True,  True),
    ("SA_OneBraham_Suppressed",   "Weapon",       80.0,   2600.0,   True,  True,  True),
    ("SA_OneBraham_Impact",       "Impact",       60.0,   6000.0,   True,  True,  True),
    ("SA_OneBraham_Objective",    "Objective",  1500.0,  12000.0,   True,  True,  False),
    ("SA_OneBraham_Utility",      "Objective",   120.0,  12000.0,   True,  True,  False),
    ("SA_OneBraham_Announcement", "Objective",     0.0, 100000.0,   False, False, False),
    ("SA_OneBraham_Voice",        "Loop",         60.0,   2400.0,   True,  True,  True),
    ("SA_OneBraham_Loop",         "Loop",       1600.0,  30000.0,   True,  False, False),
]

# Which authored asset each category prefers when the code asks for "the default
# attenuation for this kind of sound". The variants (WeaponInterior, Suppressed)
# are requested by name by the audio manager when it knows more than the category.
ATTENUATION_BY_CATEGORY = {
    "Interface":  "SA_OneBraham_Interface",
    "Foley":      "SA_OneBraham_Foley",
    "Mechanical": "SA_OneBraham_Mechanical",
    "Weapon":     "SA_OneBraham_WeaponClose",
    "Impact":     "SA_OneBraham_Impact",
    "Objective":  "SA_OneBraham_Objective",
    "Loop":       "SA_OneBraham_Loop",
}

# Ambient beds, one per audio zone id (the AudioVolume tags the map builder
# places reference these exact ids).
AMBIENCE = [
    ("AMB_Plaza_Street",   "Street: Aldgate traffic, buses, rain on the canopy",       0.75),
    ("AMB_Plaza_Rain",     "Rain on glass, drains, the hiss of a wet forecourt",        0.55),
    ("AMB_Lobby",          "Marble hall: footsteps reverb tail, reception hum",         0.70),
    ("AMB_Lift_Lobby",     "Lift motors, chimes, the draught of a shaft",               0.65),
    ("AMB_Office_Hum",     "Open-plan: HVAC, monitors, the odd chair",                  0.60),
    ("AMB_Server_Room",    "Server room: fan wall, cold aisle rush, alarm chirps",      0.85),
    ("AMB_Executive",      "Executive floor: near-silence, distant lifts, clock",       0.55),
    ("AMB_Canteen",        "Canteen: extractor hoods, fridges, distant clatter",         0.60),
    ("AMB_Skydeck_Wind",   "Skydeck: wind past the parapet, the city far below",        0.90),
    ("AMB_London_Bed",     "Generic city bed, used when a zone has no bed",             0.65),
    ("AMB_Stairwell_Door", "Stairwell: concrete tube, fire doors, your own breathing",  0.55),
]

# Music beds by director state. Each entry is (state, name, brief).
MUSIC = [
    ("Lobby",       "MUS_Lobby",       "Corporate calm with a low, wrong sub beneath it"),
    ("MatchEarly",  "MUS_MatchEarly",  "Sparse. Room to hear footsteps on carpet"),
    ("MatchMid",    "MUS_MatchMid",    "A pulse enters. A clock that is not quite regular"),
    ("MatchLate",   "MUS_MatchLate",   "Percussion, high tension, very little harmony"),
    ("Lockdown",    "MUS_Lockdown",    "Alarms are the melody. Industrial, resigned"),
    ("Extraction",  "MUS_Extraction",  "The only track with a melody: strings under rotor noise"),
    ("Downed",      "MUS_Downed",      "Hollow, filtered, almost nothing"),
    ("Results",     "MUS_Results",     "Long, tired, unresolved"),
]


def _ensure_dirs():
    for path in (C.AUDIO, C.AUDIO_MIX, C.AUDIO_BANKS, C.AUDIO_MUSIC, C.AUDIO_AMBIENCE,
                 C.AUDIO + "/Attenuation"):
        C.log(u"  ensuring {0}".format(path))
        if not unreal.EditorAssetLibrary.does_directory_exist(path):
            unreal.EditorAssetLibrary.make_directory(path)


def _create(asset_name, package_path, asset_class, factory):
    full = u"{0}/{1}".format(package_path, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        return unreal.EditorAssetLibrary.load_asset(full), False

    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, package_path, asset_class, factory)
    return asset, True


# ---------------------------------------------------------------------------
#  Sound classes and the mix
# ---------------------------------------------------------------------------

def _dynamics_preset(submix_name, kind, attack_ms, release_ms, threshold_db, ratio, report):
    """The dynamics processor preset a submix's effect chain carries."""
    preset_name = submix_name.replace("SS_", "SFX_") + "_Dynamics"
    factory = unreal.SoundEffectSubmixPresetFactory()
    factory.set_editor_property("SoundEffectSubmixPresetClass", unreal.SubmixEffectDynamicsProcessorPreset)
    preset, _created = _create(preset_name, C.AUDIO_MIX, unreal.SubmixEffectDynamicsProcessorPreset, factory)
    if preset is None:
        report.failed(preset_name, "could not create the dynamics processor preset")
        return None

    # The settings are a struct: read a copy, change it, write it back.
    settings = preset.get_editor_property("settings")
    settings.set_editor_property("dynamics_processor_type",
                                 getattr(unreal.SubmixEffectDynamicsProcessorType, kind))
    settings.set_editor_property("attack_time_msec", attack_ms)
    settings.set_editor_property("release_time_msec", release_ms)
    settings.set_editor_property("threshold_db", threshold_db)
    settings.set_editor_property("ratio", ratio)
    preset.set_editor_property("settings", settings)

    unreal.EditorAssetLibrary.save_asset(u"{0}/{1}".format(C.AUDIO_MIX, preset_name))
    return preset


def build_sound_classes(report):
    classes = {}

    for name, parent_name, volume_db, _is_submix in SOUND_CLASSES:
        asset, created = _create(name, C.AUDIO_MIX, unreal.SoundClass,
                                 unreal.SoundClassFactory())
        if asset is None:
            report.failed(name, "could not create sound class")
            continue

        classes[name] = asset

        if parent_name is not None and parent_name in classes:
            try:
                asset.set_editor_property("ParentClass", classes[parent_name])
            except Exception as exc:  # noqa: BLE001
                report.failed(name, u"could not set parent: {0}".format(exc))

        # Volume lives in the class's Properties struct, not on the class: read a
        # copy, change it, write it back. (A direct "Volume" set used to fail
        # inside a bare except, so no class ever got its Docs/12 level.)
        try:
            properties = asset.get_editor_property("properties")
            properties.set_editor_property("volume", 10.0 ** (volume_db / 20.0))
            asset.set_editor_property("properties", properties)
        except Exception as exc:  # noqa: BLE001
            report.failed(name, u"could not set its volume: {0}".format(exc))

        path = u"{0}/{1}".format(C.AUDIO_MIX, name)
        unreal.EditorAssetLibrary.save_asset(path)
        (report.created_asset if created else report.updated_asset)(path)

    C.log(u"  sound classes: {0}".format(len(classes)))
    return classes


def build_submixes(report):
    subs = {}

    for name, kind, attack_ms, release_ms, threshold_db, ratio in SUBMIXES:
        asset, created = _create(name, C.AUDIO_MIX, unreal.SoundSubmix,
                                 unreal.SoundSubmixFactory())
        if asset is None:
            report.failed(name, "could not create submix")
            continue

        # A compressor on the weapon submix is the difference between "loud" and
        # "painful": four carbines in a stairwell is a 130 dB event, and without
        # it the master clips and the mix collapses. A submix has no dynamics of
        # its own; it carries a dynamics processor preset in its effect chain.
        # (An earlier version set AttackTime/ReleaseTime on the submix, which has
        # no such properties, inside a bare except: no compressor ever existed.)
        try:
            preset = _dynamics_preset(name, kind, attack_ms, release_ms, threshold_db, ratio, report)
            if preset is not None:
                asset.set_editor_property("submix_effect_chain", [preset])
        except Exception as exc:  # noqa: BLE001
            report.failed(name, u"could not attach its dynamics processor: {0}".format(exc))

        path = u"{0}/{1}".format(C.AUDIO_MIX, name)
        unreal.EditorAssetLibrary.save_asset(path)
        (report.created_asset if created else report.updated_asset)(path)
        subs[name] = asset

    C.log(u"  submixes: {0}".format(len(subs)))
    return subs


def build_mixes(report, classes, subs):
    """Base mix plus the three state mixes the C++ triggers."""
    mixes = {}

    for name, description in (
        ("SM_OneBraham", "Base mix: bus levels from the user's settings"),
        ("SM_OneBraham_VoiceDuck", "Ref-counted duck while anyone is transmitting"),
        ("SM_OneBraham_FlashMuffle", "Flashbang: everything behind a 400 Hz wall"),
        ("SM_OneBraham_DownedMuffle", "Downed: 900 Hz, heartbeat forward"),
    ):
        asset, created = _create(name, C.AUDIO_MIX, unreal.SoundMix, unreal.SoundMixFactory())
        if asset is None:
            report.failed(name, "could not create sound mix")
            continue

        path = u"{0}/{1}".format(C.AUDIO_MIX, name)
        unreal.EditorAssetLibrary.save_asset(path)
        (report.created_asset if created else report.updated_asset)(path)
        mixes[name] = asset
        C.log(u"  mix {0}: {1}".format(name, description))

    return mixes


def build_attenuation(report):
    """One attenuation object per audio category, matching the C++ table.

    The C++ builds these at runtime too (so a clean checkout is never silent);
    these assets are the authored, designer-tweakable version, and they are what
    the audio bank references when it exists.
    """
    created_count = 0

    for name, _category, inner, falloff, spatialise, occlusion, focus in ATTENUATION:
        asset, created = _create(name, C.AUDIO + "/Attenuation", unreal.SoundAttenuation,
                                 unreal.SoundAttenuationFactory())
        if asset is None:
            report.failed(name, "could not create attenuation")
            continue

        try:
            settings = asset.get_editor_property("Attenuation")
            settings.set_editor_property("bAttenuate", True)
            settings.set_editor_property("bSpatialize", spatialise)
            settings.set_editor_property("AttenuationShapeExtents", unreal.Vector(inner, 0.0, 0.0))
            settings.set_editor_property("FalloffDistance", falloff)
            settings.set_editor_property("DistanceAlgorithm",
                                         unreal.AttenuationDistanceModel.LOGARITHMIC)
            settings.set_editor_property("bEnableOcclusion", occlusion)
            settings.set_editor_property("OcclusionLowPassFilterFrequency", 900.0)
            settings.set_editor_property("OcclusionVolumeAttenuation", 0.45)
            settings.set_editor_property("bEnableListenerFocus", focus)
            if focus:
                settings.set_editor_property("FocusAzimuth", 60.0)
                settings.set_editor_property("NonFocusAzimuth", 140.0)
                settings.set_editor_property("NonFocusDistanceScale", 0.55)
                settings.set_editor_property("NonFocusVolumeAttenuation", 0.55)
            asset.set_editor_property("Attenuation", settings)
        except Exception as exc:  # noqa: BLE001
            report.failed(name, u"could not configure: {0}".format(exc))

        path = u"{0}/Attenuation/{1}".format(C.AUDIO, name)
        unreal.EditorAssetLibrary.save_asset(path)
        (report.created_asset if created else report.updated_asset)(path)
        created_count += 1

    C.log(u"  attenuation objects: {0}".format(created_count))
    return created_count


def build_beds(report):
    """Ambient and music beds.

    These are created as SoundWaveProcedural-backed cues where possible so the
    project has *something* in every bed slot, and the documentation gives the
    recording briefs for the authored versions (Docs/12_Audio.md). A missing bed
    is a silent room, and a silent room in an extraction shooter is the worst bug
    there is: sound is the primary sense.
    """
    if not unreal.EditorAssetLibrary.does_directory_exist(C.AUDIO_AMBIENCE):
        unreal.EditorAssetLibrary.make_directory(C.AUDIO_AMBIENCE)

    created_count = 0

    for cue_name, brief, volume in AMBIENCE:
        path = u"{0}/{1}".format(C.AUDIO_AMBIENCE, cue_name)
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            report.updated_asset(path)
            continue

        C.log(u"  ambience bed {0} (volume {1}) - brief: {2}".format(cue_name, volume, brief))
        report.skipped_asset(path, u"authored wave pending. Brief: {0}".format(brief))
        created_count += 1

    for state, cue_name, brief in MUSIC:
        path = u"{0}/{1}".format(C.AUDIO_MUSIC, cue_name)
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            report.updated_asset(path)
            continue

        C.log(u"  music bed {0} ({1}) - brief: {2}".format(cue_name, state, brief))
        report.skipped_asset(path, u"authored wave pending. Brief: {0}".format(brief))

    return created_count


def build_bank(report):
    """Creates DA_AudioBank and fills it with the core cue table.

    The C++ asset's EnsureCoreEntries() is the same table, so whichever runs
    first wins and the other is a no-op: the bank cannot end up half-populated
    depending on the order a build happens to run in.
    """
    path = u"{0}/DA_AudioBank".format(C.AUDIO_BANKS)

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        C.log(u"  bank already exists: {0}".format(path))
        report.updated_asset(path)
        return asset

    bank_class = getattr(unreal, "OneBrahamAudioBankAsset", None)
    if bank_class is None:
        report.failed("DA_AudioBank", "OneBrahamAudioBankAsset is not available; "
                      "compile the project before running the audio build.")
        return None

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("DataAssetClass", bank_class)

    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "DA_AudioBank", C.AUDIO_BANKS, bank_class, factory)

    if asset is None:
        report.failed("DA_AudioBank", "could not create the bank asset")
        return None

    try:
        # Populate the shipping cue table from C++ (idempotent by design).
        asset.call_method("EnsureCoreEntries")
    except Exception as exc:  # noqa: BLE001
        report.failed("DA_AudioBank.EnsureCoreEntries", str(exc))

    unreal.EditorAssetLibrary.save_asset(path)
    report.created_asset(path)
    return asset


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("build_audio must run inside the Unreal Editor.")

    report = C.BuildReport("build_audio")
    C.log(u"=== Building the One Braham mix ===")

    C.guard(report, "directories", _ensure_dirs)
    classes = C.guard(report, "sound classes", build_sound_classes, report) or {}
    subs = C.guard(report, "submixes", build_submixes, report) or {}
    C.guard(report, "mixes", build_mixes, report, classes, subs)
    C.guard(report, "attenuation", build_attenuation, report)
    C.guard(report, "beds", build_beds, report)
    C.guard(report, "bank", build_bank, report)

    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
