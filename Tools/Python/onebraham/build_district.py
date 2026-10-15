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
gets windows without UVs - with a ground storey, sills, frames and glazing
bars, floor-line bands, blinds and lit rooms. The four families (glass, stone,
brick, dark cladding) split into eight variants by height and a hash of the
OSM id (district_data.facade_variant), each an instance with its own storey
height, bay and window pattern. Larger roofs carry plant (lift overruns,
air-handling units, condensers, tower plant screens), and the facades facing
the near shopping streets - Leman Street, Alie Street, Commercial Road,
Whitechapel High Street and the rest of district_data.SHOPFRONT_STREETS - get
shopfronts: riser, pilasters, lit glazing, door, fascia, cornice. All of it is
batched into the same per-material meshes.

Map data (c) OpenStreetMap contributors, ODbL 1.0.
"""

from __future__ import print_function

import json
import math
import os

import unreal

from onebraham import common as C
from onebraham import build_materials as M
from onebraham import district_data as D

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "aldgate_district.json")
FOLDER = C.ENVIRONMENT + "/District"
GROUND_Z = -402.0          # 2 cm under the tower's floor-0 slab, so the slab wins inside
GROUND_HALF_CM = 300000.0  # 6 km of ground: the City towers stand up to 2.6 km out
ROAD_RAISE = 1.5           # carriageway sits a hair over the paving
KERB_W, KERB_H = 25.0, 12.0

# The square of street around the tower the players can walk. The rest of
# Aldgate is scenery: invisible walls stop a sprint up Whitechapel High Street.
PLAY_HALF_X, PLAY_HALF_Y = 6500.0, 6000.0

# M_OB_Facade instances, one per district_data.FACADE_VARIANTS entry: London's
# window patterns, measured off the street rather than invented.
#   wall/glass/frame/plinth/roof   colours (linear albedo)
#   bay, storey, ground            bay width, storey height, ground storey (cm)
#   ww, wh, sill                   window width and height as a share of the
#                                  bay and storey; sill height as a share of it
#   ground_ww                      ground-floor opening width, share of the bay
#   mullions, transoms, bar        glazing bars across and up each window, and
#                                  their width (cm); the frame is drawn too
#   band, band_tint                a band at every floor line (cm), and how it
#                                  shifts the wall (string course, slab edge)
#   rough, frame_metal             wall roughness, frame metalness
#   lit, blinds, glow              share of windows lit from inside, share with
#                                  blinds down, and the lit windows' luminance
FACADES = {
    # Unitised curtain wall: a fine aluminium grid that is nearly all glass, a
    # double-height lobby. The glass is a sky-blue grey, not black: it has to
    # read as glass even where reflections have nothing to show.
    "Glass": dict(wall=(0.16, 0.18, 0.20), glass=(0.30, 0.40, 0.48), frame=(0.55, 0.56, 0.58),
                  plinth=(0.10, 0.11, 0.12), roof=(0.24, 0.24, 0.25),
                  bay=150.0, storey=400.0, ground=600.0, ww=0.94, wh=0.80, sill=0.10, ground_ww=0.94,
                  mullions=0, transoms=0, bar=6.0, band=0.0, band_tint=1.0,
                  rough=0.35, frame_metal=0.9, lit=0.35, blinds=0.15, glow=120.0),
    # Ribbon glazing over pale spandrel panels: the 1960s-90s office slab.
    "GlassRibbon": dict(wall=(0.42, 0.43, 0.44), glass=(0.22, 0.28, 0.33), frame=(0.12, 0.12, 0.13),
                        plinth=(0.14, 0.14, 0.15), roof=(0.22, 0.22, 0.23),
                        bay=300.0, storey=370.0, ground=450.0, ww=1.0, wh=0.58, sill=0.28, ground_ww=0.9,
                        mullions=1, transoms=0, bar=8.0, band=0.0, band_tint=1.0,
                        rough=0.5, frame_metal=0.7, lit=0.3, blinds=0.25, glow=100.0),
    # Portland stone and render: tall painted sashes, a string course at each
    # floor, a darker rusticated base, slate behind the parapet.
    "Stone": dict(wall=(0.62, 0.59, 0.52), glass=(0.07, 0.08, 0.09), frame=(0.72, 0.72, 0.69),
                  plinth=(0.40, 0.38, 0.34), roof=(0.12, 0.12, 0.13),
                  bay=260.0, storey=360.0, ground=450.0, ww=0.46, wh=0.58, sill=0.20, ground_ww=0.7,
                  mullions=1, transoms=1, bar=5.0, band=25.0, band_tint=1.12,
                  rough=0.8, frame_metal=0.0, lit=0.18, blinds=0.3, glow=60.0),
    # Stone-clad modern office: wide windows in dark aluminium, a granite base.
    "StoneOffice": dict(wall=(0.66, 0.62, 0.55), glass=(0.12, 0.15, 0.18), frame=(0.10, 0.11, 0.12),
                        plinth=(0.13, 0.13, 0.14), roof=(0.22, 0.22, 0.23),
                        bay=180.0, storey=375.0, ground=480.0, ww=0.66, wh=0.62, sill=0.20, ground_ww=0.8,
                        mullions=0, transoms=1, bar=6.0, band=0.0, band_tint=1.0,
                        rough=0.7, frame_metal=0.7, lit=0.3, blinds=0.2, glow=100.0),
    # London stock brick terrace: two-over-two sashes, a stuccoed shop storey.
    "Brick": dict(wall=(0.36, 0.25, 0.17), glass=(0.06, 0.06, 0.07), frame=(0.74, 0.74, 0.71),
                  plinth=(0.58, 0.55, 0.49), roof=(0.11, 0.11, 0.12),
                  bay=240.0, storey=310.0, ground=360.0, ww=0.40, wh=0.56, sill=0.22, ground_ww=0.7,
                  mullions=1, transoms=1, bar=5.0, band=0.0, band_tint=1.0,
                  rough=0.9, frame_metal=0.0, lit=0.15, blinds=0.35, glow=50.0),
    # Red-stock warehouse or mansion block: multi-pane black steel windows,
    # blue-brick bands and base.
    "BrickWarehouse": dict(wall=(0.31, 0.18, 0.12), glass=(0.08, 0.09, 0.10), frame=(0.05, 0.05, 0.05),
                           plinth=(0.12, 0.08, 0.07), roof=(0.20, 0.20, 0.21),
                           bay=290.0, storey=340.0, ground=400.0, ww=0.54, wh=0.60, sill=0.18, ground_ww=0.7,
                           mullions=2, transoms=2, bar=4.0, band=22.0, band_tint=0.7,
                           rough=0.9, frame_metal=0.5, lit=0.2, blinds=0.2, glow=60.0),
    # Post-war and new-build flats: yellow stock between pale concrete slab
    # edges, white uPVC.
    "BrickBlock": dict(wall=(0.46, 0.38, 0.27), glass=(0.09, 0.10, 0.11), frame=(0.74, 0.74, 0.72),
                       plinth=(0.30, 0.29, 0.27), roof=(0.22, 0.22, 0.23),
                       bay=330.0, storey=290.0, ground=330.0, ww=0.58, wh=0.50, sill=0.28, ground_ww=0.6,
                       mullions=1, transoms=0, bar=7.0, band=24.0, band_tint=1.35,
                       rough=0.88, frame_metal=0.0, lit=0.15, blinds=0.35, glow=50.0),
    # Dark rainscreen cladding on the newer blocks, a metal flashing per floor.
    "Dark": dict(wall=(0.17, 0.17, 0.18), glass=(0.20, 0.26, 0.31), frame=(0.30, 0.31, 0.32),
                 plinth=(0.08, 0.08, 0.09), roof=(0.22, 0.22, 0.23),
                 bay=200.0, storey=330.0, ground=450.0, ww=0.62, wh=0.62, sill=0.20, ground_ww=0.85,
                 mullions=0, transoms=0, bar=6.0, band=12.0, band_tint=1.4,
                 rough=0.55, frame_metal=0.8, lit=0.25, blinds=0.2, glow=80.0),
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

    # Roofscape plant: galvanised units, and the dark louvred screens and lift
    # overruns.
    "Plant":     ((0.50, 0.51, 0.52), 0.45, {"Metallic": 0.7}),
    "PlantDark": ((0.10, 0.10, 0.11), 0.60, {"Metallic": 0.4}),
    # Shopfronts. The glass shows a lit interior: a shop's ~200 cd/m2 behind
    # dark glass, warm white; under the 8,000 lux overcast it reads as lit, not
    # glowing. Frames and stall risers in dark powder-coated aluminium.
    "ShopGlass": ((0.03, 0.035, 0.04), 0.05, {"Metallic": 0.3, "EmissiveColour": (1.0, 0.86, 0.68),
                                              "EmissiveStrength": 200.0}),
    "ShopFrame": ((0.05, 0.05, 0.055), 0.45, {"Metallic": 0.5}),
    # Fascia boards: London's painted heritage colours, and the internally lit
    # box sign of the chains.
    "FasciaGreen": ((0.025, 0.10, 0.06), 0.40, {}),
    "FasciaNavy":  ((0.02, 0.03, 0.10), 0.40, {}),
    "FasciaRed":   ((0.20, 0.02, 0.02), 0.40, {}),
    "FasciaBlack": ((0.015, 0.015, 0.017), 0.35, {}),
    "FasciaLit":   ((0.80, 0.80, 0.78), 0.30, {"EmissiveColour": (1.0, 0.98, 0.94), "EmissiveStrength": 350.0}),

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
        u       = dot(WorldPosition.xy, (-n.y, n.x)) / BayWidth       along the wall
        z       = WorldPosition.z - GroundZ                            above the street
        v       = (z - GroundStorey) / StoreyHeight                    storeys above the ground one
        window  = |frac(u) - 0.5| <= WindowWidth / 2
                  * |frac(v) - Sill - WindowHeight / 2| <= WindowHeight / 2
        bars    = the window's frame, MullionsAcross bars across and
                  TransomsUp up, BarWidthCm wide
        ground  = z < GroundStorey: a plinth with wider, taller openings
        wall    = 1 - |n.z|  (the roof takes RoofColour, no windows)

    Per window cell, a hash of (floor(u), floor(v)) lights a share of them from
    inside and pulls blinds down on another share.
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

    def k(value, x, y):
        return M._scalar_constant(material, value, x, y)

    def op(cls, a, b, x, y):
        node = _node(material, cls, x, y)
        M._connect(material, a, "", node, "A")
        M._connect(material, b, "", node, "B")
        return node

    def one(cls, a, x, y):
        node = _node(material, cls, x, y)
        M._connect(material, a, "", node, "")
        return node

    def mask(src, channels, x, y):
        node = _node(material, unreal.MaterialExpressionComponentMask, x, y)
        for c in ("r", "g", "b", "a"):
            node.set_editor_property(c, c in channels)
        M._connect(material, src, "", node, "")
        return node

    def at_least(a, b, x, y):
        # Step: 1 where X >= Y.
        node = _node(material, unreal.MaterialExpressionStep, x, y)
        M._connect(material, a, "", node, "X")
        M._connect(material, b, "", node, "Y")
        return node

    def lerp(a, b, alpha, x, y):
        node = _node(material, unreal.MaterialExpressionLinearInterpolate, x, y)
        M._connect(material, a, "", node, "A")
        M._connect(material, b, "", node, "B")
        M._connect(material, alpha, "", node, "Alpha")
        return node

    mul, add, sub, div = (unreal.MaterialExpressionMultiply, unreal.MaterialExpressionAdd,
                          unreal.MaterialExpressionSubtract, unreal.MaterialExpressionDivide)

    wall = M._vector_param(material, "WallColour", (0.5, 0.5, 0.5, 1.0), -400, -900)
    glass = M._vector_param(material, "GlassColour", (0.1, 0.12, 0.14, 1.0), -400, -760)
    frame = M._vector_param(material, "FrameColour", (0.7, 0.7, 0.68, 1.0), -400, -620)
    plinth = M._vector_param(material, "PlinthColour", (0.3, 0.3, 0.3, 1.0), -400, -1040)
    roof = M._vector_param(material, "RoofColour", (0.22, 0.22, 0.23, 1.0), -400, -1180)
    bay = M._scalar_param(material, "BayWidthCm", 220.0, -3000, -300, 50.0, 800.0)
    storey = M._scalar_param(material, "StoreyHeightCm", 350.0, -3000, -100, 200.0, 800.0)
    ground = M._scalar_param(material, "GroundStoreyCm", 400.0, -3000, 100, 0.0, 1000.0)
    ground_z = M._scalar_param(material, "GroundZ", GROUND_Z, -3000, 300, -1000.0, 1000.0)
    win_w = M._scalar_param(material, "WindowWidth", 0.5, -2000, -700)
    win_h = M._scalar_param(material, "WindowHeight", 0.55, -2000, -400)
    sill = M._scalar_param(material, "SillHeight", 0.2, -2000, -300)
    ground_w = M._scalar_param(material, "GroundWindowWidth", 0.7, -2000, 700)
    mullions = M._scalar_param(material, "MullionsAcross", 0.0, -2000, -600, 0.0, 6.0)
    transoms = M._scalar_param(material, "TransomsUp", 0.0, -2000, -200, 0.0, 6.0)
    bar_cm = M._scalar_param(material, "BarWidthCm", 5.0, -2000, -500, 0.0, 30.0)
    band_cm = M._scalar_param(material, "BandCm", 0.0, -2000, 1000, 0.0, 100.0)
    band_tint = M._scalar_param(material, "BandTint", 1.0, -1000, -1100, 0.0, 3.0)
    wall_rough = M._scalar_param(material, "WallRoughness", 0.85, -400, 0)
    frame_metal = M._scalar_param(material, "FrameMetallic", 0.0, -400, 140)
    lit = M._scalar_param(material, "LitWindowShare", 0.18, -1100, 1300)
    blinds = M._scalar_param(material, "BlindShare", 0.25, -1100, 1450)
    glow = M._scalar_param(material, "WindowGlow", 60.0, -400, 1300, 0.0, 500.0)

    world = _node(material, unreal.MaterialExpressionWorldPosition, -3300, -260)
    normal = _node(material, unreal.MaterialExpressionVertexNormalWS, -3300, 80)

    # Along-the-wall direction: (-n.y, n.x), from the normal's xy.
    n_x = mask(normal, "r", -3100, 40)
    n_y = mask(normal, "g", -3100, 140)
    n_z = mask(normal, "b", -3100, 240)
    tangent = op(unreal.MaterialExpressionAppendVector, op(mul, n_y, k(-1.0, -3100, 320), -2950, 120),
                 n_x, -2800, 80)
    along = op(unreal.MaterialExpressionDotProduct, mask(world, "rg", -3100, -320), tangent, -2650, -260)
    u = op(div, along, bay, -2500, -300)
    height = op(sub, mask(world, "b", -3100, -180), ground_z, -2650, -120)
    v = op(div, op(sub, height, ground, -2500, -60), storey, -2350, -120)
    upper = at_least(height, ground, -2350, 40)                 # 1 above the ground storey
    fu = one(unreal.MaterialExpressionFrac, u, -2350, -300)
    fv = one(unreal.MaterialExpressionFrac, v, -2200, -120)

    # The upper storeys' windows, centred in the bay, standing on the sill.
    half_w = op(mul, win_w, k(0.5, -2000, -800), -1850, -720)
    half_h = op(mul, win_h, k(0.5, -2000, -330), -1850, -380)
    off_u = one(unreal.MaterialExpressionAbs, op(sub, fu, k(0.5, -2200, -400), -2050, -360), -1850, -300)
    off_v = one(unreal.MaterialExpressionAbs,
                op(sub, fv, op(add, sill, half_h, -1700, -360), -1600, -200), -1450, -200)
    win_x = at_least(half_w, off_u, -1650, -460)
    win_y = at_least(half_h, off_v, -1300, -260)
    window_up = op(mul, win_x, win_y, -1150, -380)

    # Frame and glazing bars: the distance to the nearest bar line, in cm,
    # across panes of (window width / (mullions + 1)) and up panes of
    # (window height / (transoms + 1)); the window's own edges are bar lines.
    def bars(coord_cm, size_cm, count, y):
        pane = op(div, size_cm, op(add, count, k(1.0, -1900, y + 60), -1750, y + 60), -1600, y)
        phase = one(unreal.MaterialExpressionFrac,
                    op(add, op(div, coord_cm, pane, -1450, y), k(0.5, -1450, y + 80), -1300, y), -1150, y)
        dist = op(mul, one(unreal.MaterialExpressionAbs, op(sub, phase, k(0.5, -1150, y + 80), -1000, y), -850, y),
                  pane, -700, y)
        return at_least(op(mul, bar_cm, k(0.5, -850, y + 80), -700, y + 80), dist, -550, y)

    win_w_cm = op(mul, win_w, bay, -1900, -560)
    win_h_cm = op(mul, win_h, storey, -1900, -160)
    x_cm = op(mul, op(add, op(sub, fu, k(0.5, -2050, -520), -1950, -520), half_w, -1800, -520), bay, -1700, -520)
    y_cm = op(mul, op(sub, fv, sill, -1950, -140), storey, -1800, -140)
    bar_x = bars(x_cm, win_w_cm, mullions, -620)
    bar_y = bars(y_cm, win_h_cm, transoms, -120)
    on_bar = op(mul, op(mul, op(unreal.MaterialExpressionMax, bar_x, bar_y, -400, -400), window_up, -300, -400),
                upper, -200, -400)

    # The ground storey: wider openings, 7% to 83% of its height, no bars
    # (the shopfronts on the near streets are geometry).
    g_x = at_least(op(mul, ground_w, k(0.5, -1900, 760), -1750, 720), off_u, -1600, 700)
    g_y = at_least(op(mul, ground, k(0.38, -1900, 860), -1750, 820),
                   one(unreal.MaterialExpressionAbs,
                       op(sub, height, op(mul, ground, k(0.45, -1900, 960), -1750, 920), -1650, 900), -1550, 880),
                   -1450, 820)
    window = lerp(op(mul, g_x, g_y, -1300, 760), window_up, upper, -1000, 400)

    # Walls only: 1 - |n.z|, hardened.
    is_wall = one(unreal.MaterialExpressionOneMinus, one(unreal.MaterialExpressionAbs, n_z, -2950, 240), -2800, 240)
    wall_mask = at_least(is_wall, k(0.5, -2800, 340), -2650, 240)
    framed = op(mul, on_bar, wall_mask, -100, -400)
    glazed = op(mul, op(mul, window, wall_mask, -850, 400),
                one(unreal.MaterialExpressionOneMinus, on_bar, -850, 500), -700, 420)

    # Per-cell randomness on (floor(u), floor(v)): frac(sin(h) * 43758.5).
    cell = op(unreal.MaterialExpressionAppendVector, one(unreal.MaterialExpressionFloor, u, -2200, 1200),
              one(unreal.MaterialExpressionFloor, v, -2200, 1300), -2050, 1250)
    seed = _node(material, unreal.MaterialExpressionConstant2Vector, -2050, 1380)
    seed.set_editor_property("r", 12.9898)
    seed.set_editor_property("g", 78.233)
    hashed = op(unreal.MaterialExpressionDotProduct, cell, seed, -1900, 1250)
    rnd = one(unreal.MaterialExpressionFrac,
              op(mul, one(unreal.MaterialExpressionSine, hashed, -1750, 1250), k(43758.5, -1750, 1350), -1600, 1250),
              -1450, 1250)
    lit_cell = at_least(lit, rnd, -950, 1300)
    blind = at_least(blinds, one(unreal.MaterialExpressionFrac, op(mul, rnd, k(7.31, -1450, 1450), -1300, 1420),
                                 -1150, 1420), -950, 1450)

    # Colour: plinth below, wall above with its floor bands, frames, glass
    # (pale where the blinds are down), and the roof.
    band = op(mul, at_least(band_cm, op(mul, fv, storey, -1900, 1060), -1750, 1040), upper, -1600, 1040)
    wall_c = op(mul, lerp(plinth, wall, upper, -800, -1000),
                lerp(k(1.0, -1000, -1200), band_tint, band, -800, -1150), -600, -1000)
    glass_c = lerp(glass, M._constant3(material, (0.30, 0.29, 0.26), -600, -700), blind, -450, -720)
    colour = lerp(lerp(wall_c, frame, framed, -300, -900), glass_c, glazed, -150, -800)
    M._connect_property(material, lerp(roof, colour, wall_mask, 0, -900), "",
                        unreal.MaterialProperty.MP_BASE_COLOR)

    rough = lerp(lerp(wall_rough, k(0.45, -400, 60), framed, -250, 0),
                 lerp(k(0.06, -400, 220), k(0.5, -400, 260), blind, -250, 220), glazed, -100, 80)
    M._connect_property(material, lerp(k(0.85, -100, -60), rough, wall_mask, 50, 0), "",
                        unreal.MaterialProperty.MP_ROUGHNESS)

    metal = op(add, op(mul, glazed, lerp(k(0.85, -400, 380), k(0.3, -400, 420), blind, -250, 400), -100, 400),
               op(mul, framed, frame_metal, -100, 500), 50, 450)
    M._connect_property(material, metal, "", unreal.MaterialProperty.MP_METALLIC)

    # A share of windows lit from inside, warm white, at an interior's luminance.
    emissive = op(mul, op(mul, op(mul, lit_cell, glazed, -700, 1300), glow, -550, 1300),
                  M._constant3(material, (1.0, 0.82, 0.6), -700, 1400), -400, 1320)
    M._connect_property(material, emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

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
        for variant in D.FACADE_VARIANTS:
            f = FACADES[variant]
            out["Facade_" + variant] = _instance(
                "MI_OB_Facade_" + variant, facade,
                {"WallColour": f["wall"], "GlassColour": f["glass"], "FrameColour": f["frame"],
                 "PlinthColour": f["plinth"], "RoofColour": f["roof"]},
                {"BayWidthCm": f["bay"], "StoreyHeightCm": f["storey"], "GroundStoreyCm": f["ground"],
                 "GroundZ": GROUND_Z, "WindowWidth": f["ww"], "WindowHeight": f["wh"],
                 "SillHeight": f["sill"], "GroundWindowWidth": f["ground_ww"],
                 "MullionsAcross": float(f["mullions"]), "TransomsUp": float(f["transoms"]),
                 "BarWidthCm": f["bar"], "BandCm": f["band"], "BandTint": f["band_tint"],
                 "WallRoughness": f["rough"], "FrameMetallic": f["frame_metal"],
                 "LitWindowShare": f["lit"], "BlindShare": f["blinds"], "WindowGlow": f["glow"]}, report)
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


def _shopfront(batch, unit):
    """One shop on the facade line, built out towards the street: a stall
    riser, pilasters either side, the lit glazing with a door and its transom,
    the fascia board and a cornice over it. Real proportions: a 45 cm riser,
    3 m to the fascia, an 80 cm fascia."""
    x, y, yaw, w = unit["x"], unit["y"], unit["yaw"], unit["w"]
    nx, ny = unit["nx"], unit["ny"]
    tx, ty = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))    # along the facade

    def part(key, along, out, z0, z1, size_x, size_y):
        batch.box(key, x + tx * along + nx * out, y + ty * along + ny * out, GROUND_Z + z0,
                  size_x, size_y, z1 - z0, yaw)

    pier = 35.0
    clear = w - 2.0 * pier
    door_w = 110.0                                                         # the door, at one end
    door = -clear / 2.0 + door_w
    for end in (-1.0, 1.0):
        part("ShopFrame", end * (w - pier) / 2.0, 11.0, 0.0, 300.0, pier, 22.0)   # pilasters
    part("ShopFrame", (door + clear / 2.0) / 2.0, 7.0, 0.0, 45.0, clear / 2.0 - door, 14.0)  # stall riser
    part("ShopGlass", 0.0, 6.0, 45.0, 300.0, clear, 4.0)
    part("ShopGlass", door - door_w / 2.0, 6.0, 0.0, 45.0, door_w, 4.0)   # the door's glass to the floor
    part("ShopFrame", door, 8.0, 0.0, 240.0, 6.0, 8.0)
    part("ShopFrame", 0.0, 8.0, 232.0, 240.0, clear, 8.0)                 # transom
    part(unit["fascia"], 0.0, 12.0, 300.0, 380.0, w, 24.0)
    part("ShopFrame", 0.0, 16.0, 380.0, 390.0, w + 10.0, 32.0)            # cornice


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

    # Buildings in their facade variant's mesh, and the plant on their roofs.
    # The dataset lists the near buildings first.
    near = data["counts"]["buildings"]
    variants, plant = {}, 0
    for index, building in enumerate(data["buildings"]):
        variant = D.facade_variant(building)
        variants[variant] = variants.get(variant, 0) + 1
        batch.extrude("Facade_" + variant, building["ring"], GROUND_Z, float(building["h"]))
        for kind, x, y, z, sx, sy, sz, yaw in D.roof_plant(building, index < near):
            batch.box(kind, x, y, GROUND_Z + z, sx, sy, sz, yaw)
            plant += 1

    shops = D.shopfronts(data)
    for unit in shops:
        _shopfront(batch, unit)

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

    streets = sorted({unit["street"] for unit in shops})
    C.log(u"  district: {0} buildings near, {1} city to 1.8 km, {2} on the skyline, {3} roads, "
          u"{4} parks, {5} trees, {6} vehicles, {7} roof plant units, {8} shopfronts on {9} streets ({10})".format(
              data["counts"]["buildings"], data["counts"].get("city", 0), data["counts"]["skyline"],
              data["counts"]["roads"], data["counts"]["parks"], data["counts"]["trees"], vehicles,
              plant, len(shops), len(streets), data["attribution"]))
    return {"meshes": placed, "counts": data["counts"], "vehicles": vehicles, "frame": data["frame"],
            "facades": variants, "roof_plant": plant, "shopfronts": len(shops), "shop_streets": streets,
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


def build_sky_material(report, cube, dusk_cube=None):
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
    # The dusk photograph, crossfaded in by AOneBrahamSkyDirector at the Purge
    # (DuskBlend 0 = day). Without the dusk import the day cube stands in, so
    # the material still compiles and a blend changes nothing.
    dusk = _node(material, unreal.MaterialExpressionTextureSampleParameterCube, -520, -160)
    dusk.set_editor_property("parameter_name", "SkyCubeDusk")
    if dusk_cube is not None or cube is not None:
        dusk.set_editor_property("texture", dusk_cube or cube)
    M._connect(material, direction, "", dusk, "UVs")
    photograph = _node(material, unreal.MaterialExpressionLinearInterpolate, -420, -80)
    M._connect(material, sample, "RGB", photograph, "A")
    M._connect(material, dusk, "RGB", photograph, "B")
    M._connect(material, M._scalar_param(material, "DuskBlend", 0.0, -620, -240, 0.0, 1.0), "", photograph, "Alpha")
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
    M._connect(material, photograph, "", blended, "B")
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


def import_sky_cube(report, preset):
    """Imports the preset's photograph as SKY_FOLDER/T_Sky_<Preset> and returns
    (cube, manifest entry), or None when it is not fetched or does not import."""
    asset_id = SKY_PRESETS[preset][0]
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
    return cube, entry


def build_sky(report, preset=None):
    """Imports the preset's sky (and the dusk sky the Purge fades to), places
    the dome, and returns the sun as (pitch, yaw, colour) for the directional
    light, plus the dome and dusk cube for the sky director - or None to keep
    the procedural sky (no fetched assets on this machine)."""
    preset = preset or SKY_PRESET
    asset_id, luminance, colour = SKY_PRESETS[preset]
    imported = import_sky_cube(report, preset)
    if imported is None:
        return None
    cube, entry = imported
    # Imported only, never domed: AOneBrahamSkyDirector crossfades the one dome
    # to it (T_Sky_Dusk is the path it loads).
    dusk = imported if preset == "dusk" else import_sky_cube(report, "dusk")
    dusk_cube = dusk[0] if dusk else None

    material = build_sky_material(report, cube, dusk_cube)
    dome = None
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
            "sky": asset_id, "licence": entry["licence"], "dome": dome, "dusk_cube": dusk_cube}


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
