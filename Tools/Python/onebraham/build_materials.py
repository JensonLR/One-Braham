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


def _finish(material, created, name, report):
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


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("build_materials must run inside the Unreal Editor.")

    report = C.BuildReport("build_materials")
    C.log(u"=== Building One Braham materials ===")

    C.guard(report, "directories", _ensure_dirs)

    physicals = C.guard(report, "physical materials", build_physical_materials, report) or {}

    masters = {}

    def _masters(report):
        for key, fn in (("Surface", build_master_surface), ("Glass", build_master_glass),
                        ("Emissive", build_master_emissive), ("Screen", build_master_screen),
                        ("Decal", build_master_decal), ("Highlight", build_master_highlight)):
            asset = fn(report)
            if asset is not None:
                masters[key] = asset
        return masters

    C.guard(report, "master materials", _masters, report)

    C.guard(report, "surface instances", build_surface_instances, report, masters, physicals)
    C.guard(report, "brand instances", build_brand_instances, report, masters)
    C.guard(report, "textured surfaces", build_textured_surfaces, report)

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
}
# District surfaces (build_district) that take a set too.
DISTRICT_TEXTURE_SETS = {
    "Paving":  ("concrete_pavement", 260.0, 0.6, 1.4),
    "Asphalt": ("asphalt_02",        320.0, 0.4, 1.8),
    "TerracePaving": ("concrete_pavement", 240.0, 1.0, 1.6),
    "Herringbone": ("oak_wood_planks", 90.0, 0.6, 1.9),
    "TimberLight": ("oak_wood_planks", 160.0, 0.6, 2.0),
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


def import_texture_set(report, set_id):
    """Imports a set's albedo, normal and roughness; returns {role: texture}."""
    import os
    manifest, root = _source_art_manifest()
    if manifest is None:
        return None
    out = {}
    if not unreal.EditorAssetLibrary.does_directory_exist(TEXTURE_FOLDER):
        unreal.EditorAssetLibrary.make_directory(TEXTURE_FOLDER)
    for role in ("albedo", "normal", "roughness"):
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
        elif role == "roughness":
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
    """Re-parents a surface instance onto the textured master with a set."""
    set_id, size, desat, gain = spec
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
