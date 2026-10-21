# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_frontend
========================

The two maps a player passes through outside a match, and the check that
keeps the credits screen honest.

    /Game/OneBraham/Maps/L_MainMenu   GameDefaultMap: the front end. The tower
                                      over Aldgate at dusk, behind the menu
    /Game/OneBraham/Maps/L_Loading    TransitionMap: black, and nothing else

L_MAINMENU, AND WHY IT IS NOT THE TOWER
    The menu shows the real building, but not by loading it: the persistent
    level is eighteen streamed floors, a navigation mesh and every gameplay
    actor, and a menu that costs a match's frame time and a match's load is a
    menu people wait for. This level is the outside only, built from the same
    sources the tower is:
      * Aldgate East - the SM_District_* meshes build_district.py authored
        from OpenStreetMap (a few draw calls; nothing is rebuilt here)
      * the curtain wall and its fins - build_environment.build_facade, the
        same function the persistent level calls, so the two cannot drift
      * the floors behind the glass - one extruded mass in M_OB_Facade (the
        district's window master) tuned to the tower's 4 m storeys: lit office
        bays, a few blinds down, the lobby's double height on the ground
      * dusk - the Purge's sky (AOneBrahamSkyDirector's DuskState: the Belfast
        sunset dome, a low warm sun, amber haze), fixed rather than blended
      * AOneBrahamFrontEndCamera 150 m west-south-west of the tower, at a
        neighbour's roof height (29 m): the one place in the district with a
        clear line to the crown over Braham Street's roofs (street level is
        walled in by the blocks round the plaza). It drifts a few degrees over
        two and a half minutes. CAMERA below is kept in step with
        AOneBrahamFrontEndGameMode::DefaultCameraLocation, which is where the
        controller stands when the level carries no camera
    WorldSettings names AOneBrahamFrontEndGameMode: no pawn, the menu widget.

L_LOADING
    The engine shows the transition map while a seamless travel swaps worlds,
    and the movie player's loading page (UOneBrahamGameInstance) covers a hard
    load. Empty and black is correct for both; its WorldSettings names the
    engine's own GameModeBase, so opening it directly never starts a match.

THE TYPE (editor)
    import_fonts() brings the four open-licence families the design system is
    set in (SourceArt/Fonts/<Family>/, each with its OFL.txt) into
    /Game/OneBraham/UI/Fonts as UFontFace assets, FF_ + the file name:
      Archivo         Expanded Black / Bold, SemiCondensed Bold / SemiBold /
                      Medium / Regular, Regular        (Omnibus-Type, OFL 1.1)
      IBM Plex Mono   SemiBold, Medium, Regular       (google/fonts, OFL 1.1)
      Doto            Bold, Medium - instanced offline from the variable font
                      (fonttools varLib.instancer wght=700|500 ROND=0)
      Instrument Serif Italic                          (google/fonts, OFL 1.1)
    Every face is Inline: its data cooks into the asset, so the loading page
    (painted on the movie player's thread) never waits on a file. The
    composites are built in code (UOneBrahamUIStyle::Font), one typeface per
    token, so no UFont asset is needed. /Game/OneBraham/UI is always cooked.

    import_display_fonts() bakes the in-world OLED screens' type. Their copy is
    UTextRenderComponent, which draws offline-cached UFonts only (a glyph
    atlas, no runtime rasteriser), so three faces are baked from the same
    files into /Game/OneBraham/UI/Fonts as F_OLED_*:
      F_OLED_Eyebrow    ArchivoSemiCondensed-SemiBold   the HUD's Eyebrow face
      F_OLED_Headline   ArchivoSemiCondensed-Bold       the headline
      F_OLED_Aside      InstrumentSerif-Italic          the building's aside
    UTrueTypeFontFactory finds a face by PostScript name through CoreText on
    a Mac, so each file is registered with CoreText for this process only
    (nothing is installed). Printable ASCII, Latin-1 and the typographic
    punctuation the copy uses; the atlas pages are mipmapped (World group)
    so a nine-metre wall seen from across the atrium does not shimmer.
    Runs in the commandlet: no Slate is involved.

CREDITS (text only, no editor)
    check_credits() reads Docs/LICENCES.md and fails when a credit it asks for
    is missing from UOneBrahamMainMenuWidget::Credits(): the OpenStreetMap
    notice the ODbL requires (and its on-screen line on the menu itself),
    every Poly Haven and ambientCG asset by name, and Epic's stand-ins. The
    credits are compiled in because a cooked build has no Docs folder; this
    is what stops the two drifting.

USAGE
    Scripts/BuildContent.sh --only frontend          the maps (editor)
    python3 Tools/Python/onebraham/run_all.py --only frontend_credits
"""

from __future__ import print_function

import io
import os
import re

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
LICENCES = os.path.join(REPO_ROOT, "Docs", "LICENCES.md")
MENU_SOURCE = os.path.join(REPO_ROOT, "Source", "OneBraham", "UI", "OneBrahamMainMenuWidget.cpp")

MAP_MAIN_MENU = "L_MainMenu"
FOLDER = C.ENVIRONMENT + "/FrontEnd"
DISTRICT_FOLDER = C.ENVIRONMENT + "/District"

# The menu camera. Location and point of interest in step with
# AOneBrahamFrontEndGameMode::DefaultCameraLocation / DefaultCameraLookAt.
CAMERA = {
    "location": (-14772.0, -2605.0, 2500.0),
    "look_at": (0.0, -600.0, 3300.0),
    "fov": 55.0,
    "orbit_degrees": 4.0,
    "rise_cm": 120.0,
    "period_seconds": 150.0,
    "framing_yaw_degrees": 12.0,
}

# AOneBrahamSkyDirector's DuskState, held still: the Purge's sky.
DUSK = {
    "sun_pitch": -2.11,
    "sun_yaw": 127.62,
    "sun_lux": 2500.0,
    "sun_colour": (255, 196, 140),
    "skylight": 1.0,
    "fog_density": 0.005,
    "fog_luminance": (855.0, 558.0, 378.0),
    "dome_luminance": 900.0,
    "dome_haze": (0.95, 0.62, 0.42),
}

# Exposure, EV100: dusk with lit windows. Pinned (min = max) so the picture
# does not breathe as the camera drifts past a lit floor.
EXPOSURE_EV100 = 11.3

# The floors behind the glass: M_OB_Facade at the tower's own module.
TOWER_INTERIOR = {
    "WallColour": (0.10, 0.10, 0.11),       # slab edges and spandrels
    "GlassColour": (0.05, 0.06, 0.07),      # an unlit bay: dark glass
    "FrameColour": (0.30, 0.31, 0.32),
    "PlinthColour": (0.08, 0.08, 0.09),
    "RoofColour": (0.20, 0.20, 0.21),
    "BayWidthCm": 300.0,                    # one office bay lit or not
    "StoreyHeightCm": C.FLOOR_HEIGHT_CM,
    "GroundStoreyCm": C.FLOOR_HEIGHT_CM,    # floor 0, the lobby
    "GroundZ": -C.FLOOR_HEIGHT_CM,          # the plaza
    "WindowWidth": 0.96,
    "WindowHeight": 0.74,                   # the rest is slab and ceiling void
    "SillHeight": 0.04,
    "GroundWindowWidth": 0.94,
    "MullionsAcross": 0.0,
    "TransomsUp": 0.0,
    "BarWidthCm": 4.0,
    "BandCm": 0.0,
    "BandTint": 1.0,
    "WallRoughness": 0.6,
    "FrameMetallic": 0.8,
    "LitWindowShare": 0.62,                 # an office at half past five
    "BlindShare": 0.14,
    "WindowGlow": 700.0,
}
INTERIOR_INSET_CM = 70.0


# ===========================================================================
#  Credits (text only)
# ===========================================================================

# The LICENCES.md sections whose assets the credits name one by one.
CREDITED_SECTIONS = ("OpenStreetMap", "Poly Haven", "ambientCG", "Epic Games", "Fonts")
# Phrases the credits must carry whatever the table says.
REQUIRED_PHRASES = (
    ("OpenStreetMap contributors", "the ODbL attribution"),
    ("ODbL", "the licence the map data is under"),
    ("openstreetmap.org/copyright", "where the ODbL notice points"),
    ("Poly Haven", "the skies and textures"),
    ("ambientCG", "the textures"),
    ("Epic Games", "the mannequins and template weapons"),
    ("SIL Open Font License", "the four typefaces' licence"),
)

# The faces the UI style resolves (UOneBrahamUIStyle.cpp's FaceFor table).
FONT_SOURCE = os.path.join(REPO_ROOT, "SourceArt", "Fonts")
FONT_FOLDER = C.UI + "/Fonts"
FONT_FACES = (
    ("Archivo", "ArchivoExpanded-Black.ttf"),
    ("Archivo", "ArchivoExpanded-Bold.ttf"),
    ("Archivo", "ArchivoSemiCondensed-Bold.ttf"),
    ("Archivo", "ArchivoSemiCondensed-SemiBold.ttf"),
    ("Archivo", "ArchivoSemiCondensed-Medium.ttf"),
    ("Archivo", "ArchivoSemiCondensed-Regular.ttf"),
    ("Archivo", "Archivo-Regular.ttf"),
    ("IBMPlexMono", "IBMPlexMono-SemiBold.ttf"),
    ("IBMPlexMono", "IBMPlexMono-Medium.ttf"),
    ("IBMPlexMono", "IBMPlexMono-Regular.ttf"),
    ("Doto", "Doto-Bold.ttf"),
    ("Doto", "Doto-Medium.ttf"),
    ("InstrumentSerif", "InstrumentSerif-Italic.ttf"),
)


# The OLED screens' offline fonts: (asset, family folder, file, PostScript
# name, atlas height in px). AOneBrahamDisplay's EyebrowFont / HeadlineFont /
# SublineFont default to these paths.
DISPLAY_FONTS = (
    ("F_OLED_Eyebrow", "Archivo", "ArchivoSemiCondensed-SemiBold.ttf", "ArchivoSemiCondensed-SemiBold", 72),
    ("F_OLED_Headline", "Archivo", "ArchivoSemiCondensed-Bold.ttf", "ArchivoSemiCondensed-Bold", 128),
    ("F_OLED_Aside", "InstrumentSerif", "InstrumentSerif-Italic.ttf", "InstrumentSerif-Italic", 96),
)

# Every printable ASCII character (the importer skips any ASCII character not
# listed once a list is given), Latin-1's (the middle dot, the pound, the
# accents), and the punctuation copy is set with.
DISPLAY_FONT_CHARS = (u"".join(chr(c) for c in range(32, 127))
                      + u"".join(chr(c) for c in range(0xA0, 0x100))
                      + u"\u2013\u2014\u2018\u2019\u201C\u201D\u2026\u2022\u2192\u00D7")


def font_asset_name(filename):
    """'ArchivoExpanded-Black.ttf' -> 'FF_ArchivoExpanded_Black'."""
    return "FF_" + os.path.splitext(filename)[0].replace("-", "_")


def missing_font_sources():
    """[(path, why)] for faces or licences missing from SourceArt/Fonts."""
    missing = []
    for family, filename in FONT_FACES:
        path = os.path.join(FONT_SOURCE, family, filename)
        if not os.path.isfile(path):
            missing.append((path, "face"))
        licence = os.path.join(FONT_SOURCE, family, "OFL.txt")
        if not os.path.isfile(licence) and (licence, "licence") not in missing:
            missing.append((licence, "licence"))
    return missing


def _sections(markdown):
    """{heading: body} for every '## ' section of a markdown document."""
    sections, heading, lines = {}, None, []
    for line in markdown.splitlines():
        if line.startswith("## "):
            if heading is not None:
                sections[heading] = "\n".join(lines)
            heading, lines = line[3:].strip(), []
        elif heading is not None:
            lines.append(line)
    if heading is not None:
        sections[heading] = "\n".join(lines)
    return sections


def credited_assets(markdown):
    """The asset names the credits must carry: every [link](...) in a credited section."""
    names = []
    for heading, body in _sections(markdown).items():
        if not heading.startswith(CREDITED_SECTIONS):
            continue
        for name in re.findall(r"\[([^\]]+)\]\(", body):
            if name not in names:
                names.append(name)
    return names


def missing_credits(markdown, source):
    """[(what, why)] the credits lack, against the licences document."""
    missing = []
    for phrase, why in REQUIRED_PHRASES:
        if phrase not in source:
            missing.append((phrase, why))
    for name in credited_assets(markdown):
        if name not in source:
            missing.append((name, "named in Docs/LICENCES.md"))
    return missing


def check_credits():
    """Text-only step: Docs/LICENCES.md against the compiled-in credits."""
    report = C.BuildReport("frontend_credits")
    for path in (LICENCES, MENU_SOURCE):
        if not os.path.isfile(path):
            report.failed(os.path.relpath(path, REPO_ROOT), "missing")
            return report

    with io.open(LICENCES, encoding="utf-8") as handle:
        markdown = handle.read()
    with io.open(MENU_SOURCE, encoding="utf-8") as handle:
        source = handle.read()

    # The menu's own line under the backdrop, which is OpenStreetMap's work.
    if not re.search(r'"Menu_OSM",\s*"Map data \\u00A9 OpenStreetMap contributors"', source):
        report.failed("Menu_OSM", "the main menu no longer credits OpenStreetMap under the backdrop built from it")

    for what, why in missing_credits(markdown, source):
        report.failed(what, u"not in UOneBrahamMainMenuWidget::Credits() ({0})".format(why))

    # The faces the UI is set in, and the licence each family ships with.
    for path, why in missing_font_sources():
        report.failed(os.path.relpath(path, REPO_ROOT), u"missing ({0}): the type system needs it".format(why))

    if report.ok():
        C.log(u"  credits: {0} assets and {1} phrases from Docs/LICENCES.md are on the credits page".format(
            len(credited_assets(markdown)), len(REQUIRED_PHRASES)))
    return report


# ===========================================================================
#  Maps (editor)
# ===========================================================================

def _require_editor():
    if not C.IN_EDITOR:
        raise RuntimeError(
            "build_frontend must run inside the Unreal Editor "
            "(UnrealEditor-Cmd -run=pythonscript).")


def _level_subsystem():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def _open_fresh(report, name):
    """Opens the map emptied of everything a build placed, creating it if new."""
    from onebraham import build_environment as E
    path = "{0}/{1}".format(C.MAPS, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if not _level_subsystem().load_level(path):
            report.failed(path, u"could not open")
            return None
        actors = _actors()
        actors.destroy_actors(E._generated_actors(actors.get_all_level_actors()))
        report.updated_asset(path)
    else:
        if not _level_subsystem().new_level(path):
            report.failed(path, u"could not create")
            return None
        report.created_asset(path)
    return path


def _set_game_mode(report, path, game_mode_class):
    try:
        settings = _world().get_world_settings()
        settings.set_editor_property("default_game_mode", game_mode_class)
    except Exception as exc:  # noqa: BLE001
        report.failed(path + " game mode", str(exc))


def _save(report, path):
    try:
        if not _level_subsystem().save_current_level():
            report.failed(path, u"could not save")
            return False
    except Exception as exc:  # noqa: BLE001
        report.failed(path, u"could not save: {0}".format(exc))
        return False
    return True


def import_fonts(report):
    """The type system's faces, as Inline UFontFace assets in /Game/OneBraham/UI/Fonts."""
    if not unreal.EditorAssetLibrary.does_directory_exist(FONT_FOLDER):
        unreal.EditorAssetLibrary.make_directory(FONT_FOLDER)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    # UFontFace's import touches the Slate application, which a commandlet
    # (BuildContent.sh's -run=pythonscript) does not have: there the faces are
    # kept as imported, and a missing one says how to bring it in.
    commandlet = "-run=" in unreal.SystemLibrary.get_command_line().lower()
    imported = 0
    for family, filename in FONT_FACES:
        source = os.path.join(FONT_SOURCE, family, filename)
        if not os.path.isfile(source):
            report.failed(source, "missing - see SourceArt/Fonts")
            continue
        name = font_asset_name(filename)
        if commandlet:
            path = "{0}/{1}".format(FONT_FOLDER, name)
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                report.skipped_asset(path, "already imported (a commandlet cannot import fonts)")
            else:
                report.failed(path, "not imported: run UnrealEditor OneBraham.uproject "
                                    "-ExecutePythonScript=Tools/Python/onebraham/build_frontend.py (not -Cmd)")
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", source)
        task.set_editor_property("destination_path", FONT_FOLDER)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        factory = getattr(unreal, "FontFileImportFactory", None)
        if factory is not None:
            task.set_editor_property("factory", factory())
        tools.import_asset_tasks([task])
        path = "{0}/{1}".format(FONT_FOLDER, name)
        face = unreal.EditorAssetLibrary.load_asset(path)
        if face is None or not isinstance(face, unreal.FontFace):
            report.failed(path, "the import did not produce a FontFace")
            continue
        try:
            face.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
            face.set_editor_property("hinting", unreal.FontHinting.DEFAULT)
        except Exception as exc:  # noqa: BLE001
            report.failed(path + " policy", str(exc))
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        report.created_asset(path)
        imported += 1
    C.log(u"  fonts: {0} of {1} faces imported into {2}".format(imported, len(FONT_FACES), FONT_FOLDER))
    return imported


def _register_font_file(path):
    """Registers a font file with CoreText for this process, so the TrueType
    factory can find it by PostScript name. True when registered (or already)."""
    import ctypes
    import ctypes.util
    core_foundation = ctypes.cdll.LoadLibrary(ctypes.util.find_library("CoreFoundation"))
    core_text = ctypes.cdll.LoadLibrary(ctypes.util.find_library("CoreText"))
    core_foundation.CFURLCreateFromFileSystemRepresentation.restype = ctypes.c_void_p
    core_foundation.CFURLCreateFromFileSystemRepresentation.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_bool]
    core_foundation.CFRelease.argtypes = [ctypes.c_void_p]
    core_text.CTFontManagerRegisterFontsForURL.restype = ctypes.c_bool
    core_text.CTFontManagerRegisterFontsForURL.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p]
    encoded = path.encode("utf-8")
    url = core_foundation.CFURLCreateFromFileSystemRepresentation(None, encoded, len(encoded), False)
    if not url:
        return False
    try:
        # kCTFontManagerScopeProcess. A second registration of the same file
        # fails as "already registered", which is as good.
        core_text.CTFontManagerRegisterFontsForURL(url, 1, None)
    finally:
        core_foundation.CFRelease(url)
    return True


def import_display_fonts(report):
    """The OLED screens' type, as offline-cached UFonts in /Game/OneBraham/UI/Fonts."""
    import platform
    if platform.system() != "Darwin":
        # Windows finds the face by name among installed fonts, Linux by path.
        report.skipped_asset(FONT_FOLDER + "/F_OLED_*", "baked on a Mac (CoreText registration); "
                                                        "the screens use the engine font until then")
        return 0
    if not unreal.EditorAssetLibrary.does_directory_exist(FONT_FOLDER):
        unreal.EditorAssetLibrary.make_directory(FONT_FOLDER)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    baked = 0
    for name, family, filename, postscript, height in DISPLAY_FONTS:
        source = os.path.join(FONT_SOURCE, family, filename)
        path = "{0}/{1}".format(FONT_FOLDER, name)
        if not os.path.isfile(source):
            report.failed(source, "missing - see SourceArt/Fonts")
            continue
        if not _register_font_file(source):
            report.failed(source, "CoreText could not register it")
            continue
        factory = unreal.TrueTypeFontFactory()
        options = factory.get_editor_property("import_options")
        data = options.get_editor_property("data")
        for prop, value in (("font_name", postscript), ("height", float(height)),
                            ("chars", DISPLAY_FONT_CHARS), ("include_ascii_range", True),
                            ("enable_antialiasing", True), ("use_distance_field_alpha", False),
                            ("alpha_only", True), ("enable_drop_shadow", False),
                            ("texture_page_width", 1024), ("texture_page_max_height", 1024),
                            # Room round each glyph for the mips to fall into.
                            ("x_padding", 8), ("y_padding", 8)):
            data.set_editor_property(prop, value)
        options.set_editor_property("data", data)
        factory.set_editor_property("import_options", options)
        factory.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
        font = tools.create_asset(name, FONT_FOLDER, unreal.Font, factory)
        if font is None or font.get_editor_property("font_cache_type") != unreal.FontCacheType.OFFLINE:
            report.failed(path, "the TrueType factory made no offline font from {0}".format(postscript))
            continue
        pages = 0
        for page in "ABCDEFGH":
            texture = unreal.find_object(None, "{0}.{1}:{1}_Page{2}".format(path, name, page))
            if texture is None:
                break
            pages += 1
            # Coverage is in the alpha channel (alpha_only keeps it in a G8
            # page, TC_DistanceFieldFont, which the font sampler reads as
            # alpha - a TC_Grayscale page would drop it and draw blocks).
            # The importer makes no mips; the far wall wants them.
            texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        report.created_asset(path)
        C.log(u"  {0}: {1} at {2}px, {3} atlas page(s)".format(name, postscript, height, pages))
        baked += 1
    return baked


def build_loading_level(report):
    path = _open_fresh(report, C.MAP_LOADING)
    if path is None:
        return None
    _set_game_mode(report, path, unreal.GameModeBase)
    return path if _save(report, path) else None


def _place_district(report):
    """One actor per SM_District_* mesh: Aldgate, as build_district left it."""
    placed = 0
    for asset_path in unreal.EditorAssetLibrary.list_assets(DISTRICT_FOLDER, recursive=False):
        mesh = unreal.EditorAssetLibrary.load_asset(asset_path.split(".")[0])
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        actor = _actors().spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
        if actor is None:
            continue
        actor.static_mesh_component.set_static_mesh(mesh)
        # Scenery: nobody walks here, and a 6 km ground with complex collision
        # is a physics scene for nothing.
        actor.static_mesh_component.set_collision_profile_name("NoCollision")
        actor.set_actor_label("FrontEnd_" + mesh.get_name())
        placed += 1
    if placed == 0:
        report.skipped_asset(DISTRICT_FOLDER, "no district meshes - run --only environment first; the tower stands alone")
    return placed


def _rounded_ring(half_x, half_y, radius, segments=6):
    """The tower's plan: a rectangle with quarter-round corners, counter-clockwise."""
    import math
    ring = []
    corners = ((half_x - radius, half_y - radius, 0.0), (-half_x + radius, half_y - radius, 90.0),
               (-half_x + radius, -half_y + radius, 180.0), (half_x - radius, -half_y + radius, 270.0))
    for cx, cy, start in corners:
        for step in range(segments + 1):
            angle = math.radians(start + 90.0 * step / segments)
            ring.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle)))
    return ring


def _build_tower_interior(report):
    """The lit floors behind the curtain wall: one extruded mass, M_OB_Facade."""
    from onebraham import build_district as D
    from onebraham import build_environment as E

    master = unreal.EditorAssetLibrary.load_asset(C.MATERIALS + "/M_OB_Facade")
    if master is None:
        report.skipped_asset(C.MATERIALS + "/M_OB_Facade", "no facade master - run --only environment first")
        return None
    vectors = {k: v for k, v in TOWER_INTERIOR.items() if isinstance(v, tuple)}
    scalars = {k: float(v) for k, v in TOWER_INTERIOR.items() if not isinstance(v, tuple)}
    material = D._instance("MI_OB_Facade_FrontEndTower", master, vectors, scalars, report)

    geometry = D._geometry_script()
    if geometry is None:
        report.failed("tower interior", "Geometry Script is not loaded - enable the GeometryScripting plugin")
        return None

    fx, fy, r = 2215.0 - INTERIOR_INSET_CM, 1815.0 - INTERIOR_INSET_CM, E.SOFT_CORNER_R - INTERIOR_INSET_CM
    z_ground, z15, z17 = E._floor_z(0), E._floor_z(15), E._floor_z(17)
    mesh = unreal.DynamicMesh()
    options = D._options()
    # Ground to the floor-15 slab, full plate.
    points = [unreal.Vector2D(x, y) for x, y in D._ccw(_rounded_ring(fx, fy, r))]
    geometry["prim"].append_simple_extrude_polygon(mesh, options, D._xf(0.0, 0.0, z_ground), points, z15 - z_ground, 0, True)
    # The crown over the canteen half: 15 to 17. The balcony north of the
    # screen is open sky, so it is left open.
    screen = E.SKYDECK_SCREEN_Y - INTERIOR_INSET_CM
    crown = [(fx, -fy), (fx, screen), (-fx, screen), (-fx, -fy)]
    points = [unreal.Vector2D(x, y) for x, y in D._ccw(crown)]
    geometry["prim"].append_simple_extrude_polygon(mesh, options, D._xf(0.0, 0.0, z15), points, z17 - z15, 0, True)
    geometry["normals"].set_per_face_normals(mesh)

    if not unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
        unreal.EditorAssetLibrary.make_directory(FOLDER)
    path = FOLDER + "/SM_FrontEnd_TowerInterior"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    create = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    for prop, value in (("enable_recompute_normals", False), ("enable_recompute_tangents", True),
                        ("enable_nanite", False), ("enable_collision", False)):
        try:
            create.set_editor_property(prop, value)
        except Exception:  # noqa: BLE001
            pass
    result = geometry["assets"].create_new_static_mesh_asset_from_mesh(mesh, path, create)
    static_mesh = result[0] if isinstance(result, tuple) else result
    if static_mesh is None:
        report.failed(path, "Geometry Script could not create the mesh")
        return None
    if material is not None:
        static_mesh.set_material(0, material)
    unreal.EditorAssetLibrary.save_asset(path)
    report.created_asset(path)

    actor = _actors().spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
    actor.static_mesh_component.set_static_mesh(static_mesh)
    actor.static_mesh_component.set_collision_profile_name("NoCollision")
    actor.set_actor_label("FrontEnd_TowerInterior")
    return actor


def _build_dusk(report):
    """The Purge's sky, held still: dome, sun, haze, skylight, grade."""
    from onebraham import build_district as D

    dome_master = unreal.EditorAssetLibrary.load_asset(C.MATERIALS + "/M_OB_SkyDome")
    if dome_master is not None:
        instance = D._instance("MI_OB_SkyDome_FrontEnd", dome_master,
                               {"HazeColour": DUSK["dome_haze"]},
                               {"SkyLuminance": DUSK["dome_luminance"], "SunClamp": D.SKY_SUN_CLAMP,
                                "DuskBlend": 1.0}, report)
        dome = _actors().spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
        component = dome.static_mesh_component
        component.set_static_mesh(unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere.Sphere"))
        component.set_material(0, instance or dome_master)
        component.set_editor_property("cast_shadow", False)
        component.set_collision_profile_name("NoCollision")
        dome.set_actor_scale3d(unreal.Vector(10000.0, 10000.0, 10000.0))
        dome.set_actor_label("FrontEnd_SkyDome_Dusk")
    else:
        report.skipped_asset(C.MATERIALS + "/M_OB_SkyDome", "no photographed sky - the procedural atmosphere stands in")
        atmosphere = _actors().spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0))
        if atmosphere is not None:
            atmosphere.set_actor_label("FrontEnd_Atmosphere")

    sun = _actors().spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 8000.0),
                                           unreal.Rotator(roll=0.0, pitch=DUSK["sun_pitch"], yaw=DUSK["sun_yaw"]))
    if sun is not None:
        sun.set_actor_label("FrontEnd_Sun_Dusk")
        light = sun.light_component
        light.set_editor_property("intensity", DUSK["sun_lux"])
        colour = DUSK["sun_colour"]
        light.set_editor_property("light_color", unreal.Color(r=colour[0], g=colour[1], b=colour[2], a=255))
        light.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)

    fog = _actors().spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, -400.0))
    if fog is not None:
        fog.set_actor_label("FrontEnd_Fog_Dusk")
        component = fog.component
        component.set_editor_property("fog_density", DUSK["fog_density"])
        component.set_editor_property("fog_height_falloff", 0.04)
        component.set_editor_property("start_distance", 4000.0)
        lum = DUSK["fog_luminance"]
        component.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(lum[0], lum[1], lum[2], 1.0))

    skylight = _actors().spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 2000.0))
    if skylight is not None:
        skylight.set_actor_label("FrontEnd_Skylight")
        component = skylight.light_component
        component.set_editor_property("intensity", DUSK["skylight"])
        component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        component.set_editor_property("real_time_capture", True)

    ppv = _actors().spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 0.0))
    if ppv is not None:
        ppv.set_actor_label("FrontEnd_Grade")
        ppv.set_editor_property("unbound", True)
        settings = ppv.settings
        for prop, value in (("bOverride_AutoExposureMinBrightness", True), ("AutoExposureMinBrightness", EXPOSURE_EV100),
                            ("bOverride_AutoExposureMaxBrightness", True), ("AutoExposureMaxBrightness", EXPOSURE_EV100),
                            ("bOverride_FilmGrainIntensity", True), ("FilmGrainIntensity", 0.18),
                            ("bOverride_VignetteIntensity", True), ("VignetteIntensity", 0.45),
                            ("bOverride_ColorSaturation", True),
                            ("ColorSaturation", unreal.Vector4(0.94, 0.94, 0.94, 1.0))):
            try:
                settings.set_editor_property(prop, value)
            except Exception as exc:  # noqa: BLE001
                report.failed("FrontEnd_Grade." + prop, str(exc))
        ppv.set_editor_property("settings", settings)


def _place_camera(report):
    camera_class = getattr(unreal, "OneBrahamFrontEndCamera", None)
    if camera_class is None:
        report.failed("FrontEnd_Camera", "OneBrahamFrontEndCamera is not loaded - build the editor target first")
        return None
    x, y, z = CAMERA["location"]
    camera = _actors().spawn_actor_from_class(camera_class, unreal.Vector(x, y, z))
    if camera is None:
        report.failed("FrontEnd_Camera", "could not spawn")
        return None
    camera.set_actor_label("FrontEnd_Camera")
    lx, ly, lz = CAMERA["look_at"]
    for prop, value in (("look_at", unreal.Vector(lx, ly, lz)),
                        ("orbit_degrees", CAMERA["orbit_degrees"]),
                        ("rise_cm", CAMERA["rise_cm"]),
                        ("period_seconds", CAMERA["period_seconds"]),
                        ("framing_yaw_degrees", CAMERA["framing_yaw_degrees"])):
        camera.set_editor_property(prop, value)
    try:
        camera.camera_component.set_editor_property("field_of_view", CAMERA["fov"])
    except Exception as exc:  # noqa: BLE001
        report.failed("FrontEnd_Camera fov", str(exc))
    return camera


def build_main_menu_level(report):
    from onebraham import build_environment as E

    path = _open_fresh(report, MAP_MAIN_MENU)
    if path is None:
        return None

    district = C.guard(report, "front end district", _place_district, report) or 0
    materials = C.guard(report, "front end materials", E.build_materials, report) or {}
    C.guard(report, "front end facade", E.build_facade, materials)
    C.guard(report, "front end interior", _build_tower_interior, report)
    C.guard(report, "front end dusk", _build_dusk, report)
    C.guard(report, "front end camera", _place_camera, report)

    game_mode = getattr(unreal, "OneBrahamFrontEndGameMode", None)
    if game_mode is None:
        report.failed(path, "OneBrahamFrontEndGameMode is not loaded - build the editor target first")
    else:
        _set_game_mode(report, path, game_mode)

    C.log(u"  {0}: {1} district meshes, the facade, the lit floors, dusk, the camera".format(MAP_MAIN_MENU, district))
    return path if _save(report, path) else None


def build_all():
    _require_editor()
    report = C.BuildReport("build_frontend")
    C.log(u"=== Building the front end ===")

    if not unreal.EditorAssetLibrary.does_directory_exist(C.MAPS):
        unreal.EditorAssetLibrary.make_directory(C.MAPS)

    C.step(1, 4, "Fonts")
    C.guard(report, FONT_FOLDER, import_fonts, report)
    C.step(2, 4, "Display fonts")
    C.guard(report, FONT_FOLDER + "/F_OLED_*", import_display_fonts, report)
    C.step(3, 4, "L_Loading")
    C.guard(report, C.MAP_LOADING, build_loading_level, report)
    C.step(4, 4, "L_MainMenu")
    C.guard(report, MAP_MAIN_MENU, build_main_menu_level, report)

    C.log(report.summary())
    return report


if __name__ == "__main__":
    if C.IN_EDITOR:
        _report = build_all()
    else:
        _report = check_credits()
        C.log(_report.summary())
    if not _report.ok():
        raise SystemExit(1)
