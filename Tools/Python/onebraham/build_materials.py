# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_materials
=========================

Authors the project's materials *as node graphs*, from Python, in the editor.

This is the part everybody assumes has to be done by hand. It does not:
`unreal.MaterialEditingLibrary.create_material_expression` and
`connect_material_property` build real node graphs, so the master materials, the
14 surface instances, the glass variants and the brand colour set are all
reproducible from this file. Docs/10_Environment_Art.md carries the same graphs
as wiring diagrams for anyone who would rather see them than run them.

WHAT IT CREATES
    /Game/OneBraham/Materials/PM_OB_<Surface>      one physical material per surface (15)
    /Game/OneBraham/Materials/M_OB_Surface         master, lit opaque
    /Game/OneBraham/Materials/M_OB_Glass           master, translucent
    /Game/OneBraham/Materials/M_OB_Emissive        master, unlit signage
    /Game/OneBraham/Materials/M_OB_Screen          master, unlit with a screen texture
    /Game/OneBraham/Materials/M_OB_Decal           master, deferred decal
    /Game/OneBraham/Materials/M_OB_SurfaceTextured master, world-aligned CC0 texture sets
    /Game/OneBraham/Materials/M_OB_GlassGrimed     master, glass with a world-aligned grime mask
    /Game/OneBraham/Materials/M_OB_DecalTextured   master, deferred decal from a CC0 set
    /Game/OneBraham/Materials/MPC_OneBraham_Global the global parameter collection the C++ drives
    /Game/OneBraham/Materials/M_OB_PP_Accessibility post-process: flash-bang white-out, colour-blind modes
    /Game/OneBraham/Materials/Instances/MI_<Surface>       14 surface instances
    /Game/OneBraham/Materials/Instances/MI_OB_Brand_*      brand + alarm instances

THE PARAMETER CONTRACT
    Everything downstream - the map builder, the prop builder, the VFX builder -
    sets the same four parameters. Do not rename them:
        BaseColour       (vector)   albedo tint
        Roughness        (scalar)   0 .. 1
        Metallic         (scalar)   0 .. 1
        SurfaceTypeIndex (scalar)   index into EOneBrahamFootstepSurface
    Glass adds Opacity / Refraction; Emissive adds EmissiveColour / EmissiveStrength;
    the screen master adds ScreenTexture / ScreenTint / ScreenBrightness.

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/build_materials.py"
"""

from __future__ import print_function

from onebraham import common as C
from onebraham.build_environment import SURFACES, SURFACE_ORDER

if C.IN_EDITOR:
    import unreal

# Surface type enums in the engine's project settings (Config/DefaultEngine.ini).
# The names must match PHYSICAL_SURFACE names there; "Default" is index 0.
PHYSICAL_SURFACE_NAMES = [
    "Default", "Marble", "Carpet", "Concrete", "Metal", "Glass", "Wood", "Puddle",
    "Grating", "Fabric", "ServerRack", "Plasterboard", "Lino", "Decking", "Flesh",
    "BallisticGlass",
]

# Surface name -> the master that draws it. Glass-ish things get the translucent
# master; everything else is the lit opaque one.
GLASS_SURFACES = ("Glass", "BallisticGlass", "Puddle")

# Real-world albedo for the building's surfaces, from the reference photographs
# (Docs/10 §7). The UI palette's seven colours are brand colours for the HUD;
# taking surface colours from them made the whole building navy and cyan -
# navy carpet, navy timber, cyan ductwork. Surfaces not listed (the glass
# family) keep their palette tint, which the glass master treats as a tint.
SURFACE_ALBEDO = {
    "Marble":       (0.74, 0.73, 0.70),   # pale terrazzo, reception
    "Carpet":       (0.11, 0.12, 0.14),   # charcoal office carpet tile
    "Concrete":     (0.50, 0.49, 0.47),   # exposed soffits and cores
    "Metal":        (0.45, 0.46, 0.48),   # galvanised ductwork, steel
    "Wood":         (0.52, 0.42, 0.30),   # pale oak, as photographed
    "Grating":      (0.30, 0.31, 0.32),
    "Fabric":       (0.22, 0.24, 0.28),   # grey-blue upholstery
    "ServerRack":   (0.035, 0.035, 0.04),
    "Plasterboard": (0.82, 0.82, 0.80),   # white partitions
    "Lino":         (0.56, 0.56, 0.54),
    "Decking":      (0.38, 0.35, 0.31),
}

# Which surfaces are metal enough to matter for reflections and impact sparks.
METAL_SURFACES = ("Metal", "Grating", "Decking", "ServerRack")

# Brand instances: name, master, base colour key, roughness, metallic, extras.
BRAND_INSTANCES = [
    ("MI_OB_Brand_Primary",      "Surface",  "Primary",      0.30, 0.20, {}),
    ("MI_OB_Brand_Navy",         "Surface",  "Navy",         0.55, 0.00, {}),
    ("MI_OB_Brand_LightGrey",    "Surface",  "LightGrey",    0.70, 0.00, {}),
    ("MI_OB_Brand_White",        "Surface",  "White",        0.85, 0.00, {}),
    ("MI_OB_Warning_Yellow",     "Surface",  "SafetyYellow", 0.40, 0.00, {}),
    ("MI_OB_Emergency_Red",      "Surface",  "EmergencyRed", 0.35, 0.00, {}),
    ("MI_OB_Signage_SignalBlue", "Emissive", "SignalBlue",   0.20, 0.00,
     {"EmissiveColour": "SignalBlue", "EmissiveStrength": 3.2}),
    ("MI_OB_Signage_White",      "Emissive", "White",        0.20, 0.00,
     {"EmissiveColour": "White", "EmissiveStrength": 2.4}),
    ("MI_OB_Signage_Exit",       "Emissive", "SafetyYellow", 0.20, 0.00,
     {"EmissiveColour": "SafetyYellow", "EmissiveStrength": 4.0}),
    ("MI_OB_Signage_Alarm",      "Emissive", "EmergencyRed", 0.20, 0.00,
     {"EmissiveColour": "EmergencyRed", "EmissiveStrength": 5.0}),
    ("MI_OB_Glass_Clear",        "Glass",    "LightGrey",    0.03, 0.00, {"Opacity": 0.12}),
    ("MI_OB_Glass_Office",       "Glass",    "LightGrey",    0.06, 0.00, {"Opacity": 0.30}),
    ("MI_OB_Glass_Ballistic",    "Glass",    "SignalBlue",   0.08, 0.00, {"Opacity": 0.55}),
    ("MI_OB_Puddle_Dirty",       "Glass",    "SignalBlue",   0.04, 0.30, {"Opacity": 0.70}),
]


# ---------------------------------------------------------------------------
#  Helpers
# ---------------------------------------------------------------------------

def _mel():
    """The material editing library, or None if the module is not loaded."""
    return getattr(unreal, "MaterialEditingLibrary", None)


def _ensure_dirs():
    for path in (C.MATERIALS, C.MATERIALS_INSTANCES):
        if not unreal.EditorAssetLibrary.does_directory_exist(path):
            unreal.EditorAssetLibrary.make_directory(path)


def _create_asset(name, folder, asset_class, factory):
    path = u"{0}/{1}".format(folder, name)
    existing = unreal.EditorAssetLibrary.load_asset(path)
    if existing is not None:
        # A master is re-authored from scratch: the builders only ever add
        # nodes, so reusing the old graph stacked a full copy of it on every
        # build (M_OB_Surface had reached 324 expressions, four graphs deep).
        # One delete_all pass removes only every other node in this engine
        # (it walks the array it deletes from), so it repeats until empty.
        if isinstance(existing, unreal.Material) and _mel():
            for _ in range(16):
                if _mel().get_num_material_expressions(existing) == 0:
                    break
                _mel().delete_all_material_expressions(existing)
        return existing, False
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, asset_class, factory)
    return asset, True


def _expr(material, expression_class, x, y):
    library = _mel()
    return library.create_material_expression(material, expression_class, x, y)


def _vector_param(material, name, default, x, y):
    node = _expr(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(default[0], default[1],
                                                                default[2], default[3]))
    return node


def _scalar_param(material, name, default, x, y, vmin=0.0, vmax=1.0):
    node = _expr(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    node.set_editor_property("slider_min", vmin)
    node.set_editor_property("slider_max", vmax)
    return node


def _constant3(material, rgb, x, y):
    node = _expr(material, unreal.MaterialExpressionConstant3Vector, x, y)
    node.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return node


def _scalar_constant(material, value, x, y):
    node = _expr(material, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property("r", value)
    return node


def _connect(material, a, out_name, b, in_name):
    _mel().connect_material_expressions(a, out_name, b, in_name)


def _connect_property(material, node, out_name, prop):
    _mel().connect_material_property(node, out_name, prop)


def _set_shading(material, shading_model_name):
    try:
        material.set_editor_property(
            "shading_model", getattr(unreal.MaterialShadingModel, shading_model_name))
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  shading model {0} not settable: {1}".format(shading_model_name, exc))


def _set_blend(material, blend_name):
    try:
        material.set_editor_property(
            "blend_mode", getattr(unreal.BlendMode, blend_name))
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  blend mode {0} not settable: {1}".format(blend_name, exc))


def _tint(colour_key):
    return C.PALETTE.get(colour_key, C.PALETTE["LightGrey"])


# ---------------------------------------------------------------------------
#  Physical materials
# ---------------------------------------------------------------------------

def build_physical_materials(report):
    """One physical material per surface type, so footsteps, bullet impacts and
    decals all read the right surface for free."""
    physicals = {}

    for index, name in enumerate(PHYSICAL_SURFACE_NAMES):
        if name == "Default":
            continue
        if name not in SURFACE_ORDER:
            continue

        asset, created = _create_asset(
            "PM_OB_" + name, C.MATERIALS, unreal.PhysicalMaterial,
            unreal.PhysicalMaterialFactoryNew())

        if asset is None:
            report.failed("PM_OB_" + name, "could not create physical material")
            continue

        surface_type = index  # EPhysicalSurface ordering mirrors the ini
        try:
            asset.set_editor_property("surface_type", unreal.PhysicalSurface(surface_type))
        except Exception:  # noqa: BLE001
            # Setting surface_type by enum is not exposed in every build; the
            # ini already declares the names in this order, so the fallback is a
            # no-op rather than a wrong value.
            pass

        # How the surface behaves under movement. These are what make concrete
        # feel like concrete and lino feel like lino when someone slides into a
        # desk at speed.
        friction = {"Puddle": 0.10, "Grating": 0.65, "Marble": 0.72, "Lino": 0.80}.get(name, 0.90)
        try:
            asset.set_editor_property("friction", friction)
            asset.set_editor_property("restitution", 0.05 if name not in ("Fabric",) else 0.0)
        except Exception:  # noqa: BLE001
            pass

        if created:
            unreal.EditorAssetLibrary.save_asset(u"{0}/{1}".format(C.MATERIALS, asset.get_name()))
            report.created_asset(u"{0}/PM_OB_{1}".format(C.MATERIALS, name))
        else:
            report.updated_asset(u"{0}/PM_OB_{1}".format(C.MATERIALS, name))

        physicals[name] = asset

    C.log(u"  physical materials: {0}".format(len(physicals)))
    return physicals


# ---------------------------------------------------------------------------
#  Master materials
# ---------------------------------------------------------------------------

def build_master_surface(report):
    """M_OB_Surface: the lit opaque master every solid surface instance is made from.

    Graph:
        BaseColour        -> BaseColor
        Roughness         -> Roughness
        Metallic          -> Metallic
        Specular          -> Specular
        AmbientOcclusion  -> AmbientOcclusion
        EmissiveColour * EmissiveStrength -> EmissiveColor
        UVTiling -> TexCoord multiply -> NormalMap sample
            lerp(flat, sampled, DetailNormalStrength) -> Normal
    """
    material, created = _create_asset("M_OB_Surface", C.MATERIALS, unreal.Material,
                                      unreal.MaterialFactoryNew())
    if material is None:
        report.failed("M_OB_Surface", "could not create material")
        return None

    if not _mel():
        report.failed("M_OB_Surface", "MaterialEditingLibrary unavailable - cannot "
                                      "author the node graph in this build")
        return material

    _set_shading(material, "MSM_DEFAULT_LIT")
    _set_blend(material, "BLEND_OPAQUE")

    base = _vector_param(material, "BaseColour", (0.5, 0.5, 0.5, 1.0), -700, -420)
    rough = _scalar_param(material, "Roughness", 0.5, -700, -280)

    # Wear: two octaves of world-space noise, a fine grain (scuffs, fibres, the
    # speckle in terrazzo) and a broad one (traffic paths, patchy concrete),
    # nudging colour and roughness. Flat single-colour surfaces are what make a
    # blockout read as a blockout; this is the cheapest thing that stops it.
    world = _expr(material, unreal.MaterialExpressionWorldPosition, -1700, -700)
    fine_scale = _scalar_param(material, "GrainScaleCm", 18.0, -1700, -560, 1.0, 400.0)
    broad_scale = _scalar_param(material, "WearScaleCm", 420.0, -1700, -460, 10.0, 4000.0)
    fine_pos = _expr(material, unreal.MaterialExpressionDivide, -1480, -640)
    _connect(material, world, "", fine_pos, "A")
    _connect(material, fine_scale, "", fine_pos, "B")
    broad_pos = _expr(material, unreal.MaterialExpressionDivide, -1480, -500)
    _connect(material, world, "", broad_pos, "A")
    _connect(material, broad_scale, "", broad_pos, "B")
    fine = _expr(material, unreal.MaterialExpressionNoise, -1280, -660)
    broad = _expr(material, unreal.MaterialExpressionNoise, -1280, -480)
    for node in (fine, broad):
        for prop, value in (("scale", 1.0), ("levels", 3), ("output_min", 0.0), ("output_max", 1.0)):
            try:
                node.set_editor_property(prop, value)
            except Exception:  # noqa: BLE001 - defaults are acceptable
                pass
    _connect(material, fine_pos, "", fine, "Position")
    _connect(material, broad_pos, "", broad, "Position")
    mix = _expr(material, unreal.MaterialExpressionLinearInterpolate, -1060, -580)
    _connect(material, fine, "", mix, "A")
    _connect(material, broad, "", mix, "B")
    _connect(material, _scalar_constant(material, 0.55, -1280, -380), "", mix, "Alpha")

    # Colour: base x (1 - amount .. 1 + amount) across the noise.
    amount = _scalar_param(material, "ColourVariation", 0.12, -1060, -420, 0.0, 0.6)
    centred = _expr(material, unreal.MaterialExpressionSubtract, -880, -580)
    _connect(material, mix, "", centred, "A")
    _connect(material, _scalar_constant(material, 0.5, -1060, -320), "", centred, "B")
    spread = _expr(material, unreal.MaterialExpressionMultiply, -720, -560)
    _connect(material, centred, "", spread, "A")
    _connect(material, amount, "", spread, "B")
    gain = _expr(material, unreal.MaterialExpressionAdd, -560, -560)
    _connect(material, spread, "", gain, "A")
    _connect(material, _scalar_constant(material, 1.0, -720, -440), "", gain, "B")
    varied = _expr(material, unreal.MaterialExpressionMultiply, -400, -460)
    _connect(material, base, "", varied, "A")
    _connect(material, gain, "", varied, "B")
    _connect_property(material, varied, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Roughness: worn paths are smoother.
    rough_amount = _scalar_param(material, "RoughnessVariation", 0.18, -1060, -220, 0.0, 0.6)
    rough_spread = _expr(material, unreal.MaterialExpressionMultiply, -720, -260)
    _connect(material, centred, "", rough_spread, "A")
    _connect(material, rough_amount, "", rough_spread, "B")
    rough_out = _expr(material, unreal.MaterialExpressionAdd, -560, -280)
    _connect(material, rough, "", rough_out, "A")
    _connect(material, rough_spread, "", rough_out, "B")
    rough_clamped = _expr(material, unreal.MaterialExpressionSaturate, -400, -280)
    _connect(material, rough_out, "", rough_clamped, "")
    _connect_property(material, rough_clamped, "", unreal.MaterialProperty.MP_ROUGHNESS)

    metal = _scalar_param(material, "Metallic", 0.0, -700, -160)
    _connect_property(material, metal, "", unreal.MaterialProperty.MP_METALLIC)

    spec = _scalar_param(material, "Specular", 0.5, -700, -40)
    _connect_property(material, spec, "", unreal.MaterialProperty.MP_SPECULAR)

    ao = _scalar_param(material, "AmbientOcclusion", 1.0, -700, 80)
    _connect_property(material, ao, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

    emissive_colour = _vector_param(material, "EmissiveColour", (1.0, 1.0, 1.0, 1.0), -700, 200)
    emissive_strength = _scalar_param(material, "EmissiveStrength", 0.0, -700, 340, 0.0, 50.0)
    emissive_mul = _expr(material, unreal.MaterialExpressionMultiply, -380, 240)
    _connect(material, emissive_colour, "", emissive_mul, "A")
    _connect(material, emissive_strength, "", emissive_mul, "B")
    _connect_property(material, emissive_mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # Normal detail. The default texture is empty, so the strength multiplies
    # towards flat: a fresh instance is correct with no texture assigned.
    tiling = _scalar_param(material, "UVTiling", 1.0, -940, 460, 0.05, 40.0)
    texcoord = _expr(material, unreal.MaterialExpressionTextureCoordinate, -1140, 460)
    uv_mul = _expr(material, unreal.MaterialExpressionMultiply, -760, 460)
    _connect(material, texcoord, "", uv_mul, "A")
    _connect(material, tiling, "", uv_mul, "B")

    normal_sample = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -500, 460)
    normal_sample.set_editor_property("parameter_name", "NormalMap")
    # A Normal sampler must default to a normal-compressed texture: the engine's
    # DefaultTexture is a colour texture, and with it the whole master failed to
    # compile on Metal - every surface in the game drew as the grey checker.
    flat_normal = unreal.load_asset("/Engine/EngineMaterials/FlatNormal.FlatNormal")
    if flat_normal is not None:
        normal_sample.set_editor_property("texture", flat_normal)
    normal_sample.set_editor_property("sampler_type",
                                     unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    _connect(material, uv_mul, "", normal_sample, "UVs")

    flat = _constant3(material, (0.0, 0.0, 1.0), -500, 660)
    strength = _scalar_param(material, "DetailNormalStrength", 0.0, -760, 800)
    lerp = _expr(material, unreal.MaterialExpressionLinearInterpolate, -240, 560)
    _connect(material, flat, "", lerp, "A")
    _connect(material, normal_sample, "RGB", lerp, "B")
    _connect(material, strength, "", lerp, "Alpha")
    _connect_property(material, lerp, "", unreal.MaterialProperty.MP_NORMAL)

    # Read by tooling and by the impact system, never rendered: the surface index
    # is the authoritative mapping from a mesh to a footstep/impact sound.
    surface_index = _scalar_param(material, "SurfaceTypeIndex", 2.0, -940, 0, 0.0, 20.0)
    surface_index.set_editor_property("parameter_name", "SurfaceTypeIndex")

    _finish(material, created, "M_OB_Surface", report)
    return material


def build_master_glass(report):
    """M_OB_Glass: office glazing, the Skydeck screens, and puddles."""
    material, created = _create_asset("M_OB_Glass", C.MATERIALS, unreal.Material,
                                      unreal.MaterialFactoryNew())
    if material is None or not _mel():
        report.failed("M_OB_Glass", "could not author the glass master")
        return material

    _set_shading(material, "MSM_DEFAULT_LIT")
    _set_blend(material, "BLEND_TRANSLUCENT")

    base = _vector_param(material, "BaseColour", (0.75, 0.80, 0.85, 1.0), -700, -420)
    _connect_property(material, base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    opacity = _scalar_param(material, "Opacity", 0.15, -700, -280)
    _connect_property(material, opacity, "", unreal.MaterialProperty.MP_OPACITY)

    rough = _scalar_param(material, "Roughness", 0.04, -700, -160)
    _connect_property(material, rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    metal = _scalar_param(material, "Metallic", 0.0, -700, -40)
    _connect_property(material, metal, "", unreal.MaterialProperty.MP_METALLIC)

    refraction = _scalar_param(material, "Refraction", 1.02, -700, 80, 1.0, 1.4)
    _connect_property(material, refraction, "", unreal.MaterialProperty.MP_REFRACTION)

    surface_index = _scalar_param(material, "SurfaceTypeIndex", 4.0, -700, 200, 0.0, 20.0)
    surface_index.set_editor_property("parameter_name", "SurfaceTypeIndex")

    _finish(material, created, "M_OB_Glass", report)
    return material


def build_master_emissive(report):
    """M_OB_Emissive: signage, exit signs, emergency beacons, desk task lights."""
    material, created = _create_asset("M_OB_Emissive", C.MATERIALS, unreal.Material,
                                      unreal.MaterialFactoryNew())
    if material is None or not _mel():
        report.failed("M_OB_Emissive", "could not author the emissive master")
        return material

    _set_shading(material, "MSM_UNLIT")
    _set_blend(material, "BLEND_OPAQUE")

    colour = _vector_param(material, "EmissiveColour", (0.08, 0.52, 1.0, 1.0), -700, -200)
    strength = _scalar_param(material, "EmissiveStrength", 3.0, -700, -40, 0.0, 60.0)
    mul = _expr(material, unreal.MaterialExpressionMultiply, -380, -140)
    _connect(material, colour, "", mul, "A")
    _connect(material, strength, "", mul, "B")
    _connect_property(material, mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    _finish(material, created, "M_OB_Emissive", report)
    return material


def build_master_screen(report):
    """M_OB_Screen: lift indicator panels, floor signage, terminal monitors."""
    material, created = _create_asset("M_OB_Screen", C.MATERIALS, unreal.Material,
                                      unreal.MaterialFactoryNew())
    if material is None or not _mel():
        report.failed("M_OB_Screen", "could not author the screen master")
        return material

    _set_shading(material, "MSM_UNLIT")
    _set_blend(material, "BLEND_OPAQUE")

    sample = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -700, -200)
    sample.set_editor_property("parameter_name", "ScreenTexture")
    sample.set_editor_property("sampler_type",
                               unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    tint = _vector_param(material, "ScreenTint", (1.0, 1.0, 1.0, 1.0), -700, 40)
    tint_mul = _expr(material, unreal.MaterialExpressionMultiply, -400, -140)
    _connect(material, sample, "RGB", tint_mul, "A")
    _connect(material, tint, "", tint_mul, "B")

    brightness = _scalar_param(material, "ScreenBrightness", 2.0, -700, 200, 0.0, 20.0)
    bright_mul = _expr(material, unreal.MaterialExpressionMultiply, -140, -60)
    _connect(material, tint_mul, "", bright_mul, "A")
    _connect(material, brightness, "", bright_mul, "B")
    _connect_property(material, bright_mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    _finish(material, created, "M_OB_Screen", report)
    return material


def build_master_decal(report):
    """M_OB_Decal: bullet holes, blood, scuffs, wet footprints, floor signage."""
    material, created = _create_asset("M_OB_Decal", C.MATERIALS, unreal.Material,
                                      unreal.MaterialFactoryNew())
    if material is None or not _mel():
        report.failed("M_OB_Decal", "could not author the decal master")
        return material

    try:
        material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  decal domain not settable: {0}".format(exc))

    _set_blend(material, "BLEND_TRANSLUCENT")

    sample = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -700, -200)
    sample.set_editor_property("parameter_name", "DecalTexture")
    sample.set_editor_property("sampler_type",
                               unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    tint = _vector_param(material, "BaseColour", (1.0, 1.0, 1.0, 1.0), -700, 40)
    mul = _expr(material, unreal.MaterialExpressionMultiply, -400, -140)
    _connect(material, sample, "RGB", mul, "A")
    _connect(material, tint, "", mul, "B")
    _connect_property(material, mul, "", unreal.MaterialProperty.MP_BASE_COLOR)

    opacity = _scalar_param(material, "Opacity", 1.0, -700, 260)
    _connect_property(material, opacity, "", unreal.MaterialProperty.MP_OPACITY)

    normal = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -700, 400)
    normal.set_editor_property("parameter_name", "DecalNormal")
    normal.set_editor_property("sampler_type",
                               unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    _connect_property(material, normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    _finish(material, created, "M_OB_Decal", report)
    return material


# Usage flags per master. A cooked build cannot add one at runtime: a material
# drawn on a component kind it was not flagged for is swapped for the engine's
# default material ("missing usage flag InstancedStaticMeshes! Default
# Material will be used in game" - MI_Metal on the lift's ISM landing doors,
# the gate glass). The editor sets them silently on first use and never saves
# them, which is why the editor never showed it. What draws where (read from
# the maps and the mesh assets):
#   ISM          the lift's landing doors and glass frame, the kit's repeats
#   skeletal     the general-purpose masters, for anything a cosmetic or prop
#                attaches to a character or a weapon's skeletal mesh
#   Nanite       the opaque masters, so enabling Nanite on a kit mesh does not
#                quietly turn it into the default material in a package
# build_district.py's masters (Facade, AdScreen) finish through _finish too.
MASTER_USAGE = {
    "M_OB_Surface":         ("instanced_static_meshes", "skeletal_mesh", "nanite"),
    "M_OB_SurfaceTextured": ("instanced_static_meshes", "skeletal_mesh", "nanite"),
    "M_OB_Glass":           ("instanced_static_meshes", "skeletal_mesh"),
    "M_OB_GlassGrimed":     ("instanced_static_meshes", "skeletal_mesh"),
    "M_OB_Emissive":        ("instanced_static_meshes", "skeletal_mesh", "nanite"),
    "M_OB_Screen":          ("instanced_static_meshes", "nanite"),
    "M_OB_Facade":          ("instanced_static_meshes", "nanite"),
    "M_OB_AdScreen":        ("instanced_static_meshes", "nanite"),
    "M_OB_SkyDome":         ("instanced_static_meshes",),
}


def _set_usage(material, name):
    for usage in MASTER_USAGE.get(name, ()):
        try:
            material.set_editor_property("used_with_" + usage, True)
        except Exception as exc:  # noqa: BLE001
            C.warn(u"  {0}: usage {1} not settable: {2}".format(name, usage, exc))


def _finish(material, created, name, report):
    _set_usage(material, name)
    try:
        _mel().recompile_material(material)
    except Exception:  # noqa: BLE001
        pass
    path = u"{0}/{1}".format(C.MATERIALS, name)
    unreal.EditorAssetLibrary.save_asset(path)
    if created:
        report.created_asset(path)
        C.log(u"  {0}: authored".format(name))
    else:
        report.updated_asset(path)


# ---------------------------------------------------------------------------
#  Instances
# ---------------------------------------------------------------------------

def _apply_instance(material_instance, master, base_key, roughness, metallic,
                    surface_index=None, extras=None, physical=None):
    extras = extras or {}
    library = _mel()

    library.set_material_instance_parent(material_instance, master)

    rgb = base_key if isinstance(base_key, tuple) else _tint(base_key)
    library.set_material_instance_vector_parameter_value(
        material_instance, "BaseColour", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    try:
        library.set_material_instance_scalar_parameter_value(
            material_instance, "Roughness", roughness)
        library.set_material_instance_scalar_parameter_value(
            material_instance, "Metallic", metallic)
    except Exception:  # noqa: BLE001
        pass

    if surface_index is not None:
        try:
            library.set_material_instance_scalar_parameter_value(
                material_instance, "SurfaceTypeIndex", float(surface_index))
        except Exception:  # noqa: BLE001
            pass

    for key, value in extras.items():
        try:
            if key.startswith("Emissive") and key.endswith("Colour"):
                tint = _tint(value)
                library.set_material_instance_vector_parameter_value(
                    material_instance, key, unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
            else:
                library.set_material_instance_scalar_parameter_value(
                    material_instance, key, float(value))
        except Exception as exc:  # noqa: BLE001
            C.warn(u"  {0}.{1} not set: {2}".format(material_instance.get_name(), key, exc))

    if physical is not None:
        try:
            material_instance.set_editor_property("phys_material", physical)
        except Exception:  # noqa: BLE001
            pass


def build_surface_instances(report, masters, physicals):
    """MI_<Surface> for all 14 surfaces, parented to the right master."""
    instances = {}

    for name, colour_key, roughness in SURFACES:
        master = masters.get("Glass" if name in GLASS_SURFACES else "Surface")
        if master is None:
            report.skipped_asset(u"{0}/MI_{1}".format(C.MATERIALS_INSTANCES, name),
                                 "master material unavailable")
            continue

        asset, created = _create_asset(
            "MI_" + name, C.MATERIALS_INSTANCES, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())

        if asset is None:
            report.failed("MI_" + name, "could not create material instance")
            continue

        _apply_instance(
            asset, master, SURFACE_ALBEDO.get(name, colour_key), roughness,
            0.85 if name in METAL_SURFACES else 0.0,
            SURFACE_ORDER.index(name) if name in SURFACE_ORDER else 2,
            physical=physicals.get(name))

        path = u"{0}/MI_{1}".format(C.MATERIALS_INSTANCES, name)
        unreal.EditorAssetLibrary.save_asset(path)
        report.created_asset(path) if created else report.updated_asset(path)
        instances[name] = asset
        C.log(u"  MI_{0}: {1}, roughness {2:.2f}{3}".format(
            name, colour_key, roughness, " (metal)" if name in METAL_SURFACES else ""))

    return instances


def build_brand_instances(report, masters):
    """The brand and alarm set: everything that carries the One Braham identity."""
    instances = {}

    for name, master_key, colour_key, roughness, metallic, extras in BRAND_INSTANCES:
        master = masters.get(master_key)
        if master is None:
            report.skipped_asset(u"{0}/{1}".format(C.MATERIALS_INSTANCES, name),
                                 u"master M_OB_{0} unavailable".format(master_key))
            continue

        asset, created = _create_asset(
            name, C.MATERIALS_INSTANCES, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())

        if asset is None:
            report.failed(name, "could not create material instance")
            continue

        _apply_instance(asset, master, colour_key, roughness, metallic, extras=extras)

        path = u"{0}/{1}".format(C.MATERIALS_INSTANCES, name)
        unreal.EditorAssetLibrary.save_asset(path)
        report.created_asset(path) if created else report.updated_asset(path)
        instances[name] = asset

    C.log(u"  brand instances: {0}".format(len(instances)))
    return instances


# ---------------------------------------------------------------------------
#  Global parameter collection and the accessibility post-process
# ---------------------------------------------------------------------------
# MPC_OneBraham_Global: every parameter the C++ writes with
# UKismetMaterialLibrary::Set*ParameterValue. The names are the contract:
#   FlashAlpha       AOneBrahamPlayerController::TickFlash, 0 .. 1 white-out
#   ColourBlindMode  UOneBrahamGameUserSettings::SetColourBlindMode, the
#                    EOneBrahamColourBlindMode value (0 off, 1 protanopia,
#                    2 deuteranopia, 3 tritanopia)
GLOBAL_COLLECTION = "MPC_OneBraham_Global"
GLOBAL_SCALARS = (
    ("FlashAlpha", 0.0),
    ("ColourBlindMode", 0.0),
)

# Colour-blind correction, one 3x3 per EOneBrahamColourBlindMode, in linear
# RGB: daltonisation, c' = c + E (c - S c) = (I + E (I - S)) c. S simulates
# the deficiency (Machado, Oliveira & Fernandes 2009, severity 1.0); E moves
# the colour the player cannot see into channels they can. Red-green types
# push the lost red/green difference into green and blue (Fidaner et al.);
# tritanopia pushes the lost blue into red and green. Precomputed here so the
# shader carries one matrix per mode, not the simulation and the shift.
_CVD_SIMULATION = (
    ((0.152286, 1.052583, -0.204868), (0.114503, 0.786281, 0.099216), (-0.003882, -0.048116, 1.051998)),
    ((0.367322, 0.860646, -0.227968), (0.280085, 0.672501, 0.047413), (-0.011820, 0.042940, 0.968881)),
    ((1.255528, -0.076749, -0.178779), (-0.078411, 0.930809, 0.147602), (0.004733, 0.691367, 0.303900)),
)
_CVD_SHIFT = (
    ((0.0, 0.0, 0.0), (0.7, 1.0, 0.0), (0.7, 0.0, 1.0)),
    ((0.0, 0.0, 0.0), (0.7, 1.0, 0.0), (0.7, 0.0, 1.0)),
    ((1.0, 0.0, 0.7), (0.0, 1.0, 0.7), (0.0, 0.0, 0.0)),
)


def _cvd_delta(mode_index):
    """(I + E(I - S)) - I = E(I - S): the rows added to identity for one mode."""
    sim, shift = _CVD_SIMULATION[mode_index], _CVD_SHIFT[mode_index]
    lost = [[(1.0 if r == c else 0.0) - sim[r][c] for c in range(3)] for r in range(3)]
    return [tuple(sum(shift[r][k] * lost[k][c] for k in range(3)) for c in range(3))
            for r in range(3)]


def build_global_collection(report):
    """MPC_OneBraham_Global, with GLOBAL_SCALARS. An existing collection keeps
    its parameters (and their ids: a material's collection node finds its
    parameter by id, so recreating them would orphan every reader) and only
    gains the missing ones."""
    path = u"{0}/{1}".format(C.MATERIALS, GLOBAL_COLLECTION)
    collection, created = _create_asset(
        GLOBAL_COLLECTION, C.MATERIALS, unreal.MaterialParameterCollection,
        unreal.MaterialParameterCollectionFactoryNew())
    if collection is None:
        report.failed(GLOBAL_COLLECTION, "could not create the parameter collection")
        return None

    scalars = list(collection.get_editor_property("scalar_parameters"))
    by_name = dict((str(p.get_editor_property("parameter_name")), p) for p in scalars)
    for name, default in GLOBAL_SCALARS:
        parameter = by_name.get(name)
        if parameter is None:
            parameter = unreal.CollectionScalarParameter()
            parameter.set_editor_property("parameter_name", name)
            scalars.append(parameter)
        parameter.set_editor_property("default_value", default)
    collection.set_editor_property("scalar_parameters", scalars)

    unreal.EditorAssetLibrary.save_asset(path)
    report.created_asset(path) if created else report.updated_asset(path)
    C.log(u"  {0}: {1}".format(GLOBAL_COLLECTION, ", ".join(n for n, _ in GLOBAL_SCALARS)))
    return collection


def build_master_accessibility(report):
    """M_OB_PP_Accessibility: the flash-bang white-out and the colour-blind
    modes, as one post-process material on the global volume.

        c      = PostProcessInput0.rgb                (after tonemapping)
        lin    = c * c                                (display -> ~linear)
        w_m    = saturate(1 - |ColourBlindMode - m|)  m = 1, 2, 3
        row_i  = I_i + sum_m w_m * delta_m,i          (_cvd_delta)
        cvd    = sqrt(saturate(dot(row_i, lin)))
        graded = lerp(c, cvd, saturate(ColourBlindMode))
        out    = lerp(graded, FlashColour, FlashAlpha)

    After tonemapping, so the white-out is the white of the screen whatever the
    exposure, and the matrices act on the colours the player actually sees.
    Both lerps start from c itself: with FlashAlpha and ColourBlindMode at 0
    (the collection's defaults) the frame is passed through untouched."""
    name = "M_OB_PP_Accessibility"
    material, created = _create_asset(name, C.MATERIALS, unreal.Material,
                                      unreal.MaterialFactoryNew())
    if material is None or not _mel():
        report.failed(name, "could not author the accessibility post-process")
        return material
    collection = unreal.EditorAssetLibrary.load_asset(
        u"{0}/{1}".format(C.MATERIALS, GLOBAL_COLLECTION))
    if collection is None:
        report.failed(name, GLOBAL_COLLECTION + " missing - the collection step failed")
        return None

    try:
        material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
        material.set_editor_property("blendable_location",
                                     unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
        # Last in the chain. Same-priority blendables run in volume order, and
        # the global volume (priority -1) is blended first, so at 0 each floor's
        # M_OB_Highlight would draw after this: an interaction outline over a
        # full white-out, and outline colours the daltonisation never saw.
        material.set_editor_property("blendable_priority", 100)
    except Exception as exc:  # noqa: BLE001
        report.failed(name, u"post-process domain not settable: {0}".format(exc))
        return material
    _set_shading(material, "MSM_UNLIT")
    _set_blend(material, "BLEND_OPAQUE")

    def op(cls, a, b, x, y):
        node = _expr(material, cls, x, y)
        _connect(material, a, "", node, "A")
        _connect(material, b, "", node, "B")
        return node

    def one(cls, a, x, y):
        node = _expr(material, cls, x, y)
        _connect(material, a, "", node, "")
        return node

    def collection_param(parameter, x, y):
        node = _expr(material, unreal.MaterialExpressionCollectionParameter, x, y)
        node.set_editor_property("collection", collection)
        node.set_editor_property("parameter_name", parameter)
        return node

    scene = _expr(material, unreal.MaterialExpressionSceneTexture, -1900, -400)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    colour = _expr(material, unreal.MaterialExpressionComponentMask, -1700, -400)
    for channel, keep in (("r", True), ("g", True), ("b", True), ("a", False)):
        colour.set_editor_property(channel, keep)
    _connect(material, scene, "Color", colour, "")
    linear = op(unreal.MaterialExpressionMultiply, colour, colour, -1500, -300)

    # One weight per deficiency: 1 on its mode, 0 on the others.
    mode = collection_param("ColourBlindMode", -1900, 200)
    weights = []
    for index in range(3):
        y = 100 + index * 160
        offset = op(unreal.MaterialExpressionSubtract, mode,
                    _scalar_constant(material, float(index + 1), -1700, y + 60), -1500, y)
        weights.append(one(unreal.MaterialExpressionSaturate,
                           one(unreal.MaterialExpressionOneMinus,
                               one(unreal.MaterialExpressionAbs, offset, -1350, y), -1200, y),
                           -1050, y))

    deltas = [_cvd_delta(index) for index in range(3)]
    channels = []
    for row in range(3):
        y = -700 + row * 420
        identity = tuple(1.0 if column == row else 0.0 for column in range(3))
        total = _constant3(material, identity, -900, y)
        for index in range(3):
            scaled = op(unreal.MaterialExpressionMultiply,
                        _constant3(material, deltas[index][row], -900, y + 90 * (index + 1)),
                        weights[index], -700, y + 90 * (index + 1))
            total = op(unreal.MaterialExpressionAdd, total, scaled, -500, y + 90 * (index + 1))
        dot = op(unreal.MaterialExpressionDotProduct, total, linear, -300, y)
        channels.append(dot)
    rg = op(unreal.MaterialExpressionAppendVector, channels[0], channels[1], -120, -500)
    rgb = op(unreal.MaterialExpressionAppendVector, rg, channels[2], 40, -400)
    corrected = one(unreal.MaterialExpressionSquareRoot,
                    one(unreal.MaterialExpressionSaturate, rgb, 200, -400), 360, -400)

    graded = _expr(material, unreal.MaterialExpressionLinearInterpolate, 560, -300)
    _connect(material, colour, "", graded, "A")
    _connect(material, corrected, "", graded, "B")
    _connect(material, one(unreal.MaterialExpressionSaturate, mode, 360, -160), "", graded, "Alpha")

    # The white-out. A touch warm, as a retina reads a magnesium flash.
    flash = collection_param("FlashAlpha", 360, 0)
    out = _expr(material, unreal.MaterialExpressionLinearInterpolate, 760, -200)
    _connect(material, graded, "", out, "A")
    _connect(material, _vector_param(material, "FlashColour", (1.0, 0.98, 0.94, 1.0), 560, -60),
             "", out, "B")
    _connect(material, flash, "", out, "Alpha")
    _connect_property(material, out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    _finish(material, created, name, report)
    return material


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("build_materials must run inside the Unreal Editor.")

    report = C.BuildReport("build_materials")
    C.log(u"=== Building One Braham materials ===")

    C.guard(report, "directories", _ensure_dirs)

    physicals = C.guard(report, "physical materials", build_physical_materials, report) or {}
    C.guard(report, "parameter collection", build_global_collection, report)

    masters = {}

    def _masters(report):
        for key, fn in (("Surface", build_master_surface), ("Glass", build_master_glass),
                        ("Emissive", build_master_emissive), ("Screen", build_master_screen),
                        ("Decal", build_master_decal), ("Highlight", build_master_highlight),
                        ("Accessibility", build_master_accessibility)):
            asset = fn(report)
            if asset is not None:
                masters[key] = asset
        return masters

    C.guard(report, "master materials", _masters, report)

    C.guard(report, "surface instances", build_surface_instances, report, masters, physicals)
    C.guard(report, "brand instances", build_brand_instances, report, masters)
    C.guard(report, "textured surfaces", build_textured_surfaces, report)
    C.guard(report, "glass grime", build_grimed_glass, report)
    C.guard(report, "decal textures", apply_decal_texture_sets, report)

    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)


def build_master_highlight(report):
    """M_OB_Highlight: the interaction outline, as a post-process material.

    Graph:
        SceneTexture(PostProcessInput0) -------------------------------.
                                                                        + --> EmissiveColor
        SceneTexture(CustomStencil) -- Subtract(StencilTarget) -> Abs ->  |
            Saturate -> OneMinus ---------------------> Multiply(  <----'
                                                          OutlineColour)

    WHY IT IS A MATERIAL AND NOT AN OUTLINE COMPONENT
        Six interactables in this game already set a custom-depth stencil when
        the player looks at them - the keycard sets 2 while highlighted and 1
        while not, and `SetInteractionHighlight_Implementation` on every other
        one is a two-line function that does the same. Nothing read those values:
        the stencil was written to the depth buffer and no material consumed it,
        so the highlight existed in code, in the docs and in the HUD's intent,
        and on screen it did nothing at all. This is the half that reads it.

    THE ONE NUMBER THAT CAN BE WRONG WITHOUT LOOKING WRONG
        `StencilTarget` is a parameter, not a constant, because the units the
        CustomStencil node hands back are the one thing about this graph that has
        moved between engine versions (raw 0-255 in 5.5). If the outline ever
        appears over ordinary geometry - stencil 0 being read as if it were 2 -
        set StencilTarget to 0.0078 (2/255) on the PostProcess volume's instance
        and it comes back on the highlighted object. Docs/10 section 2.7 says the
        same thing next to the picture of the graph.
    """
    material, created = _create_asset("M_OB_Highlight", C.MATERIALS, unreal.Material,
                                      unreal.MaterialFactoryNew())
    if material is None:
        report.failed("M_OB_Highlight", "could not create material")
        return None

    if not _mel():
        report.failed("M_OB_Highlight", "MaterialEditingLibrary unavailable - cannot "
                                        "author the node graph in this build")
        return material

    try:
        material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  post-process domain not settable: {0}".format(exc))
        return material

    _set_shading(material, "MSM_UNLIT")
    _set_blend(material, "BLEND_OPAQUE")

    # The scene, unchanged: a post-process material replaces what it is handed,
    # so the pass-through is not decoration, it is the rest of the frame.
    scene = _expr(material, unreal.MaterialExpressionSceneTexture, -900, -300)
    try:
        scene.set_editor_property("scene_texture_id",
                                  unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  scene texture id not settable: {0}".format(exc))

    stencil = _expr(material, unreal.MaterialExpressionSceneTexture, -900, 0)
    try:
        stencil.set_editor_property("scene_texture_id",
                                    unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  custom stencil id not settable: {0}".format(exc))

    target = _scalar_param(material, "StencilTarget", 2.0, -900, 260, 0.0, 255.0)

    # 1 - saturate(|S - target|): 1 exactly on the highlighted object, 0 everywhere
    # else. Written with Abs/Saturate/OneMinus rather than an If node so that it
    # is four nodes a person can read in the editor, not one node they have to
    # open.
    # SceneTexture hands back float4; the stencil is in R, the frame in RGB.
    # Mixing a float4 into float3 maths is a compile error on Metal (and the
    # default material it falls back to is what framed every shot in brown).
    stencil_r = _expr(material, unreal.MaterialExpressionComponentMask, -760, 20)
    for channel, keep in (("r", True), ("g", False), ("b", False), ("a", False)):
        stencil_r.set_editor_property(channel, keep)
    _connect(material, stencil, "Color", stencil_r, "")

    scene_rgb = _expr(material, unreal.MaterialExpressionComponentMask, -700, -300)
    for channel, keep in (("r", True), ("g", True), ("b", True), ("a", False)):
        scene_rgb.set_editor_property(channel, keep)
    _connect(material, scene, "Color", scene_rgb, "")

    subtract = _expr(material, unreal.MaterialExpressionSubtract, -620, 20)
    _connect(material, stencil_r, "", subtract, "A")
    _connect(material, target, "", subtract, "B")

    absolute = _expr(material, unreal.MaterialExpressionAbs, -440, 20)
    _connect(material, subtract, "", absolute, "")

    clamped = _expr(material, unreal.MaterialExpressionSaturate, -280, 20)
    _connect(material, absolute, "", clamped, "")

    inverted = _expr(material, unreal.MaterialExpressionOneMinus, -120, 20)
    _connect(material, clamped, "", inverted, "")

    colour = _vector_param(material, "OutlineColour",
                           _tint("SignalBlue") + (1.0,), -620, 260)

    tinted = _expr(material, unreal.MaterialExpressionMultiply, 80, 120)
    _connect(material, inverted, "", tinted, "A")
    _connect(material, colour, "", tinted, "B")

    combined = _expr(material, unreal.MaterialExpressionAdd, 280, -60)
    _connect(material, scene_rgb, "", combined, "A")
    _connect(material, tinted, "", combined, "B")

    _connect_property(material, combined, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    _finish(material, created, "M_OB_Highlight", report)
    return material


# ---------------------------------------------------------------------------
#  Textured surfaces (CC0 sets from SourceArt/, world-aligned)
# ---------------------------------------------------------------------------
# Texture set -> surface, with how the set is used. The textures give detail -
# grit, grain, joints, the way light catches them - and the colour stays the
# measured albedo (SURFACE_ALBEDO): a set is desaturated by `desat`, its
# brightness normalised by `gain`, then tinted. That is how a brown terrazzo
# photograph becomes One Braham's pale grey one without being re-authored.
# size: world size of one texture repeat, cm. Docs/21 §4 is the bar a set must
# clear before it goes in this table.
TEXTURE_SETS = {
    "Marble":       ("terrazzo_tiles",    120.0, 1.0, 2.2),
    "Concrete":     ("concrete_floor_02", 220.0, 0.7, 1.6),
    "Wood":         ("oak_wood_planks",   180.0, 0.6, 1.5),
    "Plasterboard": ("white_plaster_02",  200.0, 1.0, 1.25),
    "Carpet":       ("Carpet001",         180.0, 1.0, 2.2),   # loop-pile office tile (ambientCG)
    "Metal":        ("Metal011",          140.0, 1.0, 1.9),   # brushed steel (ambientCG)
    "Decking":      ("Planks037A",        240.0, 0.4, 2.4),   # terrace planks (ambientCG)
    "Lino":         ("rubber_tiles",      120.0, 0.7, 1.8),   # canteen floor
    # Plain-weave contract upholstery (ambientCG), spine screens and seats.
    # The fifth value scales the set's roughness map: woven cloth is matt.
    "Fabric":       ("Fabric030",          40.0, 1.0, 8.0, 1.25),
}
# District surfaces (build_district) that take a set too.
DISTRICT_TEXTURE_SETS = {
    "Paving":  ("concrete_pavement", 260.0, 0.6, 1.4),
    "Asphalt": ("asphalt_02",        320.0, 0.4, 1.8),
    "TerracePaving": ("concrete_pavement", 240.0, 1.0, 1.6),
    "Herringbone": ("oak_wood_planks", 90.0, 0.6, 1.9),
    "TimberLight": ("oak_wood_planks", 160.0, 0.6, 2.0),
    # The fit-out's upholstery: the same woven cloth as MI_Fabric in the
    # booths' purple and the lounge sofa's ink, and a fine flat knit for the
    # task chairs' mesh. FeltWhite keeps its flat instance: the textured
    # master has no emissive, and the discs' edge glow is the point of them.
    "BoothPurple": ("Fabric030", 40.0, 1.0, 8.0, 1.25),
    "SofaDark":    ("Fabric030", 40.0, 1.0, 8.0, 1.3),
    "MeshWhite":   ("Fabric031", 25.0, 1.0, 5.0, 1.4),
}
TEXTURE_FOLDER = C.MATERIALS + "/Textures"
WORLD_ALIGNED_TEXTURE = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture.WorldAlignedTexture"
WORLD_ALIGNED_NORMAL = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal.WorldAlignedNormal"


def _source_art_manifest():
    import json, os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "SourceArt", "open_assets.json")
    path = os.path.abspath(path)
    if not os.path.exists(path):
        return None, None
    with open(path) as handle:
        return json.load(handle), os.path.dirname(os.path.dirname(path))


SURFACE_ROLES = ("albedo", "normal", "roughness")


def import_texture_set(report, set_id, roles=SURFACE_ROLES):
    """Imports a set's maps - albedo, normal and roughness unless `roles` asks
    for others (the grime and decal sets add an opacity mask); returns
    {role: texture}."""
    import os
    manifest, root = _source_art_manifest()
    if manifest is None:
        return None
    out = {}
    if not unreal.EditorAssetLibrary.does_directory_exist(TEXTURE_FOLDER):
        unreal.EditorAssetLibrary.make_directory(TEXTURE_FOLDER)
    for role in roles:
        entry = manifest.get("{0}:{1}".format(set_id, role))
        if entry is None:
            return None
        name = "T_{0}_{1}".format(set_id, role)
        path = "{0}/{1}".format(TEXTURE_FOLDER, name)
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", os.path.join(root, entry["path"]))
            task.set_editor_property("destination_path", TEXTURE_FOLDER)
            task.set_editor_property("destination_name", name)
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("automated", True)
            task.set_editor_property("save", False)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.EditorAssetLibrary.load_asset(path)
        if texture is None:
            report.failed(path, "could not import " + entry["path"])
            return None
        if role == "normal":
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            texture.set_editor_property("srgb", False)
        elif role in ("roughness", "opacity"):
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            texture.set_editor_property("srgb", False)
        unreal.EditorAssetLibrary.save_asset(path)
        out[role] = texture
    return out


def build_master_textured(report):
    """M_OB_SurfaceTextured: M_OB_Surface's parameters, with world-aligned
    (triplanar) albedo detail, roughness and normal maps - box-projected in
    world space, so a slab made of one scaled cube still shows 60 cm tiles."""
    path = C.MATERIALS + "/M_OB_SurfaceTextured"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_OB_SurfaceTextured", C.MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        report.failed("M_OB_SurfaceTextured", "could not create material")
        return None
    wat = unreal.load_asset(WORLD_ALIGNED_TEXTURE)
    wan = unreal.load_asset(WORLD_ALIGNED_NORMAL)
    if wat is None or wan is None:
        report.failed("M_OB_SurfaceTextured", "world-aligned material functions not found")
        return None

    def tex_object(name, default, y):
        node = _expr(material, unreal.MaterialExpressionTextureObjectParameter, -1500, y)
        node.set_editor_property("parameter_name", name)
        if default is not None:
            node.set_editor_property("texture", default)
        return node

    def call(function, y):
        node = _expr(material, unreal.MaterialExpressionMaterialFunctionCall, -1150, y)
        node.set_editor_property("material_function", function)
        return node

    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")
    flat = unreal.load_asset("/Engine/EngineMaterials/FlatNormal.FlatNormal")
    size = _scalar_param(material, "TextureSizeCm", 200.0, -1500, -700, 10.0, 2000.0)

    albedo_obj = tex_object("AlbedoTex", white, -560)
    albedo = call(wat, -560)
    _connect(material, albedo_obj, "", albedo, "TextureObject")
    _connect(material, size, "", albedo, "TextureSize")
    desat = _expr(material, unreal.MaterialExpressionDesaturation, -900, -560)
    _connect(material, albedo, "XYZ Texture", desat, "")
    _connect(material, _scalar_param(material, "Desaturation", 0.0, -1150, -420), "", desat, "Fraction")
    gain = _expr(material, unreal.MaterialExpressionMultiply, -720, -560)
    _connect(material, desat, "", gain, "A")
    _connect(material, _scalar_param(material, "TextureGain", 1.0, -900, -420, 0.0, 8.0), "", gain, "B")
    tint = _expr(material, unreal.MaterialExpressionMultiply, -540, -560)
    _connect(material, gain, "", tint, "A")
    _connect(material, _vector_param(material, "BaseColour", (1.0, 1.0, 1.0, 1.0), -720, -420), "", tint, "B")
    _connect_property(material, tint, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rough_obj = tex_object("RoughnessTex", white, -260)
    rough = call(wat, -260)
    _connect(material, rough_obj, "", rough, "TextureObject")
    _connect(material, size, "", rough, "TextureSize")
    rough_r = _expr(material, unreal.MaterialExpressionComponentMask, -900, -260)
    for channel, on in (("r", True), ("g", False), ("b", False), ("a", False)):
        rough_r.set_editor_property(channel, on)
    _connect(material, rough, "XYZ Texture", rough_r, "")
    rough_scale = _expr(material, unreal.MaterialExpressionMultiply, -720, -260)
    _connect(material, rough_r, "", rough_scale, "A")
    _connect(material, _scalar_param(material, "Roughness", 1.0, -900, -140, 0.0, 2.0), "", rough_scale, "B")
    rough_sat = _expr(material, unreal.MaterialExpressionSaturate, -540, -260)
    _connect(material, rough_scale, "", rough_sat, "")
    _connect_property(material, rough_sat, "", unreal.MaterialProperty.MP_ROUGHNESS)

    normal_obj = tex_object("NormalTex", flat, 60)
    normal = call(wan, 60)
    _connect(material, normal_obj, "", normal, "TextureObject")
    _connect(material, size, "", normal, "TextureSize")
    lerp = _expr(material, unreal.MaterialExpressionLinearInterpolate, -720, 60)
    _connect(material, _constant3(material, (0.0, 0.0, 1.0), -900, 180), "", lerp, "A")
    _connect(material, normal, "XYZ Texture", lerp, "B")
    _connect(material, _scalar_param(material, "NormalStrength", 1.0, -900, 260), "", lerp, "Alpha")
    _connect_property(material, lerp, "", unreal.MaterialProperty.MP_NORMAL)

    _connect_property(material, _scalar_param(material, "Metallic", 0.0, -540, 300), "",
                      unreal.MaterialProperty.MP_METALLIC)
    _connect_property(material, _scalar_param(material, "Specular", 0.5, -540, 400), "",
                      unreal.MaterialProperty.MP_SPECULAR)
    _scalar_param(material, "SurfaceTypeIndex", 2.0, -540, 500, 0.0, 20.0)
    _finish(material, True, "M_OB_SurfaceTextured", report)
    return material


def apply_texture_set(report, instance, master, spec, albedo):
    """Re-parents a surface instance onto the textured master with a set.
    spec: (set, size cm, desaturation, gain[, roughness]) - the optional fifth
    value is the textured master's Roughness, a scale on the set's roughness
    map; without it the instance keeps the flat roughness it already had."""
    set_id, size, desat, gain = spec[:4]
    textures = import_texture_set(report, set_id)
    if textures is None or master is None:
        return False
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(instance, master)
    mel.set_material_instance_texture_parameter_value(instance, "AlbedoTex", textures["albedo"])
    mel.set_material_instance_texture_parameter_value(instance, "NormalTex", textures["normal"])
    mel.set_material_instance_texture_parameter_value(instance, "RoughnessTex", textures["roughness"])
    mel.set_material_instance_scalar_parameter_value(instance, "TextureSizeCm", size)
    mel.set_material_instance_scalar_parameter_value(instance, "Desaturation", desat)
    mel.set_material_instance_scalar_parameter_value(instance, "TextureGain", gain)
    if len(spec) > 4:
        mel.set_material_instance_scalar_parameter_value(instance, "Roughness", spec[4])
    mel.set_material_instance_vector_parameter_value(
        instance, "BaseColour", unreal.LinearColor(albedo[0], albedo[1], albedo[2], 1.0))
    return True


def build_textured_surfaces(report):
    """Puts the texture sets on the surfaces that have one. Without SourceArt/
    (fetch_open_assets.py not run) the flat instances stand, and say so."""
    manifest, _root = _source_art_manifest()
    if manifest is None:
        report.skipped_asset(TEXTURE_FOLDER, "SourceArt/open_assets.json missing - run fetch_open_assets.py")
        return 0
    master = build_master_textured(report)
    applied = 0
    for surface, spec in sorted(TEXTURE_SETS.items()):
        path = "{0}/MI_{1}".format(C.MATERIALS_INSTANCES, surface)
        instance = unreal.EditorAssetLibrary.load_asset(path)
        if instance is None:
            continue
        if apply_texture_set(report, instance, master, spec, SURFACE_ALBEDO[surface]):
            unreal.EditorAssetLibrary.save_asset(path)
            applied += 1
            C.log(u"  MI_{0}: textured with {1}".format(surface, spec[0]))
    return applied


# ---------------------------------------------------------------------------
#  Glass grime (CC0 masks from SourceArt/, world-aligned)
# ---------------------------------------------------------------------------
# Glass instance -> (set, world size of one repeat cm, grime amount). Clean
# glass reads as no glass at all; four to eight years of use (Docs/10 §1) puts
# hand smudges on everything people push past and a dust film on everything
# they do not. The set's opacity map is the mask: where it is white the glass
# turns a little milkier and rougher. Puddles keep the plain glass master.
GLASS_GRIME_SETS = {
    "MI_Glass":              ("Fingerprints002",         80.0, 0.35),  # partitions, meeting rooms
    "MI_BallisticGlass":     ("Fingerprints002",         80.0, 0.45),  # gate wings, security screens
    "MI_OB_Glass_Office":    ("Fingerprints002",         90.0, 0.30),
    "MI_OB_Glass_Ballistic": ("Fingerprints002",         80.0, 0.45),
    "MI_OB_Glass_Clear":     ("SurfaceImperfections015", 200.0, 0.25),  # balustrades: dust, not hands
}
GRIME_ROLES = ("opacity",)


def build_master_glass_grimed(report, default_mask):
    """M_OB_GlassGrimed: M_OB_Glass's parameters, with a world-aligned grime
    mask that lifts opacity and roughness and greys the tint where it lies.

        g          = saturate(WorldAligned(GrimeTex, GrimeSizeCm).r * GrimeAmount)
        BaseColor  = lerp(BaseColour, GrimeColour, g)
        Opacity    = lerp(Opacity, GrimeOpacity, g)
        Roughness  = lerp(Roughness, GrimeRoughness, g)

    Lit per pixel (surface forward shading) so the smudges show in the
    reflections, which is where grime on glass is actually seen.

    `default_mask` is a grime set's opacity map (TC_MASKS, linear): GrimeTex
    takes its sampler type from its default, so a white sRGB default would
    clash with every mask an instance sets."""
    path = C.MATERIALS + "/M_OB_GlassGrimed"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_OB_GlassGrimed", C.MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        report.failed("M_OB_GlassGrimed", "could not create material")
        return None
    wat = unreal.load_asset(WORLD_ALIGNED_TEXTURE)
    if wat is None:
        report.failed("M_OB_GlassGrimed", "world-aligned material function not found")
        return None

    _set_shading(material, "MSM_DEFAULT_LIT")
    _set_blend(material, "BLEND_TRANSLUCENT")
    try:
        material.set_editor_property("translucency_lighting_mode",
                                     unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  glass lighting mode not settable: {0}".format(exc))

    grime_obj = _expr(material, unreal.MaterialExpressionTextureObjectParameter, -1500, -500)
    grime_obj.set_editor_property("parameter_name", "GrimeTex")
    grime_obj.set_editor_property("texture", default_mask)
    grime_obj.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    grime = _expr(material, unreal.MaterialExpressionMaterialFunctionCall, -1150, -500)
    grime.set_editor_property("material_function", wat)
    _connect(material, grime_obj, "", grime, "TextureObject")
    _connect(material, _scalar_param(material, "GrimeSizeCm", 80.0, -1500, -360, 10.0, 2000.0), "",
             grime, "TextureSize")
    grime_r = _expr(material, unreal.MaterialExpressionComponentMask, -900, -500)
    for channel, on in (("r", True), ("g", False), ("b", False), ("a", False)):
        grime_r.set_editor_property(channel, on)
    _connect(material, grime, "XYZ Texture", grime_r, "")
    amount = _expr(material, unreal.MaterialExpressionMultiply, -720, -500)
    _connect(material, grime_r, "", amount, "A")
    _connect(material, _scalar_param(material, "GrimeAmount", 0.0, -900, -380, 0.0, 2.0), "", amount, "B")
    mask = _expr(material, unreal.MaterialExpressionSaturate, -560, -500)
    _connect(material, amount, "", mask, "")

    def blend(clean, dirty, y, prop):
        node = _expr(material, unreal.MaterialExpressionLinearInterpolate, -300, y)
        _connect(material, clean, "", node, "A")
        _connect(material, dirty, "", node, "B")
        _connect(material, mask, "", node, "Alpha")
        _connect_property(material, node, "", prop)

    blend(_vector_param(material, "BaseColour", (0.75, 0.80, 0.85, 1.0), -700, -260),
          _vector_param(material, "GrimeColour", (0.55, 0.55, 0.53, 1.0), -700, -120),
          -200, unreal.MaterialProperty.MP_BASE_COLOR)
    blend(_scalar_param(material, "Opacity", 0.15, -700, 20),
          _scalar_param(material, "GrimeOpacity", 0.45, -700, 120),
          60, unreal.MaterialProperty.MP_OPACITY)
    blend(_scalar_param(material, "Roughness", 0.04, -700, 240),
          _scalar_param(material, "GrimeRoughness", 0.45, -700, 340),
          280, unreal.MaterialProperty.MP_ROUGHNESS)

    _connect_property(material, _scalar_param(material, "Metallic", 0.0, -300, 440), "",
                      unreal.MaterialProperty.MP_METALLIC)
    _connect_property(material, _scalar_param(material, "Refraction", 1.02, -300, 540, 1.0, 1.4), "",
                      unreal.MaterialProperty.MP_REFRACTION)
    _scalar_param(material, "SurfaceTypeIndex", 4.0, -300, 640, 0.0, 20.0)
    _finish(material, True, "M_OB_GlassGrimed", report)
    return material


def build_grimed_glass(report):
    """Moves the glass instances in GLASS_GRIME_SETS onto the grimed master.
    Their own parameters (tint, opacity, roughness, refraction, surface index)
    carry over by name. Without SourceArt/ they stay clean, and say so."""
    manifest, _root = _source_art_manifest()
    if manifest is None:
        report.skipped_asset(C.MATERIALS + "/M_OB_GlassGrimed",
                             "SourceArt/open_assets.json missing - run fetch_open_assets.py")
        return 0
    masks = {}
    for set_id in sorted(set(spec[0] for spec in GLASS_GRIME_SETS.values())):
        textures = import_texture_set(report, set_id, GRIME_ROLES)
        if textures is not None:
            masks[set_id] = textures["opacity"]
    if not masks:
        report.skipped_asset(C.MATERIALS + "/M_OB_GlassGrimed", "no grime set in SourceArt/")
        return 0
    master = build_master_glass_grimed(report, masks[sorted(masks)[0]])
    if master is None:
        return 0
    mel = unreal.MaterialEditingLibrary
    applied = 0
    for name, (set_id, size, amount) in sorted(GLASS_GRIME_SETS.items()):
        path = "{0}/{1}".format(C.MATERIALS_INSTANCES, name)
        instance = unreal.EditorAssetLibrary.load_asset(path)
        if instance is None:
            continue
        if set_id not in masks:
            report.skipped_asset(path, "texture set {0} not in SourceArt/".format(set_id))
            continue
        mel.set_material_instance_parent(instance, master)
        mel.set_material_instance_texture_parameter_value(instance, "GrimeTex", masks[set_id])
        mel.set_material_instance_scalar_parameter_value(instance, "GrimeSizeCm", size)
        mel.set_material_instance_scalar_parameter_value(instance, "GrimeAmount", amount)
        unreal.EditorAssetLibrary.save_asset(path)
        applied += 1
        C.log(u"  {0}: grime {1} at {2:.0f} cm".format(name, set_id, size))
    return applied


# ---------------------------------------------------------------------------
#  Decal textures (CC0 sets from SourceArt/)
# ---------------------------------------------------------------------------
# Decal instance (build_vfx.py makes them) -> (set, mask role, tint, texture
# gain, roughness). The mask role names the map that says where the decal is:
# a set's opacity map, or, for a smear photographed on black, its roughness.
# Smear001's colour inside the smear is ~0.04 linear - black rubber already -
# so it goes on untinted. Bullet holes and the glass crack have no CC0 source
# that reads as a bullet hole (Docs/15 carries their briefs); the blood decal
# is procedural by design (build_vfx.M_OB_Decal_Blood).
DECAL_TEXTURE_SETS = {
    C.VFX + "/Decals/M_OB_Decal_Scuff": ("Smear001", "roughness", (1.0, 1.0, 1.0), 1.0, 0.6),
}


def build_master_decal_textured(report, defaults):
    """M_OB_DecalTextured: M_OB_Decal's parameters (DecalTexture, DecalNormal,
    BaseColour, Opacity), plus the mask that shapes it and a roughness.

        BaseColor = DecalTexture.rgb * TextureGain * BaseColour
        Opacity   = DecalMask.r * Opacity
        Roughness = Roughness
        Normal    = lerp(flat, DecalNormal, NormalStrength)

    `defaults` (role -> texture) are the first set's maps: each sampler needs a
    default of its own type, so the master is built after the import."""
    path = C.MATERIALS + "/M_OB_DecalTextured"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_OB_DecalTextured", C.MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        report.failed("M_OB_DecalTextured", "could not create material")
        return None
    try:
        material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  decal domain not settable: {0}".format(exc))
    _set_blend(material, "BLEND_TRANSLUCENT")

    def sample(name, sampler, texture, y):
        node = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("sampler_type", sampler)
        node.set_editor_property("texture", texture)
        return node

    colour = sample("DecalTexture", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, defaults["albedo"], -400)
    gain = _expr(material, unreal.MaterialExpressionMultiply, -560, -400)
    _connect(material, colour, "RGB", gain, "A")
    _connect(material, _scalar_param(material, "TextureGain", 1.0, -900, -260, 0.0, 8.0), "", gain, "B")
    tint = _expr(material, unreal.MaterialExpressionMultiply, -360, -400)
    _connect(material, gain, "", tint, "A")
    _connect(material, _vector_param(material, "BaseColour", (1.0, 1.0, 1.0, 1.0), -560, -260), "", tint, "B")
    _connect_property(material, tint, "", unreal.MaterialProperty.MP_BASE_COLOR)

    mask = sample("DecalMask", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, defaults["mask"], -120)
    opacity = _expr(material, unreal.MaterialExpressionMultiply, -560, -120)
    _connect(material, mask, "R", opacity, "A")
    _connect(material, _scalar_param(material, "Opacity", 1.0, -900, 20), "", opacity, "B")
    _connect_property(material, opacity, "", unreal.MaterialProperty.MP_OPACITY)

    _connect_property(material, _scalar_param(material, "Roughness", 0.6, -560, 120), "",
                      unreal.MaterialProperty.MP_ROUGHNESS)

    normal = sample("DecalNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, defaults["normal"], 240)
    lerp = _expr(material, unreal.MaterialExpressionLinearInterpolate, -560, 240)
    _connect(material, _constant3(material, (0.0, 0.0, 1.0), -900, 400), "", lerp, "A")
    _connect(material, normal, "RGB", lerp, "B")
    _connect(material, _scalar_param(material, "NormalStrength", 1.0, -900, 480), "", lerp, "Alpha")
    _connect_property(material, lerp, "", unreal.MaterialProperty.MP_NORMAL)

    _finish(material, True, "M_OB_DecalTextured", report)
    return material


def apply_decal_texture_sets(report):
    """Puts the CC0 sets on the decal instances that have one, re-parented
    onto M_OB_DecalTextured. The instances are build_vfx.py's: a decal not
    made yet is skipped and picked up on the next materials build (or by
    build_vfx calling this after it makes them)."""
    manifest, _root = _source_art_manifest()
    if manifest is None:
        report.skipped_asset(C.MATERIALS + "/M_OB_DecalTextured",
                             "SourceArt/open_assets.json missing - run fetch_open_assets.py")
        return 0
    sets = {}
    for path, (set_id, mask_role, _tint, _gain, _rough) in sorted(DECAL_TEXTURE_SETS.items()):
        textures = import_texture_set(report, set_id, ("albedo", "normal", mask_role))
        if textures is None:
            report.skipped_asset(path, "texture set {0} not in SourceArt/".format(set_id))
            continue
        textures["mask"] = textures[mask_role]
        sets[path] = textures
    if not sets:
        return 0
    master = build_master_decal_textured(report, sets[sorted(sets)[0]])
    if master is None:
        return 0
    mel = unreal.MaterialEditingLibrary
    applied = 0
    for path, textures in sorted(sets.items()):
        set_id, _mask_role, tint, gain, roughness = DECAL_TEXTURE_SETS[path]
        instance = unreal.EditorAssetLibrary.load_asset(path)
        if instance is None:
            report.skipped_asset(path, "decal instance not built yet - run the vfx step, then materials")
            continue
        mel.set_material_instance_parent(instance, master)
        mel.set_material_instance_texture_parameter_value(instance, "DecalTexture", textures["albedo"])
        mel.set_material_instance_texture_parameter_value(instance, "DecalNormal", textures["normal"])
        mel.set_material_instance_texture_parameter_value(instance, "DecalMask", textures["mask"])
        mel.set_material_instance_scalar_parameter_value(instance, "TextureGain", gain)
        mel.set_material_instance_scalar_parameter_value(instance, "Roughness", roughness)
        mel.set_material_instance_vector_parameter_value(
            instance, "BaseColour", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
        unreal.EditorAssetLibrary.save_asset(path)
        applied += 1
        C.log(u"  {0}: decal textured with {1}".format(path.rsplit("/", 1)[-1], set_id))
    return applied
