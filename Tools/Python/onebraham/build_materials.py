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
    /Game/OneBraham/Materials/MPC_OneBraham_Global flash, colour-blind mode, rain wetness
    /Game/OneBraham/Materials/M_OB_Surface         master, lit opaque, dirt and wetness
    /Game/OneBraham/Materials/M_OB_Glass           master, translucent, cracks
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

#: How much grime each surface shows (M_OB_Surface DirtAmount). Floors people
#: walk on show the most; a server rack is cleaned more often than anything else
#: in the building.
DIRT = {
    "Concrete": 0.35, "Carpet": 0.20, "Lino": 0.25, "Decking": 0.30, "Grating": 0.30,
    "Metal": 0.15, "ServerRack": 0.10, "Marble": 0.08, "Wood": 0.15,
    "Plasterboard": 0.12, "Fabric": 0.15,
}

#: How much a surface answers the rain (WetnessResponse). Only what is out in
#: the weather: the Skydeck's decking. Indoor concrete stays dry in a wet match.
WET_RESPONSE = {"Decking": 1.0}

#: The global parameter collection C++ writes to: the flashbang white-out, the
#: colour-blind mode, and the rain wetness the surface master reads.
COLLECTION_SCALARS = (("FlashAlpha", 0.0), ("ColourBlindMode", 0.0), ("RainWetness", 0.0))

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
        # A master is authored from scratch every run. Without this, each run
        # added a second copy of every node to the existing graph.
        if asset_class is unreal.Material and _mel() is not None:
            try:
                _mel().delete_all_material_expressions(existing)
            except Exception as exc:  # noqa: BLE001
                C.warn(u"  {0}: old graph not cleared ({1})".format(name, exc))
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

        # The surface type is what footsteps, impacts and decals read. The ini
        # declares SurfaceType1..N in PHYSICAL_SURFACE_NAMES order, and Python
        # exposes them as enum members (SURFACE_TYPE1...). A material left on
        # the default surface sounds like nothing, so a miss is a failure.
        surface = None
        for member in (u"SURFACE_TYPE{0}".format(index), u"SURFACE_TYPE_{0}".format(index)):
            surface = getattr(unreal.PhysicalSurface, member, None)
            if surface is not None:
                break
        if surface is None:
            report.failed("PM_OB_" + name, u"no unreal.PhysicalSurface member for SurfaceType{0}".format(index))
        else:
            try:
                asset.set_editor_property("surface_type", surface)
            except Exception as exc:  # noqa: BLE001
                report.failed("PM_OB_" + name, u"surface_type: {0}".format(exc))

        # How the surface behaves under movement. These are what make concrete
        # feel like concrete and lino feel like lino when someone slides into a
        # desk at speed.
        friction = {"Puddle": 0.10, "Grating": 0.65, "Marble": 0.72, "Lino": 0.80}.get(name, 0.90)
        try:
            asset.set_editor_property("friction", friction)
            asset.set_editor_property("restitution", 0.05 if name not in ("Fabric",) else 0.0)
        except Exception as exc:  # noqa: BLE001
            report.failed(name, u"friction/restitution: {0}".format(exc))

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

def build_parameter_collection(report):
    """MPC_OneBraham_Global: the values C++ sets for every material at once.

    FlashAlpha (the flashbang white-out), ColourBlindMode (world markers follow
    the interface's palette) and RainWetness (the surface master's rain). The
    code has always written to this collection; until this function, nothing
    made it, so every one of those writes went nowhere.
    """
    path = u"{0}/MPC_OneBraham_Global".format(C.MATERIALS)
    collection, created = _create_asset("MPC_OneBraham_Global", C.MATERIALS,
                                        unreal.MaterialParameterCollection,
                                        unreal.MaterialParameterCollectionFactoryNew())
    if collection is None:
        report.failed("MPC_OneBraham_Global", "could not create the collection")
        return None

    existing = {}
    try:
        for parameter in collection.get_editor_property("scalar_parameters"):
            existing[str(parameter.get_editor_property("parameter_name"))] = parameter
    except Exception:  # noqa: BLE001
        existing = {}

    scalars = []
    for name, default in COLLECTION_SCALARS:
        # An existing parameter keeps its id, so materials already compiled
        # against it keep resolving.
        parameter = existing.get(name) or unreal.CollectionScalarParameter()
        parameter.set_editor_property("parameter_name", name)
        parameter.set_editor_property("default_value", float(default))
        scalars.append(parameter)
    collection.set_editor_property("scalar_parameters", scalars)

    unreal.EditorAssetLibrary.save_asset(path)
    report.created_asset(path) if created else report.updated_asset(path)
    C.log(u"  MPC_OneBraham_Global: {0}".format(", ".join(n for n, _ in COLLECTION_SCALARS)))
    return collection


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

    base = _vector_param(material, "BaseColour", (0.5, 0.5, 0.5, 1.0), -1500, -420)
    rough = _scalar_param(material, "Roughness", 0.5, -1500, -280)

    # Dirt: world-aligned noise, so grime is continuous across a wall made of
    # many boxes and never repeats with the UVs. DirtAmount scales it.
    world = _expr(material, unreal.MaterialExpressionWorldPosition, -2100, -900)
    dirt_scale = _scalar_param(material, "DirtScale", 0.004, -2100, -760, 0.0005, 0.05)
    world_mul = _expr(material, unreal.MaterialExpressionMultiply, -1850, -860)
    _connect(material, world, "", world_mul, "A")
    _connect(material, dirt_scale, "", world_mul, "B")
    noise = _expr(material, unreal.MaterialExpressionNoise, -1600, -860)
    for prop, value in (("output_min", 0.0), ("output_max", 1.0), ("levels", 4), ("turbulence", True)):
        try:
            noise.set_editor_property(prop, value)
        except Exception as exc:  # noqa: BLE001
            C.warn(u"  M_OB_Surface noise {0}: {1}".format(prop, exc))
    _connect(material, world_mul, "", noise, "Position")
    dirt_amount = _scalar_param(material, "DirtAmount", 0.0, -1600, -700)
    dirt_mask = _expr(material, unreal.MaterialExpressionMultiply, -1350, -800)
    _connect(material, noise, "", dirt_mask, "A")
    _connect(material, dirt_amount, "", dirt_mask, "B")
    dirt_colour = _vector_param(material, "DirtColour", (0.16, 0.14, 0.11, 1.0), -1350, -640)

    dirty_base = _expr(material, unreal.MaterialExpressionLinearInterpolate, -1100, -480)
    _connect(material, base, "", dirty_base, "A")
    _connect(material, dirt_colour, "", dirty_base, "B")
    _connect(material, dirt_mask, "", dirty_base, "Alpha")
    dirty_rough = _expr(material, unreal.MaterialExpressionLinearInterpolate, -1100, -300)
    _connect(material, rough, "", dirty_rough, "A")
    _connect(material, _scalar_constant(material, 0.92, -1350, -360), "", dirty_rough, "B")
    _connect(material, dirt_mask, "", dirty_rough, "Alpha")

    # Wetness: the rain (MPC_OneBraham_Global.RainWetness, set by the world
    # effects component in a wet match) times how much this surface is out in
    # it. Wet darkens the colour and takes the roughness down to a sheen.
    wet = None
    collection = unreal.EditorAssetLibrary.load_asset(C.MATERIALS + "/MPC_OneBraham_Global")
    if collection is not None:
        rain = _expr(material, unreal.MaterialExpressionCollectionParameter, -1350, -180)
        try:
            rain.set_editor_property("collection", collection)
            rain.set_editor_property("parameter_name", "RainWetness")
            response = _scalar_param(material, "WetnessResponse", 0.0, -1350, -60)
            wet = _expr(material, unreal.MaterialExpressionMultiply, -1100, -120)
            _connect(material, rain, "", wet, "A")
            _connect(material, response, "", wet, "B")
        except Exception as exc:  # noqa: BLE001
            C.warn(u"  M_OB_Surface: no wetness ({0})".format(exc))
            wet = None
    else:
        C.warn(u"  M_OB_Surface: MPC_OneBraham_Global missing - built without wetness")

    if wet is not None:
        darken = _expr(material, unreal.MaterialExpressionLinearInterpolate, -850, -420)
        _connect(material, _scalar_constant(material, 1.0, -1100, -560), "", darken, "A")
        _connect(material, _scalar_constant(material, 0.55, -1100, -620), "", darken, "B")
        _connect(material, wet, "", darken, "Alpha")
        wet_base = _expr(material, unreal.MaterialExpressionMultiply, -600, -440)
        _connect(material, dirty_base, "", wet_base, "A")
        _connect(material, darken, "", wet_base, "B")
        wet_rough = _expr(material, unreal.MaterialExpressionLinearInterpolate, -600, -280)
        _connect(material, dirty_rough, "", wet_rough, "A")
        _connect(material, _scalar_constant(material, 0.08, -850, -240), "", wet_rough, "B")
        _connect(material, wet, "", wet_rough, "Alpha")
        _connect_property(material, wet_base, "", unreal.MaterialProperty.MP_BASE_COLOR)
        _connect_property(material, wet_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    else:
        _connect_property(material, dirty_base, "", unreal.MaterialProperty.MP_BASE_COLOR)
        _connect_property(material, dirty_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

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

    glass_base = _vector_param(material, "BaseColour", (0.75, 0.80, 0.85, 1.0), -1300, -420)

    opacity = _scalar_param(material, "Opacity", 0.15, -1300, -280)
    rough = _scalar_param(material, "Roughness", 0.04, -1300, -160)

    # Cracks: lines where world-aligned noise crosses zero, faded in by
    # CrackAmount, which AOneBrahamBreakableGlass raises as the pane takes
    # damage. A cracked pane is whiter, rougher and harder to see through, so
    # a window that has been shot is a window a player can read.
    world = _expr(material, unreal.MaterialExpressionWorldPosition, -1900, 300)
    crack_scale = _scalar_param(material, "CrackScale", 0.03, -1900, 440, 0.005, 0.2)
    world_mul = _expr(material, unreal.MaterialExpressionMultiply, -1650, 360)
    _connect(material, world, "", world_mul, "A")
    _connect(material, crack_scale, "", world_mul, "B")
    noise = _expr(material, unreal.MaterialExpressionNoise, -1400, 360)
    for prop, value in (("output_min", -1.0), ("output_max", 1.0), ("levels", 3), ("turbulence", False)):
        try:
            noise.set_editor_property(prop, value)
        except Exception as exc:  # noqa: BLE001
            C.warn(u"  M_OB_Glass noise {0}: {1}".format(prop, exc))
    _connect(material, world_mul, "", noise, "Position")
    line_abs = _expr(material, unreal.MaterialExpressionAbs, -1150, 360)
    _connect(material, noise, "", line_abs, "")
    line_mul = _expr(material, unreal.MaterialExpressionMultiply, -950, 360)
    _connect(material, line_abs, "", line_mul, "A")
    _connect(material, _scalar_constant(material, 14.0, -1150, 460), "", line_mul, "B")
    line_sat = _expr(material, unreal.MaterialExpressionSaturate, -780, 360)
    _connect(material, line_mul, "", line_sat, "")
    lines = _expr(material, unreal.MaterialExpressionOneMinus, -620, 360)
    _connect(material, line_sat, "", lines, "")
    crack_amount = _scalar_param(material, "CrackAmount", 0.0, -780, 480)
    crack = _expr(material, unreal.MaterialExpressionMultiply, -460, 400)
    _connect(material, lines, "", crack, "A")
    _connect(material, crack_amount, "", crack, "B")

    cracked_opacity = _expr(material, unreal.MaterialExpressionLinearInterpolate, -250, -280)
    _connect(material, opacity, "", cracked_opacity, "A")
    _connect(material, _scalar_constant(material, 0.85, -460, -240), "", cracked_opacity, "B")
    _connect(material, crack, "", cracked_opacity, "Alpha")
    _connect_property(material, cracked_opacity, "", unreal.MaterialProperty.MP_OPACITY)

    cracked_rough = _expr(material, unreal.MaterialExpressionLinearInterpolate, -250, -160)
    _connect(material, rough, "", cracked_rough, "A")
    _connect(material, _scalar_constant(material, 0.6, -460, -120), "", cracked_rough, "B")
    _connect(material, crack, "", cracked_rough, "Alpha")
    _connect_property(material, cracked_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    cracked_base = _expr(material, unreal.MaterialExpressionLinearInterpolate, -250, -420)
    _connect(material, glass_base, "", cracked_base, "A")
    _connect(material, _constant3(material, (0.92, 0.94, 0.95), -460, -460), "", cracked_base, "B")
    _connect(material, crack, "", cracked_base, "Alpha")
    _connect_property(material, cracked_base, "", unreal.MaterialProperty.MP_BASE_COLOR)

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

    rgb = _tint(base_key)
    library.set_material_instance_vector_parameter_value(
        material_instance, "BaseColour", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    try:
        library.set_material_instance_scalar_parameter_value(
            material_instance, "Roughness", roughness)
        library.set_material_instance_scalar_parameter_value(
            material_instance, "Metallic", metallic)
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  {0}: {1}".format(material_instance.get_name(), exc))

    if surface_index is not None:
        try:
            library.set_material_instance_scalar_parameter_value(
                material_instance, "SurfaceTypeIndex", float(surface_index))
        except Exception as exc:  # noqa: BLE001
            C.warn(u"  {0}: {1}".format(material_instance.get_name(), exc))

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
        except Exception as exc:  # noqa: BLE001
            C.warn(u"  {0}: {1}".format(material_instance.get_name(), exc))


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

        extras = {}
        if name not in GLASS_SURFACES:
            extras["DirtAmount"] = DIRT.get(name, 0.15)
            extras["WetnessResponse"] = WET_RESPONSE.get(name, 0.0)
        _apply_instance(
            asset, master, colour_key, roughness,
            0.85 if name in METAL_SURFACES else 0.0,
            SURFACE_ORDER.index(name) if name in SURFACE_ORDER else 2,
            extras=extras, physical=physicals.get(name))

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

    C.guard(report, "parameter collection", build_parameter_collection, report)
    C.guard(report, "master materials", _masters, report)

    C.guard(report, "surface instances", build_surface_instances, report, masters, physicals)
    C.guard(report, "brand instances", build_brand_instances, report, masters)

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
                                  unreal.SceneTextureId.PPI_PostProcessInput0)
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  scene texture id not settable: {0}".format(exc))

    stencil = _expr(material, unreal.MaterialExpressionSceneTexture, -900, 0)
    try:
        stencil.set_editor_property("scene_texture_id",
                                    unreal.SceneTextureId.PPI_CustomStencil)
    except Exception as exc:  # noqa: BLE001
        C.warn(u"  custom stencil id not settable: {0}".format(exc))

    target = _scalar_param(material, "StencilTarget", 2.0, -900, 260, 0.0, 255.0)

    # 1 - saturate(|S - target|): 1 exactly on the highlighted object, 0 everywhere
    # else. Written with Abs/Saturate/OneMinus rather than an If node so that it
    # is four nodes a person can read in the editor, not one node they have to
    # open.
    subtract = _expr(material, unreal.MaterialExpressionSubtract, -620, 20)
    _connect(material, stencil, "Color", subtract, "A")
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
    _connect(material, scene, "Color", combined, "A")
    _connect(material, tinted, "", combined, "B")

    _connect_property(material, combined, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    _finish(material, created, "M_OB_Highlight", report)
    return material
