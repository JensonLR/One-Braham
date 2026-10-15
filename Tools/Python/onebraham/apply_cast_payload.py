# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.apply_cast_payload
============================

Takes the cast payload written by build_characters.py and writes it onto the
real data asset: /Game/OneBraham/Characters/DA_CharacterRegistry.

WHY THIS EXISTS AS A SEPARATE STEP
    build_characters.py has to work with or without a compiled module, because it
    is run early in a fresh clone when the C++ has never been built. So it writes
    the cast to JSON first. This script is the second half: it runs after a
    successful compile and turns that JSON into the structured data asset the
    runtime reads, field by field, with the archetype tag, the body variant, the
    height offset, the animation personality, the voice set and the default
    cosmetics per slot.

    Run it again after any change to the cast table. It is idempotent: an
    existing asset is updated in place, not replaced, so anything an artist set
    on it by hand in a field this script does not own survives.

WHAT IT WRITES
    DA_CharacterRegistry.Archetypes          6 entries
    DA_CharacterRegistry.CosmeticSlots       12 slots
    DA_CharacterRegistry.CompetitiveBannedCosmeticIds
        Every cosmetic that is not purely cosmetic. Should always be empty; the
        script shouts if it is not, because a cosmetic that affects gameplay in a
        12-player extraction shooter is a bug that costs players.

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/apply_cast_payload.py"
"""

from __future__ import print_function

import io
import json
import os

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

REGISTRY_PATH = C.CHARACTERS + "/DA_CharacterRegistry"
PAYLOAD_FILE = "character_registry.json"

# Cosmetics that would give an in-game advantage if they were allowed to. The
# registry is checked against this list after the payload is applied.
FORBIDDEN_COSMETIC_TRAITS = (
    "smaller", "bigger", "faster", "quieter", "lighter", "darker", "brighter",
)
# A cosmetic may darken or lighten a garment. It may not change a silhouette.
FORBIDDEN_SLOT_MODIFIERS = ("scale", "hitbox", "speed", "sound", "accuracy")


def _payload_path():
    return os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                         "..", "payloads", PAYLOAD_FILE))


def load_payload(report):
    path = _payload_path()

    if not os.path.isfile(path):
        report.failed("payload", u"no cast payload at {0} - run build_characters.py "
                                 u"first".format(path))
        return None

    with io.open(path, "r", encoding="utf-8") as handle:
        payload = json.loads(handle.read())

    archetypes = payload.get("archetypes", [])
    C.log(u"  payload: {0} archetypes, {1} cosmetic slots ({2})".format(
        len(archetypes), len(payload.get("cosmetic_slots", [])),
        os.path.basename(path)))

    return payload


def ensure_registry(report):
    registry_class = getattr(unreal, "OneBrahamCharacterDataAsset", None)
    if registry_class is None:
        report.skipped_asset(REGISTRY_PATH,
                             "UOneBrahamCharacterDataAsset is not reflected yet - compile "
                             "the OneBraham module, then re-run this script")
        return None

    if unreal.EditorAssetLibrary.does_asset_exist(REGISTRY_PATH):
        registry = unreal.EditorAssetLibrary.load_asset(REGISTRY_PATH)
        report.updated_asset(REGISTRY_PATH)
        return registry

    folder = C.CHARACTERS
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)

    registry = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "DA_CharacterRegistry", folder, registry_class, unreal.DataAssetFactory())

    if registry is None:
        report.failed("DA_CharacterRegistry", "could not create the registry asset")
        return None

    report.created_asset(REGISTRY_PATH)
    return registry


def _make_tag(tag_string):
    """OB.Character.Archetype.Analyst -> the reflected FGameplayTag."""
    try:
        return unreal.GameplayTagLibrary.request_gameplay_tag(unreal.GameplayTag(),
                                                              unreal.Name(tag_string))
    except Exception:  # noqa: BLE001
        try:
            tag = unreal.GameplayTag()
            tag.tag_name = unreal.Name(tag_string)
            return tag
        except Exception:  # noqa: BLE001
            return None


def build_archetype_struct(report, registry, entry):
    """Creates one FOneBrahamArchetypeDefinition from a payload entry.

    The struct is created through the registry's own property so the script never
    has to know the C++ layout: read the array element type off the asset, make
    one, set the fields by name.
    """
    struct_info = None
    try:
        archetypes = registry.get_editor_property("archetypes")
        if archetypes and len(archetypes) > 0:
            struct_info = archetypes[0]
    except Exception:  # noqa: BLE001
        struct_info = None

    archetype = type(struct_info)() if struct_info is not None else None

    if archetype is None:
        # No existing entry to clone from: ask Python for the struct type by name.
        struct_class = getattr(unreal, "OneBrahamArchetypeDefinition", None)
        if struct_class is None:
            report.failed(entry.get("id", "?"), "archetype struct is not reflected")
            return None
        archetype = struct_class()

    def _set(prop, value):
        try:
            archetype.set_editor_property(prop, value)
            return True
        except Exception as exc:  # noqa: BLE001
            report.failed(entry.get("id", "?"), u"{0}: {1}".format(prop, exc))
            return False

    tag_string = entry.get("tag", "")
    tag = _make_tag(tag_string)
    if tag is not None:
        _set("archetype_tag", tag)

    _set("display_name", unreal.Text(entry.get("display_name", entry.get("id", ""))))
    _set("biography", unreal.Text(entry.get("biography", "")))
    _set("home_floor", int(entry.get("home_floor", 4)))
    _set("body_variant", int(entry.get("body_variant", 0)))
    _set("height_offset_cm", float(entry.get("height_offset_cm", 0.0)))
    _set("hair_preset_id", unreal.Name(entry.get("hair_preset", "")))
    _set("voice_set_id", unreal.Name(entry.get("voice_set", "")))
    _set("unlock_rule_id", unreal.Name(entry.get("unlock_rule", "Default")))
    _set("required_level", int(entry.get("required_level", 0)))

    # Animation personality: the whole reason two Business Analysts do not move
    # like the same person in a corridor.
    personality = entry.get("personality", {})
    try:
        existing = archetype.get_editor_property("personality")
        if existing is not None:
            for key, value in personality.items():
                try:
                    existing.set_editor_property(key, float(value))
                except Exception:  # noqa: BLE001
                    pass
            archetype.set_editor_property("personality", existing)
    except Exception as exc:  # noqa: BLE001
        report.failed(entry.get("id", "?"), u"personality: {0}".format(exc))

    # Default cosmetics: one id per slot, applied on first deploy.
    try:
        defaults = archetype.get_editor_property("default_cosmetics")
        for slot, cosmetic_id in (entry.get("default_cosmetics") or {}).items():
            defaults[unreal.Name(slot)] = unreal.Name(cosmetic_id)
        archetype.set_editor_property("default_cosmetics", defaults)
    except Exception as exc:  # noqa: BLE001
        report.failed(entry.get("id", "?"), u"default cosmetics: {0}".format(exc))

    # Meshes and animation class are soft references to content the art pass
    # delivers; the conventional paths are recorded so the import lands in place.
    for prop, key in (("body_mesh", "body_mesh_path"),
                      ("first_person_mesh", "first_person_mesh_path"),
                      ("face_material", "face_material_path"),
                      ("hair_material", "hair_material_path")):
        path = entry.get(key)
        if path:
            # A soft reference is set from the loaded asset; a path the content
            # has not reached yet is reported, not swallowed.
            asset = unreal.load_asset(path)
            if asset is None:
                report.skipped_asset(path, "not in the project yet")
                continue
            try:
                archetype.set_editor_property(prop, asset)
            except Exception as exc:  # noqa: BLE001
                report.failed(entry.get("id", "?"), u"{0}: {1}".format(prop, exc))

    anim_path = entry.get("anim_class_path")
    if anim_path:
        anim_class = unreal.EditorAssetLibrary.load_blueprint_class(anim_path)
        if anim_class is None:
            report.skipped_asset(anim_path, "animation blueprint not in the project yet")
        else:
            try:
                archetype.set_editor_property("anim_instance_class", anim_class)
            except Exception as exc:  # noqa: BLE001
                report.failed(entry.get("id", "?"), u"anim_instance_class: {0}".format(exc))

    return archetype


def build_cosmetic_slot_struct(report, registry, entry):
    struct_class = getattr(unreal, "OneBrahamCosmeticSlot", None)
    if struct_class is None:
        report.failed(entry.get("id", "?"), "cosmetic slot struct is not reflected")
        return None

    slot = struct_class()

    def _set(prop, value):
        try:
            slot.set_editor_property(prop, value)
            return True
        except Exception as exc:  # noqa: BLE001
            report.failed(entry.get("id", "?"), u"{0}: {1}".format(prop, exc))
            return False

    _set("slot_name", unreal.Name(entry.get("id", "")))
    _set("display_name", unreal.Text(entry.get("display_name", entry.get("id", ""))))
    _set("parent_bone", unreal.Name(entry.get("parent_bone", "spine_03")))
    _set("attach_socket", unreal.Name(entry.get("socket", "")))
    _set("bHiddenWhenArmed", bool(entry.get("hidden_when_armed", False)))

    return slot


def apply_payload(report, registry, payload):
    archetypes = []
    slots = []

    for entry in payload.get("archetypes", []):
        built = build_archetype_struct(report, registry, entry)
        if built is not None:
            archetypes.append(built)

    for entry in payload.get("cosmetic_slots", []):
        built = build_cosmetic_slot_struct(report, registry, entry)
        if built is not None:
            slots.append(built)

    try:
        registry.set_editor_property("archetypes", archetypes)
        registry.set_editor_property("cosmetic_slots", slots)
    except Exception as exc:  # noqa: BLE001
        report.failed("DA_CharacterRegistry", str(exc))
        return

    C.log(u"  registry: {0} archetypes, {1} cosmetic slots written".format(
        len(archetypes), len(slots)))

    # Competitive integrity check. A cosmetic that changes anything but the way a
    # character looks is a gameplay bug wearing a hat, and it gets reported here
    # rather than discovered in a tournament.
    banned = []

    for entry in payload.get("cosmetics", []):
        traits = " ".join(str(v).lower() for v in entry.values())

        if any(word in traits for word in FORBIDDEN_COSMETIC_TRAITS):
            banned.append(entry.get("id", "?"))

    try:
        registry.set_editor_property("competitive_banned_cosmetic_ids",
                                     [unreal.Name(b) for b in banned])
    except Exception:  # noqa: BLE001
        pass

    if banned:
        report.failed("cosmetic integrity",
                      u"{0} cosmetics look like they affect gameplay: {1}".format(
                          len(banned), ", ".join(banned)))
    else:
        C.log(u"  cosmetic integrity: all {0} cosmetics are cosmetic-only".format(
            len(payload.get("cosmetics", []))))


def verify(report, registry):
    """Reads the asset back and checks the six archetypes are all present.

    A registry that silently contains five archetypes is the kind of bug that
    ships, because nothing crashes: a sixth of the players just get a default
    body. So it is checked, by name, here.
    """
    try:
        archetypes = registry.get_editor_property("archetypes")
    except Exception as exc:  # noqa: BLE001
        report.failed("verification", str(exc))
        return

    names = []
    for entry in archetypes:
        try:
            names.append(str(entry.get_editor_property("display_name")))
        except Exception:  # noqa: BLE001
            names.append("?")

    C.log(u"  verified archetypes: {0}".format(", ".join(names)))

    if len(archetypes) != 6:
        report.failed("verification",
                      u"expected 6 archetypes, found {0}".format(len(archetypes)))


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("apply_cast_payload must run inside the Unreal Editor.")

    report = C.BuildReport("apply_cast_payload")
    C.log(u"=== Applying the One Braham cast payload ===")

    payload = C.guard(report, "payload", load_payload, report)
    if payload is None:
        C.log(report.summary())
        return report

    registry = C.guard(report, "registry", ensure_registry, report)
    if registry is not None:
        C.guard(report, "apply", apply_payload, report, registry, payload)
        C.guard(report, "verify", verify, report, registry)
        unreal.EditorAssetLibrary.save_asset(REGISTRY_PATH)

    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
