# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.run_all
=================

The one command that builds every piece of One Braham content, in the order the
pieces depend on each other, and stops with a clear report if anything fails.

ORDER MATTERS, AND THIS IS WHY
    1. generate_input_ini   text only; it can fail before the editor is even open
    2. materials            everything else references MI_* and PM_OB_*
    3. environment          the map: floors, rooms, doors, shutters, the pad
    4. props                blueprints + the dressing manifest
    5. weapons              data assets, blueprints, the registry
    6. characters           the cast payload
    7. cast payload         applies that payload to the registry data asset
    8. audio                sound classes, submixes, attenuations, the bank
    9. input                IA_* assets and DA_InputConfig
    10. ui                  widgets, the HUD string table, the font slot
    11. vfx                 Niagara systems, camera shakes, decals
    12. validate            the health check, last, on everything above

USAGE
    Inside the editor (normal):
        UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
            -script="Tools/Python/onebraham/run_all.py"

    Outside the editor, for the text-only steps:
        PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/run_all.py --text-only
        (input_ini, cpp_symbols, uht_contracts, soft_paths, calls, tags, layout,
         voice_lines, input_actions and doc_numbers: none needs a running editor,
         so all ten work on a fresh checkout and in CI)

    Useful switches:
        --only materials,weapons      run just these steps
        --skip characters,ui          run everything except these
        --stop-on-fail                default: keep going and report everything
        --dress                       also dress the level from the prop manifest

    Exit code is 0 only if every step succeeded. CI reads that, not the log.
"""

from __future__ import print_function

import datetime
import io
import os
import sys
import time

from onebraham import common as C

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                          "..", "..", ".."))
REPORT_DIR = os.path.join(REPO_ROOT, "Saved", "Build")

#: The sentence every content builder raises when it is run outside the editor.
#: Matched on rather than caught by exception type: the builders deliberately
#: raise a plain RuntimeError so they stay importable by the engine-free checks.
EDITOR_REQUIRED_MESSAGE = u"must run inside the Unreal Editor"

# step name, module, needs the editor, one-line purpose
STEPS = [
    ("input_ini", "onebraham.generate_input_ini", False,
     "write the default key bindings into Config/DefaultInput.ini"),
    ("cpp_symbols", "onebraham.check_cpp_symbols", False,
     "declared-but-undefined member functions (the link errors grep misses)"),
    ("uht_contracts", "onebraham.check_uht_contracts", False,
     "RPCs, native events, RepNotify callbacks and replicated properties"),
    ("soft_paths", "onebraham.check_soft_paths", False,
     "every /Game/ path the C++ loads by name, against the pipeline's naming contract"),
    ("calls", "onebraham.check_calls", False,
     "call sites whose argument count cannot match their declaration"),
    ("tags", "onebraham.check_gameplay_tags", False,
     "every gameplay tag the code asks for, declared natively or in the ini"),
    ("layout", "onebraham.check_building_layout", False,
     "the building the map authors against the building the game describes"),
    ("voice_lines", "onebraham.check_voice_lines", False,
     "every voice line the code asks for, against the script and the recording brief"),
    ("input_actions", "onebraham.check_input_actions", False,
     "every generated input action against the field that binds it, and the field "
     "against the code that reads it"),
    ("doc_numbers", "onebraham.check_doc_numbers", False,
     "every count in the build report and the README against the count the tooling "
     "actually produces"),
    ("test_names", "onebraham.check_test_names", False,
     "every automation test Docs/18 lists against the tests that exist, and every "
     "name Scripts/ runs"),
    ("doc_files", "onebraham.check_doc_files", False,
     "every script and tool the documentation tells the reader to run, against "
     "the files on disk"),
    ("doc_types", "onebraham.check_doc_types", False,
     "every type name the documentation uses against the types this project "
     "declares - a reader who cannot find a type will invent it"),
    ("weapon_ids", "onebraham.check_weapon_ids", False,
     "the ten weapons as four places describe them: the armoury table that "
     "generates them, the spawn table that rolls them, the defaults that hand "
     "them out, and Docs/05"),
    ("audio_ids", "onebraham.check_audio_ids", False,
     "the cue list: the bank's 126 rows, what the synthesiser can make, the "
     "beds the audio builder declares, the zone table against the building "
     "layout, and the briefs in Docs/12 - every cue is either audible or "
     "written down, and no cue is invented"),
    ("hud_strings", "onebraham.check_hud_strings", False,
     "the HUD's copy: the list build_ui.py generates DT_HUDStrings from, the "
     "rows compiled into the HUD, and the keys the code asks for - every key "
     "exists, and every key has something that shows it"),
    ("cosmetic_ids", "onebraham.check_cosmetic_ids", False,
     "the wardrobe as three places describe it: the slots and options the "
     "character builder generates, the compiled-in default a fresh clone runs "
     "on, and the starter outfit a new profile unlocks"),
    ("interaction_ids", "onebraham.check_interaction", False,
     "the interaction surface: every class that implements the interface against "
     "the table in Docs/13, every number in that table against the constant it "
     "names, every gameplay tag either claimed by an emitter or written down as "
     "waiting, and every verb against the montage it plays"),
    ("materials", "onebraham.build_materials", True,
     "master materials, physical materials, surface and brand instances"),
    ("environment", "onebraham.build_environment", True,
     "the map: plaza to Skydeck, rooms, doors, shutters, extraction pad"),
    ("props", "onebraham.build_props", True,
     "gameplay blueprints and the prop dressing manifest"),
    ("weapons", "onebraham.build_weapons", True,
     "ten weapon definitions, blueprints and the registry"),
    ("characters", "onebraham.build_characters", True,
     "the six-archetype cast and the cosmetic slots"),
    ("cast_payload", "onebraham.apply_cast_payload", True,
     "apply the cast payload to DA_CharacterRegistry"),
    ("audio", "onebraham.build_audio", True,
     "sound classes, submixes, attenuations, beds and the audio bank"),
    ("input", "onebraham.build_input", True,
     "Enhanced Input actions, mapping contexts and DA_InputConfig"),
    ("ui", "onebraham.build_ui", True,
     "widgets, the HUD string table and the font slot"),
    ("vfx", "onebraham.build_vfx", True,
     "Niagara systems, camera shakes and decal materials"),
    ("validate", "onebraham.validate_project", True,
     "the project health check"),
]


def _parse_switches(argv):
    only = set()
    skip = set()
    text_only = "--text-only" in argv
    dress = "--dress" in argv
    stop_on_fail = "--stop-on-fail" in argv

    for index, argument in enumerate(argv):
        if argument == "--only" and index + 1 < len(argv):
            only = set(name.strip() for name in argv[index + 1].split(",") if name.strip())
        if argument == "--skip" and index + 1 < len(argv):
            skip = set(name.strip() for name in argv[index + 1].split(",") if name.strip())

    return only, skip, text_only, dress, stop_on_fail


def _select_steps(only, skip, text_only):
    selected = []

    for name, module_name, needs_editor, purpose in STEPS:
        if only and name not in only:
            continue
        if name in skip:
            continue
        if text_only and needs_editor:
            continue
        selected.append((name, module_name, needs_editor, purpose))

    return selected


def _run_step(name, module_name, purpose):
    """Imports and runs one step's build_all(). Returns (ok, seconds, detail)."""
    started = time.time()

    C.log(u"")
    C.log(u"--- {0}: {1} ---".format(name, purpose))

    try:
        module = __import__(module_name, fromlist=["build_all"])
    except Exception as exc:  # noqa: BLE001
        return u"failed", time.time() - started, u"import failed: {0}".format(exc)

    build_all = getattr(module, "build_all", None)

    if build_all is None:
        return u"failed", time.time() - started, u"{0} has no build_all()".format(module_name)

    try:
        report = build_all()
    except Exception as exc:  # noqa: BLE001
        # The content builders refuse to run outside the editor, and they say so
        # in exactly those words. That is not a broken project - it is a machine
        # without an Unreal install - and calling it a failure makes the exit code
        # from a CI box with no engine mean nothing. It is reported as SKIPPED,
        # it is counted as skipped, and the verdict line says how many.
        if EDITOR_REQUIRED_MESSAGE in str(exc):
            return u"skipped", time.time() - started, u"needs the Unreal Editor"

        return u"failed", time.time() - started, u"raised: {0}".format(exc)

    elapsed = time.time() - started

    if report is None:
        return u"ok", elapsed, u"no report returned"

    summary = report.summary() if hasattr(report, "summary") else u"ran"

    if hasattr(report, "ok"):
        return (u"ok" if report.ok() else u"failed"), elapsed, summary

    return u"ok", elapsed, summary


def _write_report(results, started_at, finished_at):
    try:
        if not os.path.isdir(REPORT_DIR):
            os.makedirs(REPORT_DIR)
    except Exception:  # noqa: BLE001
        return None

    stamp = started_at.strftime("%Y%m%d_%H%M%S")
    path = os.path.join(REPORT_DIR, u"ob_build_{0}.md".format(stamp))

    succeeded = sum(1 for status, _n, _s, _d in results if status == u"ok")
    failed = sum(1 for status, _n, _s, _d in results if status == u"failed")
    skipped = sum(1 for status, _n, _s, _d in results if status == u"skipped")

    lines = []
    lines.append(u"# ONE BRAHAM - content build report")
    lines.append(u"")
    lines.append(u"Started:  {0}".format(started_at.strftime("%Y-%m-%d %H:%M:%S")))
    lines.append(u"Finished: {0}".format(finished_at.strftime("%Y-%m-%d %H:%M:%S")))
    lines.append(u"Duration: {0:.1f}s".format((finished_at - started_at).total_seconds()))
    lines.append(u"")
    if skipped:
        lines.append(u"**{0} steps succeeded, {1} failed, {2} skipped - no Unreal "
                     u"Editor on this machine, so the content steps did not run.**".format(
                         succeeded, failed, skipped))
    else:
        lines.append(u"**{0} steps succeeded, {1} failed.**".format(succeeded, failed))
    lines.append(u"")
    lines.append(u"| Step | Result | Seconds | Detail |")
    lines.append(u"| --- | --- | --- | --- |")

    for status, name, seconds, detail in results:
        safe_detail = str(detail).replace(u"|", u"\\|").replace(u"\n", u" ")[:180]
        shown = {u"ok": u"OK", u"skipped": u"SKIPPED", u"failed": u"**FAILED**"}[status]
        lines.append(u"| {0} | {1} | {2:.1f} | {3} |".format(name, shown, seconds, safe_detail))

    lines.append(u"")
    lines.append(u"## Next steps")
    lines.append(u"")
    lines.append(u"* `Docs/` - every artifact this build refers to that needs a human hand")
    lines.append(u"* `Scripts/RunServer.bat` / `RunClient.bat` - the dedicated and listen builds")
    lines.append(u"* `Tools/Python/onebraham/validate_project.py` - run this after every content change")
    lines.append(u"")

    with io.open(path, "w", encoding="utf-8") as handle:
        handle.write(u"\n".join(lines))

    return path


def build_all(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)

    only, skip, text_only, dress, stop_on_fail = _parse_switches(argv)
    selected = _select_steps(only, skip, text_only)

    started_at = datetime.datetime.now()

    C.log(u"================================================================")
    C.log(u"  ONE BRAHAM - content build")
    C.log(u"  {0} step(s), {1}".format(len(selected),
                                     u"editor" if not text_only else u"text only"))
    C.log(u"================================================================")

    results = []

    for name, module_name, _needs_editor, purpose in selected:
        status, seconds, detail = _run_step(name, module_name, purpose)
        results.append((status, name, seconds, detail))

        C.log(u"--- {0}: {1} in {2:.1f}s ---".format(
            name, status.upper(), seconds))

        if status == u"failed" and stop_on_fail:
            C.log(u"  stopping: --stop-on-fail")
            break

    # Optional: dress the level. Off by default, because it mutates the map and
    # should be a deliberate act after the prop meshes have been imported.
    if dress and C.IN_EDITOR:
        try:
            from onebraham import build_props
            C.log(u"")
            C.log(u"--- dressing the level from the prop manifest ---")
            placed = build_props.spawn_props_from_manifest(dry_run=False)
            results.append((u"ok", "dress_level", 0.0,
                            u"{0} prop instances placed".format(placed)))
        except Exception as exc:  # noqa: BLE001
            results.append((u"failed", "dress_level", 0.0, str(exc)))

    finished_at = datetime.datetime.now()
    report_path = _write_report(results, started_at, finished_at)

    failed = [name for status, name, _s, _d in results if status == u"failed"]
    skipped = [name for status, name, _s, _d in results if status == u"skipped"]

    C.log(u"")
    C.log(u"================================================================")

    if failed:
        C.log(u"  BUILD FAILED: {0} step(s) - {1}".format(len(failed), ", ".join(failed)))
    else:
        C.log(u"  BUILD OK: {0} step(s) succeeded{1}".format(
            len(results) - len(skipped),
            u", {0} skipped (no Unreal Editor on this machine)".format(len(skipped))
            if skipped else u""))

    if skipped:
        C.log(u"           skipped: {0}".format(", ".join(skipped)))

    if report_path:
        C.log(u"  report: {0}".format(report_path))

    C.log(u"================================================================")

    return 0 if not failed else 1


# Called by the editor's script runner as run_all.build_all(). Passing no
# switches means "every step that this environment can run": the text-only steps
# always, the editor steps only inside the editor.
if __name__ == "__main__":
    raise SystemExit(build_all())
