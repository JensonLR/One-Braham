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
            buildings.append({
                "id": element["id"],
                "name": tags.get("name", ""),
                "h": round(height * 100.0),
                "facade": _facade(tags, height),
                "ring": [[x, y] for x, y in ring],
            })

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
