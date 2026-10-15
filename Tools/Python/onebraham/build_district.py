# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_district
========================

Aldgate East around the tower, built from data/aldgate_district.json (made from
OpenStreetMap by district_data.py): every building within 600 m extruded from
its real footprint to its real or storey-derived height, the City cluster on
the skyline, the street network with kerbs and London's road markings, Braham
Street Park and the other green spaces, and the street trees.

Runs inside the environment step, into the persistent level. Geometry is merged
per material through Geometry Script into a handful of static meshes
(/Game/OneBraham/Environment/District/SM_District_*), so fourteen hundred
buildings cost a few draw calls, not fourteen hundred actors.

The facades share one master, M_OB_Facade: storey bands and window bays
computed from world position and the face normal, so a footprint of any shape
gets windows without UVs, and the four families (glass, stone, brick, dark
cladding) are its instances.

Map data (c) OpenStreetMap contributors, ODbL 1.0.
"""

from __future__ import print_function

import json
import math
import os

import unreal

from onebraham import common as C
from onebraham import build_materials as M

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "aldgate_district.json")
FOLDER = C.ENVIRONMENT + "/District"
GROUND_Z = -402.0          # 2 cm under the tower's floor-0 slab, so the slab wins inside
GROUND_HALF_CM = 300000.0  # 6 km of ground: the City towers stand up to 2.6 km out
ROAD_RAISE = 1.5           # carriageway sits a hair over the paving
KERB_W, KERB_H = 25.0, 12.0

# The square of street around the tower the players can walk. The rest of
# Aldgate is scenery: invisible walls stop a sprint up Whitechapel High Street.
PLAY_HALF_X, PLAY_HALF_Y = 6500.0, 6000.0

# M_OB_Facade instances: wall colour, glass colour, bay width, storey height,
# window width and height as a fraction of the bay/storey, wall roughness.
FACADES = {
    # Curtain wall: a fine mullion grid that is nearly all glass.
    # The glass is a sky-blue grey, not black: it has to read as glass even
    # where reflections have nothing to show.
    "Glass": ((0.16, 0.18, 0.20), (0.30, 0.40, 0.48), 150.0, 380.0, 0.92, 0.86, 0.35),
    # Portland stone and render: deep reveals, tall windows.
    "Stone": ((0.62, 0.59, 0.52), (0.07, 0.08, 0.09), 260.0, 360.0, 0.48, 0.58, 0.80),
    # London stock brick: smaller sash windows.
    "Brick": ((0.36, 0.25, 0.17), (0.06, 0.06, 0.07), 240.0, 320.0, 0.40, 0.55, 0.90),
    # Dark rainscreen cladding on the newer blocks.
    "Dark": ((0.17, 0.17, 0.18), (0.20, 0.26, 0.31), 200.0, 330.0, 0.62, 0.62, 0.55),
}

SURFACES = {
    "Paving":  ((0.42, 0.41, 0.39), 0.80),   # York stone and concrete flags
    "Asphalt": ((0.07, 0.07, 0.075), 0.70),
    "Kerb":    ((0.55, 0.54, 0.51), 0.75),   # granite
    "LineWhite": ((0.80, 0.80, 0.78), 0.55),
    "LineYellow": ((0.75, 0.55, 0.05), 0.55),  # double yellows
    "Grass":   ((0.10, 0.17, 0.06), 0.95),
    "Bark":    ((0.10, 0.08, 0.06), 0.95),
    "Leaves":  ((0.09, 0.16, 0.06), 0.85),
    "Roof":    ((0.20, 0.20, 0.21), 0.85),
    "BusRed":  ((0.55, 0.03, 0.02), 0.40),   # London red
    "CabBlack": ((0.02, 0.02, 0.022), 0.25),
    "VehicleGlass": ((0.03, 0.04, 0.05), 0.08),
    "Lamp":    ((0.10, 0.12, 0.11), 0.50),   # Westminster green-black columns

    # One Braham's own finishes, from the reference photographs (Docs/10 §7).
    # Extra parameters ride in a third element.
    "Aluminium": ((0.80, 0.81, 0.83), 0.28, {"Metallic": 1.0}),     # the facade fins
    "BlackCladding": ((0.018, 0.018, 0.02), 0.35, {}),              # reception feature volume
    "Terrazzo": ((0.74, 0.73, 0.70), 0.22, {"ColourVariation": 0.30}),
    "Planter": ((0.10, 0.10, 0.11), 0.60, {"Metallic": 0.6}),       # powder-coated steel
    "TerracePaving": ((0.66, 0.66, 0.64), 0.22, {}),   # wet-look pale porcelain, as photographed
    "Baffle": ((0.025, 0.025, 0.028), 0.55, {}),
    "LightRibbon": ((1.0, 1.0, 1.0), 0.40, {"EmissiveColour": (1.0, 0.97, 0.92), "EmissiveStrength": 120.0}),
    "LightRibbonViolet": ((0.5, 0.2, 1.0), 0.40, {"EmissiveColour": (0.45, 0.15, 1.0), "EmissiveStrength": 160.0}),
    "Timber": ((0.42, 0.29, 0.18), 0.55, {}),

    # The fit-out, from the interior photographs (Docs/10 §7).
    "PanelCream":  ((0.74, 0.71, 0.64), 0.55, {}),    # lift lobby wall panels
    "DeskWhite":   ((0.80, 0.80, 0.78), 0.40, {}),    # benching worktops
    "MeshWhite":   ((0.66, 0.66, 0.66), 0.70, {}),    # task chair mesh
    "BoothPurple": ((0.22, 0.12, 0.42), 0.85, {}),    # high-back booths
    "SofaDark":    ((0.05, 0.06, 0.10), 0.80, {}),    # reception lounge sofa
    "TimberLight": ((0.66, 0.54, 0.40), 0.55, {}),    # cubby shelving, kitchen
    "FeltWhite":   ((0.80, 0.80, 0.80), 0.95, {"EmissiveColour": (1.0, 0.97, 0.92), "EmissiveStrength": 0.6}),    # acoustic discs, edge-lit
    "Porcelain":   ((0.66, 0.66, 0.64), 0.95, {}),    # lift lobby and floor-1 tiles: matt
    "Herringbone": ((0.62, 0.50, 0.36), 0.50, {}),    # reception lounge floor: pale oak
    # The lift lobby's dropped ceiling: matt white with the coves' glow on it.
    "CeilingWhite": ((0.85, 0.84, 0.80), 0.9, {"EmissiveColour": (1.0, 0.95, 0.86), "EmissiveStrength": 14.0}),
}

# Traffic stands on the main roads only; side streets stay clear to walk.
TRAFFIC_CLASSES = ("trunk", "primary", "secondary", "tertiary")


# ---------------------------------------------------------------------------
#  Materials
# ---------------------------------------------------------------------------

def _node(material, cls, x, y):
    return M._expr(material, cls, x, y)


def build_facade_master(report):
    """M_OB_Facade: windows from world position, on any wall of any footprint.

        n       = normalize(VertexNormalWS.xy)
        u       = dot(WorldPosition.xy, (-n.y, n.x)) / BayWidth      along the wall
        v       = WorldPosition.z / StoreyHeight                        up the wall
        window  = (frac(u) < WindowWidth) * (frac(v) < WindowHeight) * wall
        wall    = 1 - |n.z|  (roofs get no windows)
    """
    path = C.MATERIALS + "/M_OB_Facade"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    # Rebuilt from scratch each time: the graph is authored here, not by hand.
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_OB_Facade", C.MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        report.failed("M_OB_Facade", "could not create material")
        return None

    wall = M._vector_param(material, "WallColour", (0.5, 0.5, 0.5, 1.0), -400, -600)
    glass = M._vector_param(material, "GlassColour", (0.1, 0.12, 0.14, 1.0), -400, -460)
    bay = M._scalar_param(material, "BayWidthCm", 220.0, -1900, -300, 50.0, 800.0)
    storey = M._scalar_param(material, "StoreyHeightCm", 350.0, -1900, -100, 200.0, 800.0)
    win_w = M._scalar_param(material, "WindowWidth", 0.5, -1100, -360)
    win_h = M._scalar_param(material, "WindowHeight", 0.55, -1100, -60)
    wall_rough = M._scalar_param(material, "WallRoughness", 0.85, -400, 0)
    lit = M._scalar_param(material, "LitWindowShare", 0.18, -1100, 260)
    glow = M._scalar_param(material, "WindowGlow", 0.6, -400, 300, 0.0, 20.0)

    world = _node(material, unreal.MaterialExpressionWorldPosition, -2300, -260)
    normal = _node(material, unreal.MaterialExpressionVertexNormalWS, -2300, 80)

    # Along-the-wall direction: (-n.y, n.x), from the normal's xy.
    n_x = _node(material, unreal.MaterialExpressionComponentMask, -2100, 40)
    n_x.set_editor_property("r", True); n_x.set_editor_property("g", False)
    n_x.set_editor_property("b", False); n_x.set_editor_property("a", False)
    n_y = _node(material, unreal.MaterialExpressionComponentMask, -2100, 140)
    n_y.set_editor_property("r", False); n_y.set_editor_property("g", True)
    n_y.set_editor_property("b", False); n_y.set_editor_property("a", False)
    n_z = _node(material, unreal.MaterialExpressionComponentMask, -2100, 240)
    n_z.set_editor_property("r", False); n_z.set_editor_property("g", False)
    n_z.set_editor_property("b", True); n_z.set_editor_property("a", False)
    for mask in (n_x, n_y, n_z):
        M._connect(material, normal, "", mask, "")
    neg_y = _node(material, unreal.MaterialExpressionMultiply, -1950, 120)
    M._connect(material, n_y, "", neg_y, "A")
    M._connect(material, M._scalar_constant(material, -1.0, -2100, 320), "", neg_y, "B")
    tangent = _node(material, unreal.MaterialExpressionAppendVector, -1800, 80)
    M._connect(material, neg_y, "", tangent, "A")
    M._connect(material, n_x, "", tangent, "B")

    pos_xy = _node(material, unreal.MaterialExpressionComponentMask, -2100, -320)
    pos_xy.set_editor_property("r", True); pos_xy.set_editor_property("g", True)
    pos_xy.set_editor_property("b", False); pos_xy.set_editor_property("a", False)
    M._connect(material, world, "", pos_xy, "")
    pos_z = _node(material, unreal.MaterialExpressionComponentMask, -2100, -180)
    pos_z.set_editor_property("r", False); pos_z.set_editor_property("g", False)
    pos_z.set_editor_property("b", True); pos_z.set_editor_property("a", False)
    M._connect(material, world, "", pos_z, "")

    along = _node(material, unreal.MaterialExpressionDotProduct, -1650, -260)
    M._connect(material, pos_xy, "", along, "A")
    M._connect(material, tangent, "", along, "B")
    u = _node(material, unreal.MaterialExpressionDivide, -1500, -300)
    M._connect(material, along, "", u, "A")
    M._connect(material, bay, "", u, "B")
    v = _node(material, unreal.MaterialExpressionDivide, -1500, -120)
    M._connect(material, pos_z, "", v, "A")
    M._connect(material, storey, "", v, "B")

    def window_axis(coord, share, y):
        fr = _node(material, unreal.MaterialExpressionFrac, -1300, y)
        M._connect(material, coord, "", fr, "")
        step = _node(material, unreal.MaterialExpressionStep, -1000, y)   # share >= frac -> 1
        M._connect(material, fr, "", step, "Y")
        M._connect(material, share, "", step, "X")
        return step

    in_x = window_axis(u, win_w, -320)
    in_y = window_axis(v, win_h, -120)
    window = _node(material, unreal.MaterialExpressionMultiply, -820, -220)
    M._connect(material, in_x, "", window, "A")
    M._connect(material, in_y, "", window, "B")

    # Walls only: 1 - |n.z|, hardened.
    abs_z = _node(material, unreal.MaterialExpressionAbs, -1950, 240)
    M._connect(material, n_z, "", abs_z, "")
    is_wall = _node(material, unreal.MaterialExpressionOneMinus, -1800, 240)
    M._connect(material, abs_z, "", is_wall, "")
    wall_mask = _node(material, unreal.MaterialExpressionStep, -1650, 240)
    M._connect(material, is_wall, "", wall_mask, "X")
    M._connect(material, M._scalar_constant(material, 0.5, -1800, 340), "", wall_mask, "Y")
    glazed = _node(material, unreal.MaterialExpressionMultiply, -650, -200)
    M._connect(material, window, "", glazed, "A")
    M._connect(material, wall_mask, "", glazed, "B")

    colour = _node(material, unreal.MaterialExpressionLinearInterpolate, -200, -540)
    M._connect(material, wall, "", colour, "A")
    M._connect(material, glass, "", colour, "B")
    M._connect(material, glazed, "", colour, "Alpha")
    M._connect_property(material, colour, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rough = _node(material, unreal.MaterialExpressionLinearInterpolate, -200, -40)
    M._connect(material, wall_rough, "", rough, "A")
    M._connect(material, M._scalar_constant(material, 0.06, -400, 120), "", rough, "B")
    M._connect(material, glazed, "", rough, "Alpha")
    M._connect_property(material, rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    metal = _node(material, unreal.MaterialExpressionMultiply, -200, 100)
    M._connect(material, glazed, "", metal, "A")
    M._connect(material, M._scalar_constant(material, 0.85, -400, 200), "", metal, "B")
    M._connect_property(material, metal, "", unreal.MaterialProperty.MP_METALLIC)

    # A share of windows lit from inside: per-cell noise on (floor(u), floor(v)).
    cell_u = _node(material, unreal.MaterialExpressionFloor, -1300, 200)
    M._connect(material, u, "", cell_u, "")
    cell_v = _node(material, unreal.MaterialExpressionFloor, -1300, 300)
    M._connect(material, v, "", cell_v, "")
    cell = _node(material, unreal.MaterialExpressionAppendVector, -1150, 240)
    M._connect(material, cell_u, "", cell, "A")
    M._connect(material, cell_v, "", cell, "B")
    hashed = _node(material, unreal.MaterialExpressionDotProduct, -1000, 240)
    M._connect(material, cell, "", hashed, "A")
    M._connect(material, M._constant3(material, (12.9898, 78.233, 0.0), -1150, 360), "", hashed, "B")
    # frac(sin(h) * 43758.5): the classic cheap cell hash.
    sine = _node(material, unreal.MaterialExpressionSine, -850, 240)
    M._connect(material, hashed, "", sine, "")
    big = _node(material, unreal.MaterialExpressionMultiply, -720, 240)
    M._connect(material, sine, "", big, "A")
    M._connect(material, M._scalar_constant(material, 43758.5, -850, 340), "", big, "B")
    rnd = _node(material, unreal.MaterialExpressionFrac, -600, 240)
    M._connect(material, big, "", rnd, "")
    lit_cell = _node(material, unreal.MaterialExpressionStep, -480, 240)
    M._connect(material, rnd, "", lit_cell, "X")
    M._connect(material, lit, "", lit_cell, "Y")
    lit_window = _node(material, unreal.MaterialExpressionMultiply, -340, 260)
    M._connect(material, lit_cell, "", lit_window, "A")
    M._connect(material, glazed, "", lit_window, "B")
    emissive = _node(material, unreal.MaterialExpressionMultiply, -200, 280)
    M._connect(material, lit_window, "", emissive, "A")
    M._connect(material, glow, "", emissive, "B")
    warm = _node(material, unreal.MaterialExpressionMultiply, -80, 300)
    M._connect(material, emissive, "", warm, "A")
    M._connect(material, M._constant3(material, (1.0, 0.82, 0.6), -200, 400), "", warm, "B")
    M._connect_property(material, warm, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    M._finish(material, True, "M_OB_Facade", report)
    return material


def _instance(name, master, vectors, scalars, report):
    folder = C.MATERIALS_INSTANCES
    path = folder + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        instance = unreal.EditorAssetLibrary.load_asset(path)
    else:
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        report.failed(name, "could not create material instance")
        return None
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(instance, master)
    for key, rgb in vectors.items():
        mel.set_material_instance_vector_parameter_value(instance, key, unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    for key, value in scalars.items():
        mel.set_material_instance_scalar_parameter_value(instance, key, value)
    unreal.EditorAssetLibrary.save_asset(path)
    return instance


def build_district_materials(report):
    facade = build_facade_master(report)
    surface = unreal.EditorAssetLibrary.load_asset(C.MATERIALS + "/M_OB_Surface")
    out = {}
    if facade is not None:
        for family, (wall, glass, bay, storey, ww, wh, rough) in FACADES.items():
            out["Facade_" + family] = _instance(
                "MI_OB_Facade_" + family, facade,
                {"WallColour": wall, "GlassColour": glass},
                {"BayWidthCm": bay, "StoreyHeightCm": storey, "WindowWidth": ww,
                 "WindowHeight": wh, "WallRoughness": rough,
                 "LitWindowShare": 0.12 if family == "Glass" else 0.2,
                 "WindowGlow": 0.8}, report)
    if surface is not None:
        for name, spec in SURFACES.items():
            rgb, rough = spec[0], spec[1]
            extra = spec[2] if len(spec) > 2 else {}
            vectors = {"BaseColour": rgb}
            scalars = {"Roughness": rough, "ColourVariation": 0.18, "WearScaleCm": 900.0}
            for key, value in extra.items():
                if isinstance(value, tuple):
                    vectors[key] = value
                else:
                    scalars[key] = value
            out[name] = _instance("MI_OB_District_" + name, surface, vectors, scalars, report)
            # Paving and asphalt carry real texture sets (CC0, SourceArt/) on
            # the textured master, still in this palette's colours.
            spec_tex = M.DISTRICT_TEXTURE_SETS.get(name)
            textured = unreal.EditorAssetLibrary.load_asset(C.MATERIALS + "/M_OB_SurfaceTextured")
            if spec_tex and textured is not None and out[name] is not None:
                if M.apply_texture_set(report, out[name], textured, spec_tex, rgb):
                    unreal.EditorAssetLibrary.save_asset(C.MATERIALS_INSTANCES + "/MI_OB_District_" + name)
    return out


# ---------------------------------------------------------------------------
#  Geometry
# ---------------------------------------------------------------------------

G = None


def _geometry_script():
    global G
    if G is None:
        G = {
            "prim": getattr(unreal, "GeometryScript_Primitives", None),
            "assets": getattr(unreal, "GeometryScript_NewAssetUtils", None),
            "normals": getattr(unreal, "GeometryScript_Normals", None),
        }
    return G if all(G.values()) else None


def _options():
    options = unreal.GeometryScriptPrimitiveOptions()
    try:
        options.set_editor_property("polygroup_mode", unreal.GeometryScriptPrimitivePolygroupMode.PER_FACE)
    except Exception:  # noqa: BLE001 - the default grouping is fine
        pass
    return options


def _xf(x, y, z, yaw_deg=0.0):
    return unreal.Transform(unreal.Vector(x, y, z), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw_deg),
                            unreal.Vector(1.0, 1.0, 1.0))


def _ccw(ring):
    area = 0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(ring, ring[1:] + ring[:1]))
    return ring if area > 0.0 else list(reversed(ring))


class Batch(object):
    """One dynamic mesh per material, saved as one static mesh asset."""

    def __init__(self):
        self.meshes = {}
        self.counts = {}

    def mesh(self, key):
        if key not in self.meshes:
            self.meshes[key] = unreal.DynamicMesh()
            self.counts[key] = 0
        self.counts[key] += 1
        return self.meshes[key]

    def extrude(self, key, ring, z, height):
        points = [unreal.Vector2D(x, y) for x, y in _ccw(ring)]
        G["prim"].append_simple_extrude_polygon(self.mesh(key), _options(), _xf(0.0, 0.0, z),
                                                points, height, 0, True)

    def box(self, key, x, y, z, size_x, size_y, size_z, yaw=0.0):
        G["prim"].append_box(self.mesh(key), _options(), _xf(x, y, z, yaw), size_x, size_y, size_z,
                             0, 0, 0, unreal.GeometryScriptPrimitiveOriginMode.BASE)

    def cylinder(self, key, x, y, z, radius, height, sides=8):
        G["prim"].append_cylinder(self.mesh(key), _options(), _xf(x, y, z), radius, height, sides, 0, True,
                                  unreal.GeometryScriptPrimitiveOriginMode.BASE)

    def sphere(self, key, x, y, z, radius):
        G["prim"].append_sphere_lat_long(self.mesh(key), _options(), _xf(x, y, z), radius, 6, 8,
                                         unreal.GeometryScriptPrimitiveOriginMode.CENTER)

    def save(self, materials, report):
        placed = []
        if not unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
            unreal.EditorAssetLibrary.make_directory(FOLDER)
        for key, mesh in sorted(self.meshes.items()):
            G["normals"].set_per_face_normals(mesh)
            path = "{0}/SM_District_{1}".format(FOLDER, key)
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                unreal.EditorAssetLibrary.delete_asset(path)
            options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
            for prop, value in (("enable_recompute_normals", False), ("enable_recompute_tangents", True),
                                ("enable_nanite", False), ("enable_collision", True)):
                try:
                    options.set_editor_property(prop, value)
                except Exception:  # noqa: BLE001
                    pass
            result = G["assets"].create_new_static_mesh_asset_from_mesh(mesh, path, options)
            static_mesh = result[0] if isinstance(result, tuple) else result
            if static_mesh is None:
                report.failed(path, "Geometry Script could not create the mesh asset")
                continue
            try:
                body = static_mesh.get_editor_property("body_setup")
                body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            except Exception as exc:  # noqa: BLE001
                report.failed(path + " collision", str(exc))
            material = materials.get(key)
            if material is not None:
                static_mesh.set_material(0, material)
            unreal.EditorAssetLibrary.save_asset(path)
            actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
                unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
            actor.static_mesh_component.set_static_mesh(static_mesh)
            actor.set_actor_label("District_" + key)
            placed.append({"mesh": key, "parts": self.counts[key]})
            report.created_asset(path)
        return placed


def _segments_markings(batch, segments, width):
    """Kerbs either side, a dashed centre line, double yellows by the kerb."""
    for (ax, ay), (bx, by) in segments:
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy)
        if length < 50.0:
            continue
        yaw = math.degrees(math.atan2(dy, dx))
        ux, uy = dx / length, dy / length
        px, py = -uy, ux
        mx, my = (ax + bx) / 2.0, (ay + by) / 2.0
        # Overlap the joints so bends don't open gaps.
        batch.box("Asphalt", mx, my, GROUND_Z, length + width * 0.5, width, ROAD_RAISE, yaw)
        if width < 600.0:
            continue
        for side in (-1.0, 1.0):
            off = side * (width / 2.0 + KERB_W / 2.0)
            batch.box("Kerb", mx + px * off, my + py * off, GROUND_Z, length, KERB_W, KERB_H, yaw)
            for lane in (0.0, 14.0):
                o = side * (width / 2.0 - 30.0 - lane)
                batch.box("LineYellow", mx + px * o, my + py * o, GROUND_Z + ROAD_RAISE, length, 8.0, 0.6, yaw)
        dash, gap = 300.0, 600.0
        t = gap / 2.0
        while t + dash < length:
            cx, cy = ax + ux * (t + dash / 2.0), ay + uy * (t + dash / 2.0)
            batch.box("LineWhite", cx, cy, GROUND_Z + ROAD_RAISE, dash, 10.0, 0.6, yaw)
            t += dash + gap


def _street_life(batch, roads):
    """Lamp columns every 25 m, and standing traffic on the main roads: red
    double-deckers and black cabs, nose to tail in the nearside lane, as Aldgate
    is at most hours. Deterministic: the same build gives the same street."""
    vehicle = 0
    for road in roads:
        width = float(road["w"])
        main = road["class"] in TRAFFIC_CLASSES
        for (ax, ay), (bx, by) in road["segments"]:
            dx, dy = bx - ax, by - ay
            length = math.hypot(dx, dy)
            if length < 400.0:
                continue
            ux, uy = dx / length, dy / length
            px, py = -uy, ux
            yaw = math.degrees(math.atan2(dy, dx))
            if width >= 600.0:
                t = 1250.0
                while t < length:
                    side = 1.0 if int(t // 2500.0) % 2 else -1.0
                    off = side * (width / 2.0 + 60.0)
                    lx, ly = ax + ux * t + px * off, ay + uy * t + py * off
                    batch.cylinder("Lamp", lx, ly, GROUND_Z, 9.0, 800.0, 6)
                    batch.box("Lamp", lx - px * side * 60.0, ly - py * side * 60.0, GROUND_Z + 790.0,
                              30.0, 140.0, 14.0, yaw + 90.0)
                    t += 2500.0
            if not main or length < 1500.0:
                continue
            t = 600.0
            while t < length - 600.0:
                vehicle += 1
                lane = -1.0 if vehicle % 3 else 1.0
                off = lane * width * 0.25
                cx, cy = ax + ux * t + px * off, ay + uy * t + py * off
                heading = yaw if lane < 0 else yaw + 180.0
                if vehicle % 4 == 0:
                    # Routemaster-shape double-decker: two decks, a glazed band each.
                    batch.box("BusRed", cx, cy, GROUND_Z + 30.0, 1100.0, 255.0, 410.0, heading)
                    for deck_z in (130.0, 330.0):
                        batch.box("VehicleGlass", cx, cy, GROUND_Z + deck_z, 1060.0, 259.0, 80.0, heading)
                    t += 1700.0
                else:
                    # Black cab: body, cabin, glazing.
                    batch.box("CabBlack", cx, cy, GROUND_Z + 25.0, 460.0, 180.0, 90.0, heading)
                    batch.box("CabBlack", cx, cy, GROUND_Z + 115.0, 280.0, 170.0, 70.0, heading)
                    batch.box("VehicleGlass", cx, cy, GROUND_Z + 120.0, 284.0, 174.0, 50.0, heading)
                    t += 900.0 + (vehicle % 5) * 350.0
    return vehicle


def build_district(report, materials_hint=None):
    """Places Aldgate East around the tower. Returns a manifest summary."""
    if _geometry_script() is None:
        report.failed("district", "Geometry Script is not loaded - enable the GeometryScripting plugin")
        return None
    if not os.path.exists(DATA):
        report.failed("district", u"{0} is missing - run district_data.py".format(DATA))
        return None
    with open(DATA) as handle:
        data = json.load(handle)

    materials = build_district_materials(report)
    build_ad_screens(report)
    batch = Batch()

    # Ground: paving everywhere, the street grid laid over it.
    batch.box("Paving", 0.0, 0.0, GROUND_Z - 30.0, GROUND_HALF_CM * 2.0, GROUND_HALF_CM * 2.0, 30.0)

    for building in data["buildings"]:
        family = building["facade"]
        batch.extrude("Facade_" + family, building["ring"], GROUND_Z, float(building["h"]))

    for road in data["roads"]:
        _segments_markings(batch, road["segments"], float(road["w"]))

    for park in data["parks"]:
        batch.extrude("Grass", park["ring"], GROUND_Z, 4.0)

    for index, (x, y) in enumerate(data["trees"]):
        # London planes: tall clear trunk, broad crown; sizes vary a little.
        scale = 0.8 + 0.4 * ((index * 7919) % 100) / 100.0
        batch.cylinder("Bark", x, y, GROUND_Z, 14.0 * scale, 420.0 * scale)
        batch.sphere("Leaves", x, y, GROUND_Z + 620.0 * scale, 280.0 * scale)
        batch.sphere("Leaves", x + 120.0 * scale, y + 60.0 * scale, GROUND_Z + 520.0 * scale, 200.0 * scale)

    vehicles = _street_life(batch, data["roads"])

    placed = batch.save(materials, report)

    # The walkable street: invisible walls round the forecourt and Braham Street.
    for name, x, y, sx, sy in (("N", 0.0, PLAY_HALF_Y, PLAY_HALF_X * 2.0, 50.0),
                               ("S", 0.0, -PLAY_HALF_Y, PLAY_HALF_X * 2.0, 50.0),
                               ("E", PLAY_HALF_X, 0.0, 50.0, PLAY_HALF_Y * 2.0),
                               ("W", -PLAY_HALF_X, 0.0, 50.0, PLAY_HALF_Y * 2.0)):
        wall = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
            unreal.BlockingVolume, unreal.Vector(x, y, GROUND_Z + 1500.0))
        if wall is None:
            report.failed("play bounds", "could not spawn " + name)
            continue
        wall.set_actor_scale3d(unreal.Vector(sx / 200.0, sy / 200.0, 3000.0 / 200.0))
        wall.set_actor_label("District_PlayBounds_" + name)

    C.log(u"  district: {0} buildings near, {1} city to 1.8 km, {2} on the skyline, {3} roads, "
          u"{4} parks, {5} trees, {6} vehicles ({7})".format(
              data["counts"]["buildings"], data["counts"].get("city", 0), data["counts"]["skyline"],
              data["counts"]["roads"], data["counts"]["parks"], data["counts"]["trees"], vehicles,
              data["attribution"]))
    return {"meshes": placed, "counts": data["counts"], "vehicles": vehicles, "frame": data["frame"],
            "attribution": data["attribution"]}


# ---------------------------------------------------------------------------
#  Sky
# ---------------------------------------------------------------------------
# A photographed sky over the modelled city: a CC0 "pure sky" HDRI (no ground
# in it - the ground is Aldgate) on a dome, with the scene's sun aimed at the
# sun in the photograph. Fetched by fetch_open_assets.py into SourceArt/.
SKY_FOLDER = C.ENVIRONMENT + "/Sky"
SKY_PRESETS = {
    # preset: (asset id, dome luminance scale, light colour)
    "day":  ("kloofendal_48d_partly_cloudy_puresky", 2500.0, (255, 246, 235)),
    "dusk": ("belfast_sunset_puresky", 900.0, (255, 196, 140)),
}
SKY_PRESET = "day"
# Where an equirectangular image's columns land once imported as a cubemap:
# a sun at image azimuth A sits at world yaw A + 90. Measured, not assumed - the
# day sky's sun (A = 214.1, elevation 47.8) was found by an eight-way capture
# scan at yaw -56, elevation 47.6.
SKY_AZIMUTH_TO_YAW = 90.0
# The photograph's sun is clamped in the dome: unclipped, it would reach the
# skylight as tens of thousands of unshadowed lux on top of the real sun.
SKY_SUN_CLAMP = 40.0


def _project_root():
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))


def build_sky_material(report, cube):
    path = C.MATERIALS + "/M_OB_SkyDome"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_OB_SkyDome", C.MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        report.failed("M_OB_SkyDome", "could not create material")
        return None
    for prop, value in (("shading_model", unreal.MaterialShadingModel.MSM_UNLIT), ("two_sided", True),
                        ("is_sky", True)):
        try:
            material.set_editor_property(prop, value)
        except Exception as exc:  # noqa: BLE001
            report.failed("M_OB_SkyDome." + prop, str(exc))
    camera = _node(material, unreal.MaterialExpressionCameraVectorWS, -900, 0)
    flip = _node(material, unreal.MaterialExpressionMultiply, -720, 0)
    M._connect(material, camera, "", flip, "A")
    M._connect(material, M._scalar_constant(material, -1.0, -900, 120), "", flip, "B")
    # A pure-sky photograph is black below the horizon. Past the mapped city
    # that read as a dark sea, so directions below the horizon look up at it
    # instead: z is lifted a few degrees above zero and the vector renormalised.
    xy = _node(material, unreal.MaterialExpressionComponentMask, -720, 160)
    for channel, on in (("r", True), ("g", True), ("b", False), ("a", False)):
        xy.set_editor_property(channel, on)
    M._connect(material, flip, "", xy, "")
    zed = _node(material, unreal.MaterialExpressionComponentMask, -720, 260)
    for channel, on in (("r", False), ("g", False), ("b", True), ("a", False)):
        zed.set_editor_property(channel, on)
    M._connect(material, flip, "", zed, "")
    lifted = _node(material, unreal.MaterialExpressionMax, -600, 260)
    M._connect(material, zed, "", lifted, "A")
    # These photographs fade to black up to ~5 degrees above the true horizon.
    # Sample no lower than ~5 degrees, and blend into haze below ~12.5, so the
    # repeated row under the fold stays hidden in the haze.
    M._connect(material, M._scalar_constant(material, 0.09, -720, 360), "", lifted, "B")
    joined = _node(material, unreal.MaterialExpressionAppendVector, -480, 200)
    M._connect(material, xy, "", joined, "A")
    M._connect(material, lifted, "", joined, "B")
    direction = _node(material, unreal.MaterialExpressionNormalize, -380, 200)
    M._connect(material, joined, "", direction, "")
    sample = _node(material, unreal.MaterialExpressionTextureSampleParameterCube, -520, 0)
    sample.set_editor_property("parameter_name", "SkyCube")
    if cube is not None:
        sample.set_editor_property("texture", cube)
    M._connect(material, direction, "", sample, "UVs")
    # The horizon: a London haze the height fog matches, rising into the
    # photograph over the bottom 12.5 degrees of sky.
    haze_alpha = _node(material, unreal.MaterialExpressionDivide, -600, 420)
    M._connect(material, zed, "", haze_alpha, "A")
    M._connect(material, M._scalar_constant(material, 0.22, -720, 460), "", haze_alpha, "B")
    haze_alpha_sat = _node(material, unreal.MaterialExpressionSaturate, -480, 420)
    M._connect(material, haze_alpha, "", haze_alpha_sat, "")
    haze = M._vector_param(material, "HazeColour", (0.78, 0.84, 0.92, 1.0), -480, 520)
    blended = _node(material, unreal.MaterialExpressionLinearInterpolate, -360, 380)
    M._connect(material, haze, "", blended, "A")
    M._connect(material, sample, "RGB", blended, "B")
    M._connect(material, haze_alpha_sat, "", blended, "Alpha")
    clamp = _node(material, unreal.MaterialExpressionMin, -300, 0)
    M._connect(material, blended, "", clamp, "A")
    M._connect(material, M._scalar_param(material, "SunClamp", SKY_SUN_CLAMP, -520, 200, 1.0, 1000.0), "", clamp, "B")
    scale = _node(material, unreal.MaterialExpressionMultiply, -120, 0)
    M._connect(material, clamp, "", scale, "A")
    M._connect(material, M._scalar_param(material, "SkyLuminance", 2500.0, -300, 200, 1.0, 20000.0), "", scale, "B")
    M._connect_property(material, scale, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    M._finish(material, True, "M_OB_SkyDome", report)
    return material


def build_sky(report, preset=None):
    """Imports the preset's sky, places the dome, and returns the sun as
    (pitch, yaw, colour) for the directional light - or None to keep the
    procedural sky (no fetched assets on this machine)."""
    preset = preset or SKY_PRESET
    asset_id, luminance, colour = SKY_PRESETS[preset]
    manifest_path = os.path.join(_project_root(), "SourceArt", "open_assets.json")
    if not os.path.exists(manifest_path):
        report.skipped_asset(SKY_FOLDER, "SourceArt/open_assets.json missing - run fetch_open_assets.py")
        return None
    with open(manifest_path) as handle:
        entry = json.load(handle).get(asset_id)
    if entry is None:
        report.skipped_asset(SKY_FOLDER, asset_id + " not fetched")
        return None

    if not unreal.EditorAssetLibrary.does_directory_exist(SKY_FOLDER):
        unreal.EditorAssetLibrary.make_directory(SKY_FOLDER)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(_project_root(), entry["path"]))
    task.set_editor_property("destination_path", SKY_FOLDER)
    task.set_editor_property("destination_name", "T_Sky_" + preset.capitalize())
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    cube = unreal.EditorAssetLibrary.load_asset("{0}/T_Sky_{1}".format(SKY_FOLDER, preset.capitalize()))
    if cube is None:
        report.failed(SKY_FOLDER, "could not import " + entry["path"])
        return None

    material = build_sky_material(report, cube)
    if material is not None:
        haze = (0.78, 0.84, 0.92) if preset == "day" else (0.95, 0.62, 0.42)
        instance = _instance("MI_OB_SkyDome_" + preset.capitalize(), material, {"HazeColour": haze},
                             {"SkyLuminance": luminance, "SunClamp": SKY_SUN_CLAMP}, report)
        dome = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
            unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
        component = dome.static_mesh_component
        component.set_static_mesh(unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere.Sphere"))
        component.set_material(0, instance or material)
        component.set_editor_property("cast_shadow", False)
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        dome.set_actor_scale3d(unreal.Vector(10000.0, 10000.0, 10000.0))   # 10 km across
        dome.set_actor_label("Sky_Dome_" + preset.capitalize())

    yaw = entry["sun_azimuth_deg"] + SKY_AZIMUTH_TO_YAW
    # The light travels away from the sun: opposite yaw, downward pitch.
    return {"pitch": -entry["sun_elevation_deg"], "yaw": yaw + 180.0, "colour": colour,
            "sky": asset_id, "licence": entry["licence"]}


# ---------------------------------------------------------------------------
#  OLED advert screens
# ---------------------------------------------------------------------------
# One Braham's reception and the top of its escalators carry big OLED screens
# running adverts. The game's are fictional - the building is "OB" - and
# animated in the material: a drifting two-colour field, a light sweep, and a
# swap of the two colours every few seconds so the "advert" changes.

def build_ad_screen_material(report):
    """M_OB_AdScreen: an OLED field. True black, three soft aurora bands in the
    slide's two colours drifting at their own speeds and widths, a slow
    breathing glow behind them and a vignette toward the bezel. AOneBrahamDisplay
    drives ColourA, ColourB and Brightness."""
    path = C.MATERIALS + "/M_OB_AdScreen"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_OB_AdScreen", C.MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        report.failed("M_OB_AdScreen", "could not create material")
        return None
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    def mask(src, channel, x, y):
        node = _node(material, unreal.MaterialExpressionComponentMask, x, y)
        for c in ("r", "g", "b", "a"):
            node.set_editor_property(c, c == channel)
        M._connect(material, src, "", node, "")
        return node

    def op(cls, a, b, x, y):
        node = _node(material, cls, x, y)
        M._connect(material, a, "", node, "A")
        M._connect(material, b, "", node, "B")
        return node

    def k(value, x, y):
        return M._scalar_constant(material, value, x, y)

    uv = _node(material, unreal.MaterialExpressionTextureCoordinate, -2200, 0)
    u = mask(uv, "r", -2050, -60)
    v = mask(uv, "g", -2050, 60)
    time = _node(material, unreal.MaterialExpressionTime, -2200, 300)
    t = op(unreal.MaterialExpressionMultiply, time, M._scalar_param(material, "Speed", 0.35, -2200, 420, 0.0, 5.0), -2050, 300)

    def band(freq, speed, phase, centre, wobble, width, y):
        # centre(u, t) = centre + wobble * sin(freq u + speed t + phase), then
        # a soft ribbon: (1 - |v - centre| / width)^3, clamped.
        arg = op(unreal.MaterialExpressionAdd,
                 op(unreal.MaterialExpressionMultiply, u, k(freq, -1900, y - 40), -1750, y - 40),
                 op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionMultiply, t, k(speed, -1900, y + 40), -1750, y + 40),
                    k(phase, -1750, y + 100), -1600, y + 40), -1450, y)
        wave = _node(material, unreal.MaterialExpressionSine, -1300, y)
        M._connect(material, arg, "", wave, "")
        c = op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionMultiply, wave, k(wobble, -1300, y + 80), -1150, y),
               k(centre, -1150, y + 80), -1000, y)
        dist = _node(material, unreal.MaterialExpressionAbs, -850, y)
        M._connect(material, op(unreal.MaterialExpressionSubtract, v, c, -900, y + 60), "", dist, "")
        soft = _node(material, unreal.MaterialExpressionSaturate, -600, y)
        M._connect(material, op(unreal.MaterialExpressionSubtract, k(1.0, -750, y + 60),
                                op(unreal.MaterialExpressionDivide, dist, k(width, -750, y + 120), -700, y), -650, y + 30), "", soft, "")
        cube = op(unreal.MaterialExpressionMultiply, soft, op(unreal.MaterialExpressionMultiply, soft, soft, -500, y + 60), -400, y)
        return cube

    b1 = band(5.0, 0.9, 0.0, 0.42, 0.16, 0.26, -400)
    b2 = band(3.2, -0.6, 2.1, 0.60, 0.20, 0.34, 0)
    b3 = band(8.5, 1.4, 4.0, 0.50, 0.10, 0.10, 400)

    col_a = M._vector_param(material, "ColourA", (0.45, 0.10, 1.0, 1.0), -600, -700)
    col_b = M._vector_param(material, "ColourB", (0.05, 0.45, 1.0, 1.0), -600, -560)
    field = op(unreal.MaterialExpressionAdd,
               op(unreal.MaterialExpressionMultiply, col_a, b1, -300, -500),
               op(unreal.MaterialExpressionAdd,
                  op(unreal.MaterialExpressionMultiply, col_b, b2, -300, -380),
                  op(unreal.MaterialExpressionMultiply,
                     op(unreal.MaterialExpressionAdd, col_a, col_b, -450, 380),
                     op(unreal.MaterialExpressionMultiply, b3, k(0.6, -450, 480), -300, 440), -200, 400), -150, -380),
               -50, -450)

    # Breathing glow and vignette: 0.06 + 0.04 sin(0.5 t) of colour A, all
    # under (1 - edge falloff) so the field darkens toward the bezel.
    breath = _node(material, unreal.MaterialExpressionSine, -600, 700)
    M._connect(material, op(unreal.MaterialExpressionMultiply, t, k(0.5, -750, 760), -700, 700), "", breath, "")
    glow = op(unreal.MaterialExpressionMultiply, col_a,
              op(unreal.MaterialExpressionAdd, k(0.06, -450, 760),
                 op(unreal.MaterialExpressionMultiply, breath, k(0.04, -450, 820), -350, 740), -250, 740), -150, 700)
    du = _node(material, unreal.MaterialExpressionAbs, -1900, 900)
    M._connect(material, op(unreal.MaterialExpressionSubtract, u, k(0.5, -2050, 960), -2000, 900), "", du, "")
    dv = _node(material, unreal.MaterialExpressionAbs, -1900, 1000)
    M._connect(material, op(unreal.MaterialExpressionSubtract, v, k(0.5, -2050, 1060), -2000, 1000), "", dv, "")
    edge = _node(material, unreal.MaterialExpressionSaturate, -1500, 950)
    M._connect(material, op(unreal.MaterialExpressionMultiply,
                            op(unreal.MaterialExpressionSubtract, op(unreal.MaterialExpressionMax, du, dv, -1750, 950),
                               k(0.36, -1750, 1050), -1650, 950), k(7.0, -1650, 1050), -1550, 950), "", edge, "")
    vignette = op(unreal.MaterialExpressionSubtract, k(1.0, -1400, 1000), op(unreal.MaterialExpressionMultiply, edge, k(0.65, -1400, 1060), -1350, 950), -1250, 950)
    lit = op(unreal.MaterialExpressionMultiply, op(unreal.MaterialExpressionAdd, field, glow, 50, -200), vignette, 200, -100)
    out = op(unreal.MaterialExpressionMultiply, lit, M._scalar_param(material, "Brightness", 60.0, 50, 0, 0.0, 500.0), 350, -100)
    M._connect_property(material, out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    M._finish(material, True, "M_OB_AdScreen", report)
    return material


AD_SCREENS = {
    # name: (colour A, colour B, speed)
    "Reception": ((0.45, 0.10, 1.0), (0.05, 0.45, 1.0), 0.5),
    "Escalator": ((1.0, 0.25, 0.45), (0.35, 0.10, 0.9), 0.7),
}


def build_ad_screens(report):
    master = build_ad_screen_material(report)
    out = {}
    if master is None:
        return out
    for name, (a, b, speed) in AD_SCREENS.items():
        out[name] = _instance("MI_OB_AdScreen_" + name, master, {"ColourA": a, "ColourB": b},
                              {"Speed": speed}, report)
    return out
