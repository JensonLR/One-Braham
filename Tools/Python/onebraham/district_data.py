# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.district_data
=======================

Turns an OpenStreetMap extract of Aldgate East into the district dataset the
environment build extrudes: Tools/Python/onebraham/data/aldgate_district.json.

The tower is fiction laid over a real place. Everything around it - the
buildings on Leman Street and Alie Street, the Whitechapel High Street
frontage, Braham Street Park, the City cluster on the skyline - comes from
OpenStreetMap, at real scale, in the game's coordinates. The real One Braham
(OSM way 100299970, 1 Braham Street, E1 8EE) fixes the frame:

    origin  the centroid of its footprint
    X       along its long axis (the game plate's long side)
    -Y      out of its long north-west face, onto Braham Street, which is where
            the game puts the revolving doors

Unreal is left-handed, so geographic east/north go in as X = east, Y = south
and are then turned about Z by the footprint's bearing; that keeps the map the
right way round rather than mirrored.

This module needs no engine. Fetch the two Overpass extracts (queries in
Docs/09 and below), then:

    python3 -I Tools/Python/onebraham/district_data.py local.json sky.json [tile.json ...]

(the tiles are 900 m bounding-box building extracts covering 1.8 km round the
tower: way["building"](s,w,n,e); out tags geom;)

Map data (c) OpenStreetMap contributors, available under the Open Database
Licence (ODbL 1.0); the credit is carried in the dataset and in Docs/10.
"""

from __future__ import print_function

import json
import math
import os
import sys

ONE_BRAHAM_WAY = 100299970
LOCAL_RADIUS_M = 600.0
SKYLINE_MIN_DISTANCE_M = 600.0
# The surrounding city, from tiled extracts: everything between the local
# radius and this, so the towers on the skyline stand in streets, not a plain.
OUTER_RADIUS_M = 1800.0
OUTER_MIN_AREA_M2 = 30.0

# The game tower's plate (Tools/Python/onebraham/common.py AREAS), plus a
# margin for its forecourt. OSM buildings and roads inside it are the real
# building and its service yard, which the game replaces.
TOWER_CLEAR_X_CM = 2200.0 + 900.0
TOWER_CLEAR_Y_CM = 1800.0 + 900.0

STOREY_M = 3.5
ROOF_M = 1.5
DEFAULT_LEVELS = {
    "house": 3, "terrace": 3, "terraced": 3, "residential": 5, "apartments": 7,
    "commercial": 6, "office": 8, "retail": 2, "hotel": 8, "church": 4,
    "mosque": 3, "school": 3, "university": 5, "kiosk": 1, "garage": 1,
    "roof": 1, "train_station": 2, "industrial": 3, "public": 4, "civic": 4,
}
DEFAULT_LEVELS_ANY = 5   # building=yes in E1/EC3: mostly five-storey stock

# Road widths (metres, kerb to kerb) by OSM class. Footways and the like are
# left out: the ground is paved, so a pavement needs no geometry of its own.
ROAD_WIDTHS = {
    "motorway": 16.0, "trunk": 14.0, "primary": 13.0, "secondary": 11.0,
    "tertiary": 9.5, "trunk_link": 8.0, "primary_link": 8.0, "secondary_link": 7.5,
    "unclassified": 7.0, "residential": 7.0, "living_street": 6.0, "service": 4.5,
    "bus_guideway": 7.0,
}

# Facade families the build has a material for.
FACADE_GLASS = "Glass"      # curtain walls: the towers
FACADE_STONE = "Stone"      # Portland stone and render: offices, civic, hotels
FACADE_BRICK = "Brick"      # London stock brick: terraces, flats, warehouses
FACADE_DARK = "Dark"        # dark-clad modern blocks

# Each family splits into the window patterns London actually has, chosen by
# height and, among buildings of one height, by a hash of the OSM id so a
# street is a mix rather than a repeat. The build gives each variant its own
# M_OB_Facade instance (storey height, bay, window share, glazing bars) and
# its own merged mesh: eight facade draw calls for the whole city.
FACADE_VARIANTS = (
    "Glass",           # unitised curtain wall: towers
    "GlassRibbon",     # ribbon glazing over spandrel panels: 1960s-90s offices
    "Stone",           # Portland stone and render, sash windows, string courses
    "StoneOffice",     # stone-clad modern office: wide windows, dark frames
    "Brick",           # Georgian and Victorian terraces: sashes over a shop
    "BrickWarehouse",  # warehouses and mansion blocks: multi-pane steel windows
    "BrickBlock",      # post-war and new-build flats: floor bands, uPVC
    "Dark",            # dark rainscreen cladding
)

# The near streets with shops on them. Names as OpenStreetMap spells them.
SHOPFRONT_STREETS = (
    "Braham Street", "Leman Street", "Alie Street", "Commercial Road",
    "Whitechapel High Street", "Whitechapel Road", "Aldgate High Street",
    "Commercial Street", "Goulston Street", "Old Castle Street", "Osborn Street",
    "Mansell Street",
)
SHOPFRONT_RADIUS_CM = 45000.0     # 450 m: everything a player can see up close
SHOPFRONT_MIN_EDGE_CM = 450.0
SHOPFRONT_PAVEMENT_CM = 900.0     # facade line to kerb, at most
SHOPFRONT_PARALLEL = 0.92         # |cos| between facade and street: within ~23 degrees
SHOPFRONT_UNIT_CM = (520.0, 600.0, 680.0, 760.0)   # one shop's frontage
SHOPFRONT_FASCIAS = ("FasciaGreen", "FasciaNavy", "FasciaRed", "FasciaBlack", "FasciaLit")

# Roof plant: lift overruns, air-handling units, condenser rows, tower plant
# screens. Near buildings from 150 m2 of roof; the far city only its big roofs.
PLANT_MIN_AREA_NEAR_M2 = 150.0
PLANT_MIN_AREA_FAR_M2 = 800.0
PLANT_MIN_HEIGHT_CM = 900.0
PLANT_SETBACK_CM = 150.0          # inside the parapet line


# ---------------------------------------------------------------------------
#  Frame
# ---------------------------------------------------------------------------

class Frame(object):
    """Geographic (lat, lon) to game centimetres, anchored on One Braham."""

    def __init__(self, footprint):
        lat0 = sum(p[0] for p in footprint) / len(footprint)
        lon0 = sum(p[1] for p in footprint) / len(footprint)
        self.lat0, self.lon0 = lat0, lon0
        self.kx = math.cos(math.radians(lat0)) * 111320.0
        self.ky = 110540.0

        # The bearing of the long axis: the longest edge's direction.
        pts = [self._en(lat, lon) for lat, lon in footprint]
        best = None
        for a, b in zip(pts, pts[1:] + pts[:1]):
            length = math.hypot(b[0] - a[0], b[1] - a[1])
            if best is None or length > best[0]:
                best = (length, b[0] - a[0], b[1] - a[1])
        _, de, dn = best
        # In the (east, south) base the long edge points at atan2(-dn, de);
        # rotate it onto +X. Either direction along it is the same axis; the
        # sign is chosen so that -Y leaves through the north-west face.
        self.angle = -math.atan2(-dn, de)
        nx, ny = self._rot(-0.55, -0.84)   # NW, in the (east, south) base
        if ny > 0.0:
            self.angle += math.pi

    def _en(self, lat, lon):
        return ((lon - self.lon0) * self.kx, (lat - self.lat0) * self.ky)

    def _rot(self, x, y):
        c, s = math.cos(self.angle), math.sin(self.angle)
        return (x * c - y * s, x * s + y * c)

    def to_game(self, lat, lon):
        e, n = self._en(lat, lon)
        x, y = self._rot(e, -n)
        return (round(x * 100.0, 1), round(y * 100.0, 1))

    def distance_m(self, lat, lon):
        e, n = self._en(lat, lon)
        return math.hypot(e, n)


# ---------------------------------------------------------------------------
#  Geometry helpers
# ---------------------------------------------------------------------------

def _ring(points, tolerance=30.0):
    """Drop the closing duplicate, short edges and near-collinear points
    (tolerance in cm: 0.3 m close to the tower, coarser for the far city)."""
    if len(points) > 1 and points[0] == points[-1]:
        points = points[:-1]
    out = []
    for p in points:
        if out and math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) < tolerance:
            continue
        out.append(p)
    changed = True
    while changed and len(out) > 3:
        changed = False
        for i in range(len(out)):
            a, b, c = out[i - 1], out[i], out[(i + 1) % len(out)]
            cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
            if abs(cross) < tolerance * math.hypot(c[0] - a[0], c[1] - a[1]):
                del out[i]
                changed = True
                break
    return out


def _area(ring):
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(ring, ring[1:] + ring[:1]))


def _inside_tower(x, y):
    return abs(x) < TOWER_CLEAR_X_CM and abs(y) < TOWER_CLEAR_Y_CM


def _number(text):
    if text is None:
        return None
    try:
        return float(str(text).split(";")[0].replace("m", "").strip())
    except ValueError:
        return None


def _height_m(tags):
    height = _number(tags.get("height"))
    if height:
        return height
    levels = _number(tags.get("building:levels"))
    if levels is None:
        levels = DEFAULT_LEVELS.get(tags.get("building"), DEFAULT_LEVELS_ANY)
    return levels * STOREY_M + ROOF_M


def _facade(tags, height_m):
    kind = tags.get("building", "yes")
    material = (tags.get("building:material") or tags.get("building:facade:material") or "").lower()
    if height_m >= 45.0 or "glass" in material:
        return FACADE_GLASS
    if "brick" in material or kind in ("house", "terrace", "terraced", "residential", "apartments",
                                       "industrial", "warehouse"):
        return FACADE_BRICK
    if kind in ("office", "commercial", "hotel", "church", "mosque", "civic", "public",
                "university", "school") or "stone" in material:
        return FACADE_STONE
    if height_m >= 28.0:
        return FACADE_DARK
    return FACADE_BRICK if height_m <= 16.0 else FACADE_STONE


def _outer_rings(element):
    """The outer rings of a way or multipolygon relation, as (lat, lon) lists."""
    if element["type"] == "way":
        return [[(g["lat"], g["lon"]) for g in element.get("geometry", [])]]
    rings = []
    for member in element.get("members", []):
        if member.get("role") == "outer" and member.get("geometry"):
            rings.append([(g["lat"], g["lon"]) for g in member["geometry"]])
    return rings


# ---------------------------------------------------------------------------
#  Dressing: what the build puts on the extruded footprints
# ---------------------------------------------------------------------------
# Engine-free and deterministic, so the same dataset always dresses the same
# street, and so the rules can be checked without Unreal.

def _hash(value, salt=0):
    """A small integer hash (Knuth's multiplicative, folded): stable across
    Python versions, unlike hash()."""
    h = (int(value) * 2654435761 + salt * 40503) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 2246822519) & 0xFFFFFFFF
    return h ^ (h >> 13)


def facade_variant(building):
    """The FACADE_VARIANTS entry a building is drawn with."""
    family, h, pick = building["facade"], building["h"], _hash(building["id"]) % 20
    if family == FACADE_GLASS:
        # A tower is a curtain wall; two low glass blocks in three are the
        # older ribbon-glazed office.
        if h >= 4500:
            return "Glass"
        return "GlassRibbon" if pick < 13 else "Glass"
    if family == FACADE_STONE:
        if h > 2200:
            return "StoneOffice"
        # The five-storey stock of E1: mostly stone and render, a share of it
        # modern infill, and some of it brick under an untagged material.
        if pick < 10:
            return "Stone"
        return "StoneOffice" if pick < 16 else "BrickWarehouse"
    if family == FACADE_BRICK:
        if h <= 1300:
            return "Brick"
        if h <= 2200:
            return "BrickWarehouse" if pick < 13 else "Brick"
        return "BrickBlock"
    return "Dark"


def _centroid(ring):
    a = _area(ring)
    if abs(a) < 1e-6:
        return (sum(p[0] for p in ring) / len(ring), sum(p[1] for p in ring) / len(ring))
    cx = cy = 0.0
    for p, q in zip(ring, ring[1:] + ring[:1]):
        cross = p[0] * q[1] - q[0] * p[1]
        cx += (p[0] + q[0]) * cross
        cy += (p[1] + q[1]) * cross
    return (cx / (6.0 * a), cy / (6.0 * a))


def _inside(ring, x, y):
    """Even-odd point in polygon."""
    inside = False
    for p, q in zip(ring, ring[1:] + ring[:1]):
        if (p[1] > y) != (q[1] > y):
            t = (y - p[1]) / (q[1] - p[1])
            if x < p[0] + t * (q[0] - p[0]):
                inside = not inside
    return inside


def _segment_distance(px, py, ax, ay, bx, by):
    """Distance from a point to a segment, and the closest point on it."""
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 <= 0.0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length2))
    cx, cy = ax + dx * t, ay + dy * t
    return math.hypot(px - cx, py - cy), cx, cy


def _edge_clearance(ring, x, y):
    return min(_segment_distance(x, y, p[0], p[1], q[0], q[1])[0]
               for p, q in zip(ring, ring[1:] + ring[:1]))


def _long_axis(ring):
    best = None
    for p, q in zip(ring, ring[1:] + ring[:1]):
        length = math.hypot(q[0] - p[0], q[1] - p[1])
        if best is None or length > best[0]:
            best = (length, math.atan2(q[1] - p[1], q[0] - p[0]))
    return best[1]


def roof_plant(building, near=True):
    """Boxes on the roof: (kind, x, y, z, size_x, size_y, size_z, yaw_deg),
    centred in x/y, z the base above street level in cm, size_x along yaw.
    kind is "Plant" (galvanised units) or "PlantDark" (overruns, louvred
    screens). Every box sits wholly inside the footprint, clear of the parapet
    by PLANT_SETBACK_CM, and clear of the others."""
    ring = building["ring"]
    h = float(building["h"])
    area_m2 = abs(_area(ring)) / 10000.0
    if h < PLANT_MIN_HEIGHT_CM or len(ring) < 3:
        return []
    if area_m2 < (PLANT_MIN_AREA_NEAR_M2 if near else PLANT_MIN_AREA_FAR_M2):
        return []

    seed = _hash(building["id"], 7)
    theta = _long_axis(ring)
    c, s = math.cos(theta), math.sin(theta)
    cx, cy = _centroid(ring)
    if not _inside(ring, cx, cy):
        return []
    side = math.sqrt(area_m2) * 100.0      # the roof's scale, cm

    wanted = []
    if h >= 4500 and area_m2 >= 600.0:
        # A tower's plant floor: a louvred screen round the top, a third of the roof.
        screen = round(min(side * 0.55, 2400.0), 1)
        wanted.append(("PlantDark", screen, round(screen * 0.8, 1), 420.0))
    if h >= 1800:
        # Lift overrun and stair head over the core.
        wanted.append(("PlantDark", 420.0, 300.0, 300.0))
    if area_m2 >= 400.0:
        wanted.append(("Plant", 620.0, 240.0, 190.0))           # air-handling unit
    if area_m2 >= 900.0:
        wanted.append(("Plant", 480.0, 220.0, 170.0))
    condensers = 2 + seed % 3
    for _ in range(condensers):
        wanted.append(("Plant", 120.0, 110.0, 100.0))          # condenser units in a row

    # Candidate centres in the roof's own frame (along, across), nearest the
    # middle first; the seed turns the order so neighbours differ.
    steps = (0.0, 0.18, -0.18, 0.32, -0.32)
    grid = [(a * side, b * side) for a in steps for b in steps]
    order = sorted(range(len(grid)), key=lambda i: (round(abs(steps[i // len(steps)]) + abs(steps[i % len(steps)]), 3),
                                                    (i + seed) % len(grid)))
    candidates = [grid[i] for i in order]

    placed, out = [], []
    for kind, sx, sy, sz in wanted:
        for along, across in candidates:
            corners = [(along + ex * sx / 2.0, across + ey * sy / 2.0) for ex in (-1, 1) for ey in (-1, 1)]
            if any(abs(along - pa) * 2.0 < sx + psx + 60.0 and abs(across - pb) * 2.0 < sy + psy + 60.0
                   for pa, pb, psx, psy in placed):
                continue
            ok = True
            for a, b in corners:
                x, y = cx + a * c - b * s, cy + a * s + b * c
                if not _inside(ring, x, y) or _edge_clearance(ring, x, y) < PLANT_SETBACK_CM:
                    ok = False
                    break
            # Corners alone miss a re-entrant corner of an L- or U-shaped roof
            # poking into the box: no footprint vertex within the setback of it.
            for px, py in ring if ok else ():
                dx, dy = px - cx, py - cy
                gap_x = max(0.0, abs(dx * c + dy * s - along) - sx / 2.0)
                gap_y = max(0.0, abs(-dx * s + dy * c - across) - sy / 2.0)
                if math.hypot(gap_x, gap_y) < PLANT_SETBACK_CM:
                    ok = False
                    break
            if not ok:
                continue
            placed.append((along, across, sx, sy))
            out.append((kind, round(cx + along * c - across * s, 1), round(cy + along * s + across * c, 1),
                        h, sx, sy, sz, round(math.degrees(theta), 2)))
            break
    return out


def shopfronts(data):
    """Shop units on the facades that face the near shopping streets (and on
    any mapped shop facing any near street). Each unit is a dict:

        x, y     the unit's centre on the facade line (cm)
        yaw      degrees, along the facade (X of the unit's boxes)
        nx, ny   the facade's outward normal, towards the street
        w        frontage width (cm)
        fascia   one of SHOPFRONT_FASCIAS
        street   the street it faces
    """
    streets = []
    for road in data["roads"]:
        named = road["name"] in SHOPFRONT_STREETS
        if not named and road["class"] == "service":
            continue
        half = float(road["w"]) / 2.0
        for (ax, ay), (bx, by) in road["segments"]:
            if min(math.hypot(ax, ay), math.hypot(bx, by)) > SHOPFRONT_RADIUS_CM + 5000.0:
                continue
            length = math.hypot(bx - ax, by - ay)
            if length < 1.0:
                continue
            streets.append((named, road["name"], half, ax, ay, bx, by, (bx - ax) / length, (by - ay) / length,
                            min(ax, bx), max(ax, bx), min(ay, by), max(ay, by)))

    units = []
    local = data["counts"]["buildings"]
    for building in data["buildings"][:local]:
        ring = building["ring"]
        if len(ring) < 3 or building["h"] < 450:          # the shopfront stands 3.9 m
            continue
        cx, cy = _centroid(ring)
        if math.hypot(cx, cy) > SHOPFRONT_RADIUS_CM:
            continue
        retail = building.get("retail", False)
        ccw = _area(ring) > 0.0
        for index, (p, q) in enumerate(zip(ring, ring[1:] + ring[:1])):
            dx, dy = q[0] - p[0], q[1] - p[1]
            length = math.hypot(dx, dy)
            if length < SHOPFRONT_MIN_EDGE_CM:
                continue
            tx, ty = dx / length, dy / length
            nx, ny = (ty, -tx) if ccw else (-ty, tx)        # outward
            mx, my = (p[0] + q[0]) / 2.0, (p[1] + q[1]) / 2.0
            reach = SHOPFRONT_PAVEMENT_CM + 1200.0
            best = None
            for named, name, half, ax, ay, bx, by, ux, uy, x0, x1, y0, y1 in streets:
                if not (named or retail):
                    continue
                if mx < x0 - reach or mx > x1 + reach or my < y0 - reach or my > y1 + reach:
                    continue
                if abs(tx * ux + ty * uy) < SHOPFRONT_PARALLEL:
                    continue
                dist, sx, sy = _segment_distance(mx, my, ax, ay, bx, by)
                # The street has to be in front of the facade, and the
                # pavement between them no wider than a London pavement.
                if (sx - mx) * nx + (sy - my) * ny <= 0.0:
                    continue
                if dist < half - 100.0 or dist > half + SHOPFRONT_PAVEMENT_CM:
                    continue
                if best is None or dist < best[0]:
                    best = (dist, name)
            if best is None:
                continue
            seed = _hash(building["id"], index)
            target = SHOPFRONT_UNIT_CM[seed % len(SHOPFRONT_UNIT_CM)]
            usable = length - 60.0                          # clear of the corners
            count = max(1, int(round(usable / target)))
            width = usable / count
            if width < 300.0:
                continue
            yaw = round(math.degrees(math.atan2(ty, tx)), 2)
            for k in range(count):
                t = 30.0 + width * (k + 0.5)
                ux, uy = p[0] + tx * t, p[1] + ty * t
                # OSM maps some frontages twice (a building over its older
                # outline, overlapping neighbours): one shop on a stretch of
                # facade, not two in the same place fighting for the pixels.
                if any(nx * other["nx"] + ny * other["ny"] > 0.9
                       and abs((ux - other["x"]) * nx + (uy - other["y"]) * ny) < 100.0
                       and abs((ux - other["x"]) * tx + (uy - other["y"]) * ty) < (width + other["w"]) / 2.0 - 20.0
                       for other in units):
                    continue
                units.append({
                    "x": round(ux, 1), "y": round(uy, 1), "yaw": yaw,
                    "nx": round(nx, 4), "ny": round(ny, 4), "w": round(width, 1),
                    "fascia": SHOPFRONT_FASCIAS[_hash(seed, k) % len(SHOPFRONT_FASCIAS)],
                    "street": best[1],
                })
    return units


# ---------------------------------------------------------------------------
#  Conversion
# ---------------------------------------------------------------------------

def convert(local, sky, outer_tiles=()):
    elements = local["elements"]
    braham = next(e for e in elements if e.get("id") == ONE_BRAHAM_WAY and e["type"] == "way")
    frame = Frame([(g["lat"], g["lon"]) for g in braham["geometry"]][:-1])

    buildings, roads, parks, trees = [], [], [], []
    seen = set()

    def add_building(element, require_far, outer=False):
        tags = element.get("tags", {})
        for ring_ll in _outer_rings(element):
            if len(ring_ll) < 4:
                continue
            lat_c = sum(p[0] for p in ring_ll) / len(ring_ll)
            lon_c = sum(p[1] for p in ring_ll) / len(ring_ll)
            distance = frame.distance_m(lat_c, lon_c)
            if require_far and distance < SKYLINE_MIN_DISTANCE_M:
                continue
            if outer and not (LOCAL_RADIUS_M <= distance <= OUTER_RADIUS_M):
                continue
            ring = _ring([frame.to_game(lat, lon) for lat, lon in ring_ll], 150.0 if outer else 30.0)
            min_area = OUTER_MIN_AREA_M2 if outer else 8.0
            if len(ring) < 3 or abs(_area(ring)) < min_area * 10000.0:
                continue
            if any(_inside_tower(x, y) for x, y in ring):
                continue
            height = _height_m(tags)
            building = {
                "id": element["id"],
                "name": tags.get("name", ""),
                "h": round(height * 100.0),
                "facade": _facade(tags, height),
                "ring": [[x, y] for x, y in ring],
            }
            # A mapped shop or retail building gets a shopfront on any near
            # street, not only the named shopping streets.
            if "shop" in tags or tags.get("building") == "retail":
                building["retail"] = True
            buildings.append(building)

    for element in elements:
        tags = element.get("tags", {})
        key = (element["type"], element["id"])
        if key in seen:
            continue
        seen.add(key)
        if "building" in tags and element.get("id") != ONE_BRAHAM_WAY:
            add_building(element, False)
        elif tags.get("highway") in ROAD_WIDTHS and element["type"] == "way":
            line = [frame.to_game(g["lat"], g["lon"]) for g in element.get("geometry", [])]
            segments = [[a, b] for a, b in zip(line, line[1:])
                        if not _inside_tower((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)]
            if segments:
                roads.append({
                    "name": tags.get("name", ""),
                    "class": tags["highway"],
                    "w": round(ROAD_WIDTHS[tags["highway"]] * 100.0),
                    "oneway": tags.get("oneway") == "yes",
                    "segments": [[list(a), list(b)] for a, b in segments],
                })
        elif (tags.get("leisure") in ("park", "garden") or tags.get("landuse") == "grass") \
                and element["type"] == "way":
            ring = _ring([frame.to_game(g["lat"], g["lon"]) for g in element.get("geometry", [])])
            if len(ring) >= 3 and abs(_area(ring)) > 20.0 * 10000.0:
                parks.append({"name": tags.get("name", ""), "ring": [[x, y] for x, y in ring]})
        elif tags.get("natural") == "tree" and element["type"] == "node":
            x, y = frame.to_game(element["lat"], element["lon"])
            if not _inside_tower(x, y):
                trees.append([x, y])
        elif tags.get("natural") == "tree_row" and element["type"] == "way":
            line = [frame.to_game(g["lat"], g["lon"]) for g in element.get("geometry", [])]
            for a, b in zip(line, line[1:]):
                steps = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) // 800.0))
                for i in range(steps):
                    t = i / float(steps)
                    x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
                    if not _inside_tower(x, y):
                        trees.append([round(x, 1), round(y, 1)])

    skyline_before = len(buildings)
    for element in (sky or {}).get("elements", []):
        key = (element["type"], element["id"])
        if key in seen or "building" not in element.get("tags", {}):
            continue
        seen.add(key)
        add_building(element, True)

    skyline_after = len(buildings)
    for tile in outer_tiles:
        for element in tile.get("elements", []):
            key = (element["type"], element["id"])
            if key in seen or "building" not in element.get("tags", {}):
                continue
            seen.add(key)
            add_building(element, False, outer=True)

    named = sorted({b["name"] for b in buildings if b["name"] and b["h"] >= 6000})
    return {
        "attribution": "Map data (c) OpenStreetMap contributors, ODbL 1.0 - openstreetmap.org/copyright",
        "frame": {
            "anchor_osm_way": ONE_BRAHAM_WAY,
            "anchor_lat": round(frame.lat0, 7),
            "anchor_lon": round(frame.lon0, 7),
            "bearing_deg": round(math.degrees(frame.angle), 2),
            "units": "cm; X along One Braham's long axis, -Y out of its Braham Street face",
            "ground_z_cm": -400.0,
        },
        "counts": {
            "buildings": skyline_before, "skyline": skyline_after - skyline_before,
            "city": len(buildings) - skyline_after,
            "roads": len(roads), "parks": len(parks), "trees": len(trees),
        },
        "landmarks": named,
        "buildings": buildings,
        "roads": roads,
        "parks": parks,
        "trees": trees,
    }


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    with open(argv[1]) as handle:
        local = json.load(handle)
    sky = None
    if len(argv) > 2:
        with open(argv[2]) as handle:
            sky = json.load(handle)
    tiles = []
    for path in argv[3:]:
        with open(path) as handle:
            tiles.append(json.load(handle))
    data = convert(local, sky, tiles)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "aldgate_district.json")
    if not os.path.isdir(os.path.dirname(out)):
        os.makedirs(os.path.dirname(out))
    with open(out, "w") as handle:
        json.dump(data, handle, separators=(",", ":"))
    print("wrote {0}: {1}".format(out, data["counts"]))
    print("bearing {0} deg; tall neighbours: {1}".format(data["frame"]["bearing_deg"],
                                                          ", ".join(data["landmarks"][:30])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
