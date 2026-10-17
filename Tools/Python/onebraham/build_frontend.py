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
CREDITED_SECTIONS = ("OpenStreetMap", "Poly Haven", "ambientCG", "Epic Games")
# Phrases the credits must carry whatever the table says.
REQUIRED_PHRASES = (
    ("OpenStreetMap contributors", "the ODbL attribution"),
    ("ODbL", "the licence the map data is under"),
    ("openstreetmap.org/copyright", "where the ODbL notice points"),
    ("Poly Haven", "the skies and textures"),
    ("ambientCG", "the textures"),
    ("Epic Games", "the mannequins and template weapons"),
)


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

    C.step(1, 2, "L_Loading")
    C.guard(report, C.MAP_LOADING, build_loading_level, report)
    C.step(2, 2, "L_MainMenu")
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
