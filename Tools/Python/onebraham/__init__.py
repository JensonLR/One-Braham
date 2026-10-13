# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham - project automation package.

Everything in ONE BRAHAM that is *content* rather than *code* is built by these
scripts rather than clicked together by hand. That is a deliberate production
decision, not a shortcut:

  * Fifteen floors of an office tower is thousands of placements. A script that
    builds them is reviewable, diffable, and reproducible on a fresh machine.
  * The design is documented as numbers (floor heights, room dimensions, corridor
    widths, spawn distances). The scripts are those numbers in executable form,
    so the documentation and the level cannot drift apart.
  * CI can run the build headlessly and fail the branch if the map, the data
    assets or the validation report regress.

USAGE (from a command prompt, in the project root):

    # Headless full build (what CI runs)
    "<UE5>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" OneBraham.uproject ^
        -run=pythonscript -script="Tools/Python/onebraham/run_all.py"

    # Or from inside the editor's Python console, for iterating:
    import sys, importlib
    sys.path.append(r"<project>/Tools/Python")
    import onebraham.build_environment as be
    importlib.reload(be); be.build_all()

Every script module exposes build_all() so it can be called on its own.
"""

__all__ = [
    "common",
    "build_environment",
    "build_props",
    "build_materials",
    "build_audio",
    "build_characters",
    "build_input",
    "build_ui",
    "build_vfx",
    "validate_project",
    "run_all",
]

__version__ = "1.0.0"
