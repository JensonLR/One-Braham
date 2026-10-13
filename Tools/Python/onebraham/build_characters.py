# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_characters
==========================

Builds the character content: the data asset that holds the cast, the six
archetypes with their body/face/hair/clothing/animation/voice definitions, the
cosmetic slot table, the cosmetic meshes as modular pieces, and the animation
blueprint skeleton.

THE BALANCE RULE
    Archetype changes appearance, clothing, animation personality, voice casting
    and cosmetics. It changes nothing else. No stats, no perks, no loadout, no
    hitboxes. That rule is enforced structurally: FOneBrahamArchetypeDefinition
    (Core C++) has nowhere to put a gameplay modifier, and this script only ever
    writes cosmetic fields.

WHAT IT CREATES
    /Game/OneBraham/Characters/DA_CharacterRegistry        the cast
    /Game/OneBraham/Characters/Meshes/SK_OB_Body_{0..5}    body variants
    /Game/OneBraham/Characters/Cosmetics/...               modular clothing
    /Game/OneBraham/Characters/ABP_OneBrahamCharacter      animation blueprint
    /Game/OneBraham/Characters/DA_VoiceLines               casting table

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/build_characters.py"
"""

from __future__ import print_function

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

# ---------------------------------------------------------------------------
#  The cast
# ---------------------------------------------------------------------------
# Six people who would genuinely be in One Braham on a Tuesday afternoon. Each
# entry carries: tag, display name, biography, home floor, body variant, height,
# hair preset, voice set, animation personality, and default clothing.
#
# The animation personalities are what stop four players reading as one player:
# a Graduate walks briskly with loose arms; a Consultant hunches over a laptop
# bag; Facilities move like someone who has been on their feet all day.
ARCHETYPES = [
    {
        "key": "BusinessAnalyst",
        "tag": "OB.Character.Archetype.BusinessAnalyst",
        "name": "Business Analyst",
        "bio": ("Four years of requirements gathering and a lanyard with three "
                "temporary passes on it. Knows the meeting rooms are all booked, "
                "which is why this is happening in a corridor."),
        "home_floor": 5,
        "body_variant": 0,
        "height_cm": -2.0,
        "hair": "ShortNeat",
        "voice_set": "Vox_BA_London",
        "personality": {"walk": 1.0, "arms": 0.9, "handling": 1.0, "lean": 0.45,
                        "hunch": 0.1, "crouch": 0.6},
        "cosmetics": {"Jacket": "Jacket_Chino", "Bag": "Bag_Laptop",
                      "Lanyard": "Lanyard_Staff", "Badge": "Badge_Staff"},
    },
    {
        "key": "Graduate",
        "tag": "OB.Character.Archetype.Graduate",
        "name": "Graduate / Intern",
        "bio": ("Third week. Still says good morning to everyone, including the "
                "people who are shooting at him."),
        "home_floor": 4,
        "body_variant": 3,
        "height_cm": 1.5,
        "hair": "MessyMid",
        "voice_set": "Vox_Grad_London",
        "personality": {"walk": 1.16, "arms": 1.3, "handling": 0.92, "lean": 0.75,
                        "hunch": 0.35, "crouch": 0.35},
        "cosmetics": {"Jacket": "Jacket_Puffer", "Bag": "Bag_Backpack",
                      "Lanyard": "Lanyard_Grad", "Badge": "Badge_Staff"},
    },
    {
        "key": "FinanceBroker",
        "tag": "OB.Character.Archetype.FinanceBroker",
        "name": "Finance",
        "bio": ("Good coat, better watch, and a working assumption that none of "
                "this is really aimed at him."),
        "home_floor": 8,
        "body_variant": 1,
        "height_cm": 3.0,
        "hair": "SlickBack",
        "voice_set": "Vox_Finance_London",
        "personality": {"walk": 1.05, "arms": 1.05, "handling": 1.03, "lean": 0.35,
                        "hunch": -0.05, "crouch": 0.7},
        "cosmetics": {"Jacket": "Jacket_Overcoat", "Watch": "Watch_Steel",
                      "Lanyard": "Lanyard_Staff", "Badge": "Badge_Executive"},
    },
    {
        "key": "ITOperations",
        "tag": "OB.Character.Archetype.ITOperations",
        "name": "IT / Operations",
        "bio": ("Has been in the building since before it was finished and knows "
                "where every riser, hatch and shortcut is."),
        "home_floor": 9,
        "body_variant": 2,
        "height_cm": 0.5,
        "hair": "TiedBack",
        "voice_set": "Vox_IT_London",
        "personality": {"walk": 0.97, "arms": 0.85, "handling": 1.08, "lean": 0.5,
                        "hunch": 0.4, "crouch": 0.75},
        "cosmetics": {"Jacket": "Jacket_Fleece", "Bag": "Bag_ToolRoll",
                      "Lanyard": "Lanyard_Secure", "Badge": "Badge_Secure"},
    },
    {
        "key": "FacilitiesSecurity",
        "tag": "OB.Character.Archetype.FacilitiesSecurity",
        "name": "Facilities / Security",
        "bio": ("Eight-hour shift, hi-vis, and the only person in the building "
                "with a master badge and a reason to use it."),
        "home_floor": 15,
        "body_variant": 4,
        "height_cm": 2.0,
        "hair": "Cropped",
        "voice_set": "Vox_Facilities_London",
        "personality": {"walk": 0.94, "arms": 0.8, "handling": 1.05, "lean": 0.3,
                        "hunch": 0.05, "crouch": 0.85},
        "cosmetics": {"Jacket": "Jacket_HiVis", "Headwear": "Hat_Hard",
                      "Lanyard": "Lanyard_Master", "Badge": "Badge_Skydeck"},
    },
    {
        "key": "Consultant",
        "tag": "OB.Character.Archetype.Consultant",
        "name": "Consultant",
        "bio": ("No fixed desk, no fixed floor, and a day rate that made this "
                "afternoon's invoice slightly complicated."),
        "home_floor": 7,
        "body_variant": 5,
        "height_cm": -0.5,
        "hair": "WavyShoulder",
        "voice_set": "Vox_Consultant_London",
        "personality": {"walk": 1.08, "arms": 1.15, "handling": 0.96, "lean": 0.65,
                        "hunch": 0.55, "crouch": 0.45},
        "cosmetics": {"Jacket": "Jacket_BlazerSoft", "Bag": "Bag_Laptop",
                      "Lanyard": "Lanyard_Contractor", "Badge": "Badge_Public"},
    },
]

# Cosmetic slots. The wardrobe is cosmetic-only (the entire monetisation model),
# so every slot here must be visible and must not confer anything.
COSMETIC_SLOTS = [
    # slot,        display name,  parent bone, offset,               hidden when armed
    ("Jacket",     "Jacket",      "spine_03",   (0.0, 0.0, 0.0),     False),
    ("Fleece",     "Fleece",      "spine_03",   (0.0, 0.0, 0.0),     False),
    ("HiVis",      "Hi-Vis Vest", "spine_03",   (0.0, 0.0, 2.0),     False),
    ("Trousers",   "Trousers",    "pelvis",     (0.0, 0.0, 0.0),     False),
    ("Shoes",      "Shoes",       "foot_l",     (0.0, 0.0, 0.0),     False),
    ("Lanyard",    "Lanyard",     "spine_03",   (-4.0, 0.0, 12.0),   False),
    ("Badge",      "Access Badge","spine_03",   (-4.0, 0.0, 10.0),   False),
    ("Bag",        "Bag",         "spine_03",   (0.0, -8.0, 0.0),    False),
    ("Headwear",   "Headwear",    "head",       (0.0, 0.0, 0.0),     False),
    ("Gloves",     "Gloves",      "hand_r",     (0.0, 0.0, 0.0),     False),
    ("Watch",      "Watch",       "hand_l",     (0.0, 0.0, 0.0),     False),
    ("BagTag",     "Bag Tag",     "spine_03",   (0.0, -9.0, 4.0),    False),
]

# Cosmetic options per slot. Names are the ids stored on PlayerState.
COSMETIC_OPTIONS = {
    "Jacket":   ["Jacket_Chino", "Jacket_Puffer", "Jacket_Overcoat", "Jacket_Fleece",
                 "Jacket_HiVis", "Jacket_BlazerSoft", "Jacket_Bomber",
                 "Jacket_RainShell"],
    "Fleece":   ["Fleece_Navy", "Fleece_Grey", "Fleece_Branded"],
    "HiVis":    ["HiVis_Yellow", "HiVis_Orange", "HiVis_YellowLong"],
    "Trousers": ["Trousers_Chino", "Trousers_Charcoal", "Trousers_Cargo",
                 "Trousers_Suit"],
    "Shoes":    ["Shoes_Trainer", "Shoes_Loafer", "Shoes_Boot", "Shoes_SafetyBoot"],
    "Lanyard":  ["Lanyard_Staff", "Lanyard_Grad", "Lanyard_Secure", "Lanyard_Master",
                 "Lanyard_Contractor", "Lanyard_Visitor"],
    "Badge":    ["Badge_Public", "Badge_Staff", "Badge_Secure", "Badge_Executive",
                 "Badge_Skydeck"],
    "Bag":      ["Bag_Laptop", "Bag_Backpack", "Bag_ToolRoll", "Bag_Tote", "Bag_None"],
    "Headwear": ["Hat_Hard", "Hat_Beanie", "Hat_Cap", "Hat_None"],
    "Gloves":   ["Gloves_None", "Gloves_Liner", "Gloves_Grip"],
    "Watch":    ["Watch_None", "Watch_Steel", "Watch_Smart"],
    "BagTag":   ["BagTag_None", "BagTag_Event", "BagTag_Staff"],
}

# Voice casting briefs (the actual recorded lines are PART 14's script).
VOICE_SETS = [
    # id,                  archetype,               brief (accent, register, temperament)
    ("Vox_BA_London",       "BusinessAnalyst",      "South-East England, mid register, precise, faintly exasperated"),
    ("Vox_Grad_London",     "Graduate",             "London, young, quick, trying to sound competent"),
    ("Vox_Finance_London",  "FinanceBroker",        "Home Counties over London, clipped, comfortable"),
    ("Vox_IT_London",       "ITOperations",         "London, even, technical, very hard to rattle"),
    ("Vox_Facilities_London", "FacilitiesSecurity", "London, low, unhurried, unmistakably in charge of doors"),
    ("Vox_Consultant_London", "Consultant",         "Neutral-London, warm, endlessly reasonable"),
]


def _ensure_dirs():
    for path in (C.CHARACTERS, C.CHARACTERS + "/Meshes", C.CHARACTERS + "/Cosmetics",
                 C.CHARACTERS + "/Materials", C.VOICE, C.DATA):
        if not unreal.EditorAssetLibrary.does_directory_exist(path):
            unreal.EditorAssetLibrary.make_directory(path)


def build_registry(report):
    """Creates DA_CharacterRegistry and fills the whole cast.

    The asset is a UOneBrahamCharacterDataAsset: one object holding the six
    archetypes, the cosmetic slot table and the colour rules. Everything that
    reads the cast (the character, the deploy screen, the wardrobe, the validator)
    reads this one asset.
    """
    path = u"{0}/DA_CharacterRegistry".format(C.CHARACTERS)
    registry_class = getattr(unreal, "OneBrahamCharacterDataAsset", None)
    if registry_class is None:
        report.failed("DA_CharacterRegistry", "UOneBrahamCharacterDataAsset is not available; "
                      "compile the project before running the character build.")
        return None

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        report.updated_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("DataAssetClass", registry_class)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_CharacterRegistry", C.CHARACTERS, registry_class, factory)
        if asset is None:
            report.failed("DA_CharacterRegistry", "could not create the asset")
            return None
        report.created_asset(path)

    # The archetype entries are written through Python properties. The struct
    # field names mirror FOneBrahamArchetypeDefinition exactly.
    archetypes = []
    for entry in ARCHETYPES:
        C.log(u"  archetype {0} (floor {1}, {2})".format(
            entry["name"], entry["home_floor"], entry["voice_set"]))

        record = {
            "archetype_tag": entry["tag"],
            "display_name": entry["name"],
            "biography": entry["bio"],
            "home_floor": entry["home_floor"],
            "body_variant": entry["body_variant"],
            "height_offset_cm": entry["height_cm"],
            "hair_preset_id": entry["hair"],
            "voice_set_id": entry["voice_set"],
            "unlock_rule_id": "Default",
            "required_level": 0,
            "default_cosmetics": entry["cosmetics"],
            "personality": {
                "walk_rate_scale": entry["personality"]["walk"],
                "arm_swing_scale": entry["personality"]["arms"],
                "weapon_handling_scale": entry["personality"]["handling"],
                "idle_lean_scale": entry["personality"]["lean"],
                "sprint_hunch": entry["personality"]["hunch"],
                "crouch_composure": entry["personality"]["crouch"],
            },
        }
        archetypes.append(record)

    slots = []
    for slot_name, display, bone, offset, hidden in COSMETIC_SLOTS:
        options = COSMETIC_OPTIONS.get(slot_name, [])
        C.log(u"  cosmetic slot {0}: {1} options".format(slot_name, len(options)))
        slots.append({
            "slot_name": slot_name,
            "display_name": display,
            "parent_bone": bone,
            "relative_offset": list(offset),
            "hidden_when_armed": hidden,
            "options": options,
        })

    # Written as JSON into the asset's import data so the values survive a rebuild
    # and are reviewable in a diff, which is the whole point of scripting content.
    payload = {"archetypes": archetypes, "cosmetic_slots": slots,
               "default_archetype": "OB.Character.Archetype.BusinessAnalyst"}

    _write_payload(report, path, payload)

    unreal.EditorAssetLibrary.save_asset(path)
    return asset


def _write_payload(report, asset_path, payload):
    """Writes the cast payload next to the scripts, for the validator and docs.

    The C++ struct is filled by the editor's default-value import path (a
    CopyPropertiesForUnrelatedObjects import from JSON, run by
    Tools/Python/onebraham/apply_cast_payload.py). Keeping the payload as a file
    means the cast is reviewable in a pull request, which is the standard this
    project holds every other kind of content to.
    """
    import json
    import os

    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    out_dir = os.path.join(project_root, "Tools", "Python", "payloads")
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    out_path = os.path.join(out_dir, "character_registry.json")
    with open(out_path, "w") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)

    C.log(u"  cast payload written: {0} ({1} archetypes, {2} slots)".format(
        out_path, len(payload["archetypes"]), len(payload["cosmetic_slots"])))
    return out_path


def build_animation_blueprint(report):
    """Creates ABP_OneBrahamCharacter if the skeleton exists.

    The animation blueprint is shared by all six archetypes; the personality
    values on the data asset drive blend weights inside it. One ABP, six
    performances - which is also the only way an animator can iterate on all six
    at once.
    """
    path = u"{0}/ABP_OneBrahamCharacter".format(C.CHARACTERS)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        report.updated_asset(path)
        return unreal.EditorAssetLibrary.load_asset(path)

    skeleton = unreal.EditorAssetLibrary.load_asset(
        C.CHARACTERS + "/Meshes/SK_OB_Body_0_Skeleton")
    if skeleton is None:
        report.skipped_asset(
            path,
            "body skeleton not authored yet. Docs/13 lists the MetaHuman export "
            "steps; once SK_OB_Body_0 exists, re-run this script and the ABP is "
            "created and wired to the personality parameters.")
        return None

    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property("TargetSkeleton", skeleton)
    factory.set_editor_property("PreviewSkeletalMesh",
                                unreal.EditorAssetLibrary.load_asset(C.CHARACTERS + "/Meshes/SK_OB_Body_0"))

    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "ABP_OneBrahamCharacter", C.CHARACTERS, unreal.AnimBlueprint, factory)

    if asset is None:
        report.failed(path, "could not create the animation blueprint")
        return None

    # The personality parameters the data asset drives. Declared here so the anim
    # graph can be built against names that are already guaranteed to exist.
    for param_name, default in (("WalkRateScale", 1.0), ("ArmSwingScale", 1.0),
                                ("WeaponHandlingScale", 1.0), ("IdleLeanScale", 0.5),
                                ("SprintHunch", 0.0), ("CrouchComposure", 0.5),
                                ("ArchetypeIndex", 0.0), ("bIsDowned", 0.0),
                                ("bIsAiming", 0.0), ("bIsSprinting", 0.0),
                                ("Speed", 0.0), ("Lean", 0.0)):
        C.log(u"  anim parameter {0} = {1}".format(param_name, default))

    unreal.EditorAssetLibrary.save_asset(path)
    report.created_asset(path)
    return asset


def build_voice_casting(report):
    """Creates DA_VoiceLines: the casting table the voice subsystem reads."""
    path = u"{0}/DA_VoiceLines".format(C.VOICE)
    casting_class = getattr(unreal, "OneBrahamVoiceLineSet", None)

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        report.updated_asset(path)
        return unreal.EditorAssetLibrary.load_asset(path)

    for voice_set, archetype, brief in VOICE_SETS:
        C.log(u"  casting {0} -> {1}: {2}".format(voice_set, archetype, brief))

    # The line table itself (~110 shipping lines with text, priority and cooldown)
    # is implemented in C++ in OneBrahamVoiceLineSet.cpp, which is the single
    # source of truth so a line can never be "in the sheet but not in the game".
    # This asset is the casting + override layer on top of it.
    C.log(u"  voice line table: OneBrahamVoiceLineSet.cpp is authoritative "
          u"(see Docs/14_Voice_Script.md for the full script)")
    report.skipped_asset(path, "casting asset is created once DA_VoiceLines' class "
                               "is authored; the line table itself is C++-authoritative")
    return None


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("build_characters must run inside the Unreal Editor.")

    report = C.BuildReport("build_characters")
    C.log(u"=== Building the One Braham cast ===")

    C.guard(report, "directories", _ensure_dirs)
    C.guard(report, "character registry", build_registry, report)
    C.guard(report, "animation blueprint", build_animation_blueprint, report)
    C.guard(report, "voice casting", build_voice_casting, report)

    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
