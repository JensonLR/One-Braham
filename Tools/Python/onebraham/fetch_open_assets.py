# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.fetch_open_assets
===========================

Downloads the open-licence source assets the content build imports - the CC0
skies and PBR texture sets - into
SourceArt/ (ignored by git), and checks each against its published MD5.

Kept out of the repository on purpose: the files are large, they are
someone else's to host, and a fetch with a checksum is as reproducible as a
commit. Every asset here is also listed in Docs/LICENCES.md, and must clear
the bar in Docs/21 §4 before it is added.

For skies it also finds the sun - the brightest pixel of the equirectangular
image - and records its azimuth and elevation in SourceArt/open_assets.json,
so the build can aim the scene's directional light at the sun in the sky.

    python3 -I Tools/Python/onebraham/fetch_open_assets.py

No engine needed.
"""

from __future__ import print_function

import hashlib
import json
import os
import sys

try:
    from urllib.request import Request, urlopen
except ImportError:  # pragma: no cover - Python 2
    from urllib2 import Request, urlopen

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
SOURCE_ART = os.path.join(ROOT, "SourceArt")
MANIFEST = os.path.join(SOURCE_ART, "open_assets.json")

# id, kind, url, md5, licence, page
ASSETS = [
    ("kloofendal_48d_partly_cloudy_puresky", "sky",
     "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/kloofendal_48d_partly_cloudy_puresky_4k.hdr",
     "816340c969d2e60d659051d13ac3c4dc", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky"),
    ("belfast_sunset_puresky", "sky",
     "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/belfast_sunset_puresky_4k.hdr",
     "52d2339c16069a2aaa7fd37a320d8495", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/belfast_sunset_puresky"),
    # PBR texture sets (2K): albedo, DirectX normal, roughness. "set:role" ids;
    # the build maps each set onto a surface in build_materials.TEXTURE_SETS.
    ("terrazzo_tiles:albedo", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/terrazzo_tiles/terrazzo_tiles_diff_2k.jpg",
     "b4bfb01525c22e611d9a0651f372ae91", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/terrazzo_tiles"),
    ("terrazzo_tiles:normal", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/terrazzo_tiles/terrazzo_tiles_nor_dx_2k.jpg",
     "5119c019e7ea4e9b1870c3203a169cfb", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/terrazzo_tiles"),
    ("terrazzo_tiles:roughness", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/terrazzo_tiles/terrazzo_tiles_rough_2k.jpg",
     "f75be6541a4fa5c8197408224946efb3", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/terrazzo_tiles"),
    ("concrete_floor_02:albedo", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/concrete_floor_02/concrete_floor_02_diff_2k.jpg",
     "769c7bf30050cc57be848a67d5c20270", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/concrete_floor_02"),
    ("concrete_floor_02:normal", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/concrete_floor_02/concrete_floor_02_nor_dx_2k.jpg",
     "b59b52b480577e6f20986d3660342696", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/concrete_floor_02"),
    ("concrete_floor_02:roughness", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/concrete_floor_02/concrete_floor_02_rough_2k.jpg",
     "63b266940d40787b56822b7882819711", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/concrete_floor_02"),
    ("oak_wood_planks:albedo", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/oak_wood_planks/oak_wood_planks_diff_2k.jpg",
     "740160778f1c4b78bac74d96fe3c06ea", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/oak_wood_planks"),
    ("oak_wood_planks:normal", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/oak_wood_planks/oak_wood_planks_nor_dx_2k.jpg",
     "404346b76a74374c88226d33e7e2f496", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/oak_wood_planks"),
    ("oak_wood_planks:roughness", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/oak_wood_planks/oak_wood_planks_rough_2k.jpg",
     "d7533fb12d5c296917ff6fb169bb282b", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/oak_wood_planks"),
    ("white_plaster_02:albedo", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/white_plaster_02/white_plaster_02_diff_2k.jpg",
     "35f9c96c16aa0d934da7884b232f6d07", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/white_plaster_02"),
    ("white_plaster_02:normal", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/white_plaster_02/white_plaster_02_nor_dx_2k.jpg",
     "6178690fae17d3728de2f69b48295217", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/white_plaster_02"),
    ("white_plaster_02:roughness", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/white_plaster_02/white_plaster_02_rough_2k.jpg",
     "47f989e9fd3e69e667244ff4b9f406f8", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/white_plaster_02"),
    ("concrete_pavement:albedo", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/concrete_pavement/concrete_pavement_diff_2k.jpg",
     "f7cdc6f38538e2739d48fbb4f1507ddb", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/concrete_pavement"),
    ("concrete_pavement:normal", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/concrete_pavement/concrete_pavement_nor_dx_2k.jpg",
     "78759c726de9867082587563b3e00f35", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/concrete_pavement"),
    ("concrete_pavement:roughness", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/concrete_pavement/concrete_pavement_rough_2k.jpg",
     "670223ce80309b606ceea892f47aa766", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/concrete_pavement"),
    ("asphalt_02:albedo", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/asphalt_02/asphalt_02_diff_2k.jpg",
     "336af399fd98a39ab986d8b3bf73b4ff", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/asphalt_02"),
    ("asphalt_02:normal", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/asphalt_02/asphalt_02_nor_dx_2k.jpg",
     "801b454d4070a499e48c448d299fcf1f", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/asphalt_02"),
    ("asphalt_02:roughness", "texture",
     "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/asphalt_02/asphalt_02_rough_2k.jpg",
     "6fe669ab38640ef2009d6a28d1ad5ee9", "CC0 (Poly Haven)",
     "https://polyhaven.com/a/asphalt_02"),
]


def _md5(path):
    digest = hashlib.md5()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def sun_of(path):
    """(azimuth_deg, elevation_deg, peak) of the brightest pixel of a Radiance
    .hdr, from a quarter-resolution scan (new-style RLE scanlines)."""
    with open(path, "rb") as handle:
        data = handle.read()
    start = data.index(b"\n\n") + 2
    end = data.index(b"\n", start)
    fields = data[start:end].decode("ascii").split()
    height, width = int(fields[1]), int(fields[3])
    pos = end + 1
    best = (0.0, 0, 0)
    for y in range(height):
        if data[pos] != 2 or data[pos + 1] != 2:
            raise ValueError("{0}: not a new-style RLE Radiance file".format(path))
        pos += 4
        channels = []
        for _channel in range(4):
            row = bytearray()
            while len(row) < width:
                count = data[pos]
                pos += 1
                if count > 128:
                    row.extend(bytes(bytearray([data[pos]])) * (count - 128))
                    pos += 1
                else:
                    row.extend(data[pos:pos + count])
                    pos += count
            channels.append(row)
        if y % 4:
            continue
        r, g, b, e = channels
        for x in range(0, width, 4):
            if e[x]:
                value = (r[x] + g[x] + b[x]) * 2.0 ** (e[x] - 136)
                if value > best[0]:
                    best = (value, x, y)
    peak, x, y = best
    return round(x / float(width) * 360.0, 2), round(90.0 - y / float(height) * 180.0, 2), peak


def fetch():
    if not os.path.isdir(SOURCE_ART):
        os.makedirs(SOURCE_ART)
    manifest = {}
    for asset_id, kind, url, md5, licence, page in ASSETS:
        folder = os.path.join(SOURCE_ART, {"sky": "Sky", "texture": "Textures"}.get(kind, "Misc"))
        if not os.path.isdir(folder):
            os.makedirs(folder)
        path = os.path.join(folder, os.path.basename(url))
        if not os.path.exists(path) or _md5(path) != md5:
            print("fetching {0}".format(url))
            request = Request(url, headers={"User-Agent": "OneBrahamBuild/1.0"})
            with open(path, "wb") as handle:
                handle.write(urlopen(request).read())
        if _md5(path) != md5:
            print("CHECKSUM MISMATCH: {0}".format(path))
            return 1
        entry = {"kind": kind, "path": os.path.relpath(path, ROOT), "licence": licence, "page": page}
        if kind == "sky":
            azimuth, elevation, peak = sun_of(path)
            entry.update({"sun_azimuth_deg": azimuth, "sun_elevation_deg": elevation, "sun_peak": peak})
        manifest[asset_id] = entry
        print("ok  {0}  {1}".format(asset_id, entry.get("sun_elevation_deg", "")))
    with open(MANIFEST, "w") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)
    print("wrote {0}".format(MANIFEST))
    return 0


if __name__ == "__main__":
    sys.exit(fetch())
