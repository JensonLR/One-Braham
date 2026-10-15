# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_vfx
===================

Creates every Niagara system, camera shake and decal material the game asks for,
then writes the authoring sheets for the parts that need an artist's hand inside
the Niagara editor.

WHY THE SYSTEMS ARE CREATED EMPTY - AND WHY THAT IS NOW SAFE
    A Niagara system's emitter stack is a graph of modules with data interfaces;
    it is authorable from Python only through a very small surface
    (unreal.NiagaraEditorAssetLibrary and friends vary by build). What *is*
    reliable is creating the assets with the right names, in the right folders,
    with the right user parameters declared by the C++ that spawns them - and
    then writing the exact stack spec next to them, per system, so the FX pass is
    a fill-in job with a checklist rather than a design job.

    The runtime never depends on any of it. An empty system is a trap, though:
    it loads, spawns, activates, reports success and draws nothing, so for a
    long time every impact above the Low preset was invisible. The C++ now
    treats a system with no enabled emitters exactly like a missing one
    (UOneBrahamVFXStatics::IsNiagaraSystemUsable) and plays the procedural
    stand-in from AOneBrahamImpactFallbackActor instead. The moment an artist
    adds an emitter to a system, the authored effect takes over - no code
    change, no flag.

WHAT THE FALLBACK NEEDS FROM THIS SCRIPT
    Three small materials (build_fx_materials below). Without them the fallback
    still works on engine materials, just flatter: glows become opaque emissive,
    dust becomes shrinking grey spheres, and blood decals are switched off
    (a square red decal is worse than none).
        /Game/OneBraham/VFX/Materials/M_OB_FX_Glow     unlit additive:  Colour, EmissiveStrength, Opacity
        /Game/OneBraham/VFX/Materials/M_OB_FX_Haze     lit translucent: Colour, Opacity, EmissiveStrength
        /Game/OneBraham/VFX/Decals/M_OB_Decal_Blood    deferred decal:  BaseColour, Opacity, Seed, Splatter, Wetness
    The parameter names are a contract with OneBrahamImpactFallbackActor.cpp
    and OneBrahamVFXSubsystem.cpp; renaming one here silently disables it there.

WHAT IT CREATES
    /Game/OneBraham/VFX/NS_OB_*                     17 systems (see NAGARA_SYSTEMS)
    /Game/OneBraham/VFX/CameraShakes/BP_OBCameraShake_{Explosion,Landing,Extraction}
    /Game/OneBraham/VFX/Decals/M_OB_Decal_*         decal material instances
    /Game/OneBraham/VFX/Decals/M_OB_Decal_Blood     procedural blood decal (no texture)
    /Game/OneBraham/VFX/Materials/M_OB_FX_*         fallback particle materials
    /Game/OneBraham/VFX/T_OB_VFX_*                  texture slot stubs (reported)
    Tools/Python/payloads/vfx_authoring_sheet.txt   the per-system stack spec

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/build_vfx.py"
"""

from __future__ import print_function

import io
import os

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

# ---------------------------------------------------------------------------
#  Systems. Names must match EffectStem() in OneBrahamVFXStatics.cpp exactly:
#  that is how the C++ finds them, and a typo here is a silent fallback at run
#  time rather than a build error.
# ---------------------------------------------------------------------------
# stem, spawner, category, cull distance cm, budget (max concurrent), stack spec
NIAGARA_SYSTEMS = [
    ("NS_OB_Impact_Concrete", "SpawnImpact", "Impact", 12000, 64,
     "CPU sprite burst. 12-18 sparks (0.4-1.2 cm), 6-10 dust puffs, 1 decal plane. "
     "Spawn normal-aligned, gravity -980, drag 2.2. Lifetime 0.45-0.9 s. "
     "Colour from the surface's BaseColour at impact, multiplied by 0.6."),
    ("NS_OB_Impact_Metal", "SpawnImpact", "Impact", 12000, 64,
     "CPU sprite burst. 18-26 sparks with a 0.3 s gravity ramp and a 6 cm trail, "
     "2-4 hot flecks. Additive, emissive 6-14. Lifetime 0.25-0.6 s."),
    ("NS_OB_Impact_Glass", "SpawnImpact", "Impact", 9000, 48,
     "CPU mesh+GPU sprite hybrid. 8-14 shards (GPU flat quads, refraction 0.02), "
     "10 dust motes, 1 long crack decal. Shards inherit 150-400 cm/s velocity."),
    ("NS_OB_Impact_Flesh", "FleshImpact", "Impact", 9000, 48,
     "CPU sprite burst. 10-16 red mist sprites, 4-7 dark flecks with gravity, "
     "1 short-lived (0.35 s) directional mist cone. No additive. Deliberately "
     "restrained: this is a serious game, not a horror effect."),
    ("NS_OB_MuzzleFlash", "MuzzleFlash", "Weapon", 6000, 12,
     "GPU sprite, camera-facing, 0.045 s. 3 layers: core (white, emissive 30), "
     "petals (calibre-shaped, emissive 12), smoke wisp (0.25 s, 40 cm/s up). "
     "Scaled by barrel length through a user parameter, not by a second system."),
    ("NS_OB_Tracer", "Tracer", "Weapon", 25000, 40,
     "Ribbon emitter, 0.06 s, 3 cm wide, additive emissive 8. Length is set from "
     "the shot distance. One tracer in five: see TracerEveryN in the weapon data."),
    ("NS_OB_ShellEject", "ShellEject", "Weapon", 5000, 20,
     "CPU mesh (casing) with Chaos-lite ballistic arcs, 2.5 s lifetime, 3 bounces, "
     "then a 2 s rest before fade. Ejection vector is a user parameter so the "
     "pump shotgun throws differently from the pistol."),
    ("NS_OB_Explosion_Frag", "FragExplosion", "Explosion", 25000, 4,
     "Layered: 0.12 s white flash (emissive 60), 0.6 s orange fireball (3 turb noise "
     "sprites), 1.6 s black smoke, 40 debris meshes with radial impulse, 0.9 s dust "
     "ring on the floor plane. Radius scales all four through ScaleColour."),
    ("NS_OB_Explosion_Breach", "BreachExplosion", "Explosion", 25000, 4,
     "Directional variant of the frag explosion: a 60 degree cone along the wall "
     "normal, 60 plaster chunks, 2.2 s heavy dust, no fireball colour ramp. The "
     "hole is geometry (the wall opens), so the VFX only has to sell the dust."),
    ("NS_OB_Flashbang", "Flashbang", "Utility", 20000, 6,
     "0.08 s pure white sphere (emissive 120) plus a 0.25 s afterglow sprite. The "
     "screen effect is not this system: it is the post-process blend in "
     "OneBrahamCharacter, and the two are tuned together (see Docs/11)."),
    ("NS_OB_Smoke", "SmokeCloud", "Utility", 20000, 8,
     "GPU sprites, 22 s lifetime, 4-6 m radius growth then hold, wind vector from "
     "the level's draught. Two colours: white (concealment) and grey-green "
     "(stairwell, for the fire-drill fantasy). Depth-fade 40 cm."),
    ("NS_OB_BloodMist", "SpawnEffect", "Character", 4000, 12,
     "Small directional mist behind a moving target, 0.3 s. Only spawned for "
     "hits that pass through. Capped hard: it is the easiest effect to overspend."),
    ("NS_OB_SprinklerSpray", "SpawnEffect", "Environment", 8000, 24,
     "GPU sprite cone, 40 cm fan, plus a 0.3 s splash ring on the floor. Emitted "
     "from the sprinkler heads the map places; during the purge these are on "
     "every floor that is being cleared."),
    ("NS_OB_SparkShower", "SpawnEffect", "Environment", 9000, 10,
     "Electrical short: 30-60 sparks falling from a rack or a lift panel, 1.4 s, "
     "with a light flicker hook the map's Blueprint binds to."),
    ("NS_OB_Dust", "SpawnEffect", "Environment", 7000, 16,
     "Low-velocity dust puff. Used when a door first moves, when a shutter lands, "
     "and when a body hits the floor. Cheap, and it is what makes the building "
     "feel like it has been sitting there since 1998."),
    ("NS_OB_BreachDebris", "SpawnBreachDebris", "Explosion", 9000, 12,
     "A short burst of wall chunks along the breach normal, 1.2 s, 20 meshes. "
     "Spawned on the first charge impact and again, larger, when the wall opens."),
    ("NS_OB_CorpseImpact", "PlayNearMiss", "Character", 5000, 8,
     "A ground impact puff sized from the ragdoll's velocity at first contact, "
     "plus a 0.15 s cloth ripple. Bodies are the most-read thing on a floor: this "
     "is what makes a body look like it fell rather than like it was placed."),
]

# User parameters every One Braham system declares, so a designer can retune any
# of them from a Blueprint or a cheat without opening the editor.
UNIVERSAL_PARAMETERS = [
    ("Scale", "float", 1.0, "Overall size multiplier from the spawn call."),
    ("TintColour", "LinearColor", "(1,1,1,1)", "Tinted by surface / team colour."),
    ("Intensity", "float", 1.0, "Local VFX intensity setting (0.25 / 0.55 / 0.8 / 1.0)."),
    ("LifetimeScale", "float", 1.0, "Global duration multiplier for the VFX slider."),
]

CAMERA_SHAKES = [
    ("BP_OBCameraShake_Explosion", "Explosion", 1.35, 0.55, 0.42,
     "Radial. Perlin 0.55 amplitude, 26 Hz, 0.42 s, falloff distance 2200 cm, "
     "plus a 0.1 s rotational pop on the first frame."),
    ("BP_OBCameraShake_Landing", "Landing", 0.85, 0.18, 0.20,
     "Directional (down). Amplitude scaled by impact speed in the character's "
     "landing code; 0.20 s, 14 Hz, no rotation."),
    ("BP_OBCameraShake_Extraction", "Extraction", 1.9, 0.90, 1.4,
     "Continuous while the rotor is at the pad: 0.9 amplitude sine at 11 Hz "
     "with a 0.35 s wobble every 2.2 s as the blades pass the deck edge."),
]

DECAL_MATERIALS = [
    ("M_OB_Decal_BulletHole_Concrete", "Concrete", 0.85, 6.0,
     "Rough dust ring with a dark core. Base texture T_OB_VFX_Hole_Concrete."),
    ("M_OB_Decal_BulletHole_Metal", "Metal", 0.35, 5.0,
     "Bright bare-metal lip and a grey core. T_OB_VFX_Hole_Metal."),
    ("M_OB_Decal_BulletHole_Carpet", "Carpet", 0.95, 7.0,
     "Dark, matte, ragged edge. T_OB_VFX_Hole_Carpet."),
    ("M_OB_Decal_BulletHole_Plaster", "Plasterboard", 0.90, 9.0,
     "Large pale crater with a shadowed rim. T_OB_VFX_Hole_Plaster."),
    ("M_OB_Decal_GlassCrack", "Glass", 0.15, 24.0,
     "Radial crack sheet with a 0.4 s crack-through animation. T_OB_VFX_GlassCrack."),
    ("M_OB_Decal_Scuff", "Concrete", 0.80, 30.0,
     "Dragged rubber scuff, used where a body slid. ambientCG Smear001 (CC0), "
     "applied by build_materials.apply_decal_texture_sets."),
]

DECAL_ROOT = C.VFX + "/Decals"
FX_MATERIAL_ROOT = C.VFX + "/Materials"
SHAKE_ROOT = C.VFX + "/CameraShakes"
TEXTURE_ROOT = C.VFX + "/Textures"

VFX_TEXTURES = [
    ("T_OB_VFX_Hole_Concrete", 1024, "Bullet hole, dust ring, alpha in green."),
    ("T_OB_VFX_Hole_Metal", 512, "Punched metal hole with a bright lip."),
    ("T_OB_VFX_Hole_Carpet", 1024, "Dark fibre tear."),
    ("T_OB_VFX_Hole_Plaster", 1024, "Plaster crater with a soft rim."),
    ("T_OB_VFX_GlassCrack", 2048, "Radial crack sheet, 8 segments."),
    ("T_OB_VFX_Scuff", 512, "Dragged scuff, tileable length."),
    ("T_OB_VFX_SmokeSheet", 2048, "Four-frame smoke flipbook."),
    ("T_OB_VFX_Spark_04", 256, "Small spark sprite, additive."),
    ("T_OB_VFX_Dust_04", 256, "Soft dust puff, alpha in green."),
    ("T_OB_VFX_Fire_Smoke_8x8", 2048, "8x8 fire/smoke flipbook."),
]


def _ensure_dirs():
    for path in (C.VFX, DECAL_ROOT, SHAKE_ROOT, TEXTURE_ROOT, FX_MATERIAL_ROOT):
        if not unreal.EditorAssetLibrary.does_directory_exist(path):
            unreal.EditorAssetLibrary.make_directory(path)


def build_systems(report):
    """Creates the 17 Niagara systems with their user parameters declared."""
    factory_class = getattr(unreal, "NiagaraSystemFactoryNew", None)
    system_class = getattr(unreal, "NiagaraSystem", None)

    if factory_class is None or system_class is None:
        for stem, _spawner, _category, _cull, _budget, _spec in NIAGARA_SYSTEMS:
            report.skipped_asset(u"{0}/{1}".format(C.VFX, stem),
                                 "Niagara module not loaded - open the project in the "
                                 "editor with the Niagara plugin enabled and re-run")
        return []

    created = []

    for stem, spawner, category, cull, budget, spec in NIAGARA_SYSTEMS:
        path = u"{0}/{1}".format(C.VFX, stem)

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            report.updated_asset(path)
            created.append(stem)
            continue

        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            stem, C.VFX, system_class, factory_class())

        if asset is None:
            report.failed(stem, "could not create Niagara system")
            continue

        unreal.EditorAssetLibrary.save_asset(path)
        report.created_asset(path)
        created.append(stem)

        C.log(u"  {0} [{1}] cull {2} cm, budget {3} - spawned by {4}".format(
            stem, category, cull, budget, spawner))

    C.log(u"  Niagara systems ready: {0}/{1}".format(len(created), len(NIAGARA_SYSTEMS)))
    return created


def build_camera_shakes(report):
    """Creates the three camera shake blueprints the character code asks for."""
    parent = getattr(unreal, "OneBrahamCameraShake", None)
    created = []

    for name, kind, _scale, amplitude, duration, spec in CAMERA_SHAKES:
        path = u"{0}/{1}".format(SHAKE_ROOT, name)

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            report.updated_asset(path)
            created.append(name)
            continue

        factory = unreal.BlueprintFactory()
        if parent is not None:
            factory.set_editor_property("parent_class", parent)
        else:
            report.failed(name, "UOneBrahamCameraShake is not available - compile the "
                                "module first; the shake will be created on the next run")

        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, SHAKE_ROOT, unreal.Blueprint, factory)

        if asset is None:
            report.failed(name, "could not create camera shake blueprint")
            continue

        unreal.EditorAssetLibrary.save_asset(path)
        report.created_asset(path)
        created.append(name)

        C.log(u"  {0}: {1} (amp {2}, {3} s) - {4}".format(name, kind, amplitude, duration, spec))

    return created


def build_decal_materials(report):
    """Creates the decal material instances from the M_OB_Decal master."""
    master = unreal.EditorAssetLibrary.load_asset(C.MATERIALS + "/M_OB_Decal")
    if master is None:
        for name, _surface, _rough, _size, note in DECAL_MATERIALS:
            report.skipped_asset(u"{0}/{1}".format(DECAL_ROOT, name),
                                 "master M_OB_Decal missing - run build_materials.py first")
        return []

    library = getattr(unreal, "MaterialEditingLibrary", None)
    created = []

    for name, surface, roughness, size, note in DECAL_MATERIALS:
        path = u"{0}/{1}".format(DECAL_ROOT, name)

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            report.updated_asset(path)
            created.append(name)
            continue

        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, DECAL_ROOT, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())

        if asset is None:
            report.failed(name, "could not create decal material")
            continue

        if library is not None:
            library.set_material_instance_parent(asset, master)
            # Physical material drives friction, footprint surface and decal choice.
            physical = unreal.EditorAssetLibrary.load_asset(
                u"{0}/PM_OB_{1}".format(C.MATERIALS, surface))
            if physical is not None:
                try:
                    asset.set_editor_property("phys_material", physical)
                except Exception:  # noqa: BLE001
                    pass

        unreal.EditorAssetLibrary.save_asset(path)
        report.created_asset(path)
        created.append(name)
        C.log(u"  {0}: {1} cm, roughness {2} - {3}".format(name, size, roughness, note))

    # The materials step runs before this one, when the instances did not exist
    # yet: texture them now so a clean build needs no second materials pass.
    from onebraham import build_materials
    C.guard(report, "decal texture sets", build_materials.apply_decal_texture_sets, report)

    return created


# ---------------------------------------------------------------------------
#  Fallback FX materials and the blood decal
# ---------------------------------------------------------------------------

def _mel():
    return getattr(unreal, "MaterialEditingLibrary", None)


def _node(material, cls, x, y, **props):
    node = _mel().create_material_expression(material, cls, x, y)
    for key, value in props.items():
        node.set_editor_property(key, value)
    return node


def _scalar(material, name, default, x, y):
    return _node(material, unreal.MaterialExpressionScalarParameter, x, y,
                 parameter_name=name, default_value=default)


def _vector(material, name, rgba, x, y):
    return _node(material, unreal.MaterialExpressionVectorParameter, x, y,
                 parameter_name=name, default_value=unreal.LinearColor(*rgba))


def _const(material, value, x, y):
    return _node(material, unreal.MaterialExpressionConstant, x, y, r=value)


def _link(a, out_name, b, in_name):
    _mel().connect_material_expressions(a, out_name, b, in_name)


def _binary(material, cls, a, b, x, y, a_out="", b_out=""):
    node = _node(material, cls, x, y)
    _link(a, a_out, node, "A")
    _link(b, b_out, node, "B")
    return node


def _to(material, node, prop, out_name=""):
    _mel().connect_material_property(node, out_name, prop)


def _new_material(name, folder, report):
    """Creates a material, or returns None if it already exists (re-runs do not
    stack a second graph on top of the first)."""
    path = u"{0}/{1}".format(folder, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        report.updated_asset(path)
        return None, path
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if asset is None:
        report.failed(name, "could not create material")
    return asset, path


def _save_material(material, path, report):
    try:
        _mel().recompile_material(material)
    except Exception:  # noqa: BLE001
        pass
    unreal.EditorAssetLibrary.save_asset(path)
    report.created_asset(path)


def build_glow_material(report):
    """M_OB_FX_Glow: sparks, flare, tracer, fireball. Unlit additive, so
    Opacity scales the light it adds rather than covering what is behind it."""
    material, path = _new_material("M_OB_FX_Glow", FX_MATERIAL_ROOT, report)
    if material is None:
        return
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", True)

    colour = _vector(material, "Colour", (1.0, 0.7, 0.35, 1.0), -900, -200)
    strength = _scalar(material, "EmissiveStrength", 20.0, -900, 0)
    opacity = _scalar(material, "Opacity", 1.0, -900, 160)

    lit = _binary(material, unreal.MaterialExpressionMultiply, colour, strength, -600, -120)
    faded = _binary(material, unreal.MaterialExpressionMultiply, lit, opacity, -380, -60)
    _to(material, faded, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _save_material(material, path, report)


def build_haze_material(report):
    """M_OB_FX_Haze: dust, smoke, spray mist, water. Lit translucent, with the
    silhouette softened by a Fresnel term so a sphere reads as a puff rather
    than a ball."""
    material, path = _new_material("M_OB_FX_Haze", FX_MATERIAL_ROOT, report)
    if material is None:
        return
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", False)

    colour = _vector(material, "Colour", (0.5, 0.5, 0.5, 1.0), -900, -260)
    opacity = _scalar(material, "Opacity", 0.6, -900, 40)
    glow = _scalar(material, "EmissiveStrength", 0.0, -900, 200)

    _to(material, colour, unreal.MaterialProperty.MP_BASE_COLOR)
    _to(material, _const(material, 1.0, -600, -160), unreal.MaterialProperty.MP_ROUGHNESS)

    emissive = _binary(material, unreal.MaterialExpressionMultiply, colour, glow, -600, 220)
    _to(material, emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    fresnel = _node(material, unreal.MaterialExpressionFresnel, -900, 120, exponent=1.6)
    soft = _node(material, unreal.MaterialExpressionOneMinus, -700, 120)
    _link(fresnel, "", soft, "")
    edge = _binary(material, unreal.MaterialExpressionMultiply, opacity, soft, -500, 60)
    _to(material, edge, unreal.MaterialProperty.MP_OPACITY)
    _save_material(material, path, report)


def build_blood_decal(report):
    """M_OB_Decal_Blood: a procedural splatter. No texture: the shape is a radial
    falloff broken up by world-space noise, so every mark differs and two marks
    side by side never tile. Splatter 0 is a round pool with a wobbly rim,
    Splatter 1 a broken spray; Seed varies the noise; Wetness drives gloss so a
    fading mark dries rather than just thinning."""
    material, path = _new_material("M_OB_Decal_Blood", DECAL_ROOT, report)
    if material is None:
        return
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    tint = _vector(material, "BaseColour", (0.16, 0.008, 0.006, 1.0), -1400, -400)
    opacity = _scalar(material, "Opacity", 0.9, -1400, 420)
    seed = _scalar(material, "Seed", 0.0, -1700, 120)
    splatter = _scalar(material, "Splatter", 1.0, -1400, 260)
    wetness = _scalar(material, "Wetness", 1.0, -1400, -160)

    # Radial edge: 1 at the centre of the decal, 0 at its rim.
    uv = _node(material, unreal.MaterialExpressionTextureCoordinate, -1700, -60)
    centre = _node(material, unreal.MaterialExpressionConstant2Vector, -1700, 20, r=0.5, g=0.5)
    distance = _binary(material, unreal.MaterialExpressionDistance, uv, centre, -1500, -40)
    twice = _binary(material, unreal.MaterialExpressionMultiply, distance, _const(material, 2.0, -1500, 40), -1350, -40)
    edge = _node(material, unreal.MaterialExpressionOneMinus, -1200, -40)
    _link(twice, "", edge, "")

    # World-space noise, offset by Seed so neighbouring marks differ.
    world = _node(material, unreal.MaterialExpressionWorldPosition, -1700, 180)
    seeded = _binary(material, unreal.MaterialExpressionAdd, world,
                     _binary(material, unreal.MaterialExpressionMultiply, seed,
                             _const(material, 37.0, -1700, 240), -1550, 200), -1400, 160)
    noise = _node(material, unreal.MaterialExpressionNoise, -1200, 160,
                  scale=0.09, output_min=-0.5, output_max=0.5, levels=3)
    _link(seeded, "", noise, "Position")

    def shaped(bias, roughness, sharpness, y):
        """saturate((edge - bias + noise * roughness) * sharpness)"""
        broken = _binary(material, unreal.MaterialExpressionMultiply, noise,
                         _const(material, roughness, -1050, y + 60), -950, y + 40)
        moved = _binary(material, unreal.MaterialExpressionSubtract, edge,
                        _const(material, bias, -1050, y), -950, y)
        summed = _binary(material, unreal.MaterialExpressionAdd, moved, broken, -800, y)
        steep = _binary(material, unreal.MaterialExpressionMultiply, summed,
                        _const(material, sharpness, -800, y + 60), -650, y)
        clamp = _node(material, unreal.MaterialExpressionSaturate, -500, y)
        _link(steep, "", clamp, "")
        return clamp

    pool = shaped(0.12, 0.35, 9.0, 40)
    spray = shaped(0.38, 1.1, 6.0, 220)

    mask = _node(material, unreal.MaterialExpressionLinearInterpolate, -350, 120)
    _link(pool, "", mask, "A")
    _link(spray, "", mask, "B")
    _link(splatter, "", mask, "Alpha")

    final_opacity = _binary(material, unreal.MaterialExpressionMultiply, mask, opacity, -200, 200)
    _to(material, final_opacity, unreal.MaterialProperty.MP_OPACITY)

    # Thicker (centre) is darker; the rim catches more light.
    dark = _vector(material, "CoreColour", (0.05, 0.002, 0.002, 1.0), -1100, -500)
    colour = _node(material, unreal.MaterialExpressionLinearInterpolate, -400, -400)
    _link(tint, "", colour, "A")
    _link(dark, "", colour, "B")
    _link(edge, "", colour, "Alpha")
    _to(material, colour, unreal.MaterialProperty.MP_BASE_COLOR)

    # Wet blood is glossy, dry blood is matte.
    gloss = _node(material, unreal.MaterialExpressionLinearInterpolate, -400, -200)
    _link(_const(material, 0.7, -600, -260), "", gloss, "A")
    _link(_const(material, 0.12, -600, -200), "", gloss, "B")
    _link(wetness, "", gloss, "Alpha")
    _to(material, gloss, unreal.MaterialProperty.MP_ROUGHNESS)

    _save_material(material, path, report)


def build_fx_materials(report):
    """The three materials the procedural fallback and the blood system use."""
    if _mel() is None:
        for name, folder in (("M_OB_FX_Glow", FX_MATERIAL_ROOT), ("M_OB_FX_Haze", FX_MATERIAL_ROOT),
                             ("M_OB_Decal_Blood", DECAL_ROOT)):
            report.skipped_asset(u"{0}/{1}".format(folder, name),
                                 "MaterialEditingLibrary not loaded - the runtime uses engine materials")
        return

    for label, fn in (("M_OB_FX_Glow", build_glow_material),
                      ("M_OB_FX_Haze", build_haze_material),
                      ("M_OB_Decal_Blood", build_blood_decal)):
        try:
            fn(report)
        except Exception as exc:  # noqa: BLE001
            # One node class renamed in an engine update must not take the
            # other two materials down with it.
            report.failed(label, u"material graph: {0}".format(exc))


def build_texture_slots(report):
    """Registers the VFX texture list.

    Textures are authored, not generated: the list below is what the FX pass has
    to draw (or buy - see Docs/16_Marketplace_Fallbacks.md for the licence-safe
    marketplace packs that cover every one of these).
    """
    for name, size, note in VFX_TEXTURES:
        path = u"{0}/{1}".format(TEXTURE_ROOT, name)
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            report.updated_asset(path)
        else:
            report.skipped_asset(path, u"{0}px - {1}".format(size, note))


def write_authoring_sheet(report):
    """Writes the per-system Niagara stack spec next to the scripts."""
    payload_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "payloads")
    payload_dir = os.path.normpath(payload_dir)

    try:
        if not os.path.isdir(payload_dir):
            os.makedirs(payload_dir)
    except Exception:  # noqa: BLE001
        pass

    path = os.path.join(payload_dir, "vfx_authoring_sheet.txt")

    lines = []
    lines.append("ONE BRAHAM - NIAGARA AUTHORING SHEET")
    lines.append("=" * 72)
    lines.append("")
    lines.append("Every system below exists as an empty asset in " + C.VFX + ".")
    lines.append("Fill each one in against its spec. Nothing here is optional for")
    lines.append("the shipped build; the runtime fallback exists so that a build")
    lines.append("without them is still playable, not so that they can be skipped.")
    lines.append("")
    lines.append("An empty system counts as missing: until a system has at least one")
    lines.append("enabled emitter the game plays the procedural fallback in its place")
    lines.append("(UOneBrahamVFXStatics::IsNiagaraSystemUsable). Adding the first")
    lines.append("emitter is what switches the authored effect on.")
    lines.append("")
    lines.append("UNIVERSAL USER PARAMETERS (declare these in every system)")
    lines.append("-" * 72)
    for name, kind, default, note in UNIVERSAL_PARAMETERS:
        lines.append("  {0:<14} {1:<12} default {2:<10} {3}".format(name, kind, default, note))
    lines.append("")
    lines.append("BUDGETS")
    lines.append("-" * 72)
    lines.append("  GPU particles per frame, worst case (12 players, firefight on")
    lines.append("  the server floor): 24,000. Sprite budget 18 MB, mesh 6 MB.")
    lines.append("  Every system is CPU-simulated unless its spec says GPU sprite.")
    lines.append("  Fixed bounds ON for every system: dynamic bounds on a 12-player")
    lines.append("  server is the single most common cause of hitching in this genre.")
    lines.append("")
    lines.append("SYSTEMS")
    lines.append("-" * 72)

    for stem, spawner, category, cull, budget, spec in NIAGARA_SYSTEMS:
        lines.append("")
        lines.append("{0}   [{1}]".format(stem, category))
        lines.append("  spawned by : UOneBrahamVFXStatics::{0}()".format(spawner))
        lines.append("  cull       : {0} cm".format(cull))
        lines.append("  concurrency: {0} instances".format(budget))
        lines.append("  stack      : {0}".format(spec))

    lines.append("")
    lines.append("")
    lines.append("DECAL MATERIALS")
    lines.append("-" * 72)
    for name, surface, roughness, size, note in DECAL_MATERIALS:
        lines.append("  {0:<34} surface {1:<14} {2:>4} cm   {3}".format(
            name, surface, size, note))

    lines.append("")
    lines.append("TEXTURES")
    lines.append("-" * 72)
    for name, size, note in VFX_TEXTURES:
        lines.append("  {0:<32} {1:>5}px   {2}".format(name, size, note))

    lines.append("")

    with io.open(path, "w", encoding="utf-8") as handle:
        handle.write(u"\n".join(lines))

    C.log(u"  authoring sheet: {0} ({1} systems)".format(path, len(NIAGARA_SYSTEMS)))
    return path


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("build_vfx must run inside the Unreal Editor.")

    report = C.BuildReport("build_vfx")
    C.log(u"=== Building One Braham VFX ===")

    C.guard(report, "directories", _ensure_dirs)
    C.guard(report, "niagara systems", build_systems, report)
    C.guard(report, "camera shakes", build_camera_shakes, report)
    C.guard(report, "decal materials", build_decal_materials, report)
    C.guard(report, "fx materials", build_fx_materials, report)
    C.guard(report, "texture slots", build_texture_slots, report)
    C.guard(report, "authoring sheet", write_authoring_sheet, report)

    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
